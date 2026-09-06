// engine/systems/physics.h
#pragma once

#include <pthread.h>

#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/canvas.h"
#include "../components/physics.h"
#include "../ecs/ecs.h"
#include "../systems/canvas.h"
#include "../systems/sph_kernels.h"
#include "../systems/ui_helpers.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Thread Pool
 * ============================================================================
 */
inline int num_threads = 1;
inline bool threads_initialized = false;
inline ECS* ecs_ptr = nullptr;

inline void InitThreads(int threads, ECS& ecs) {
  if (threads_initialized) return;
  num_threads = threads > 0 ? threads : 1;
  ecs_ptr = &ecs;
  threads_initialized = true;
  logger::info("[APP] Created {} threads for simulation", threads);
}

inline void ShutdownThreads() { threads_initialized = false; }

inline int GetEffectiveThreads(size_t particle_count) {
  if (particle_count < 256) return 1;
  size_t min_per_thread = 32;
  size_t effective = particle_count / min_per_thread;
  if (effective < 2) return 1;
  if (effective > (size_t)num_threads) return num_threads;
  return (int)effective;
}

/**
 * ============================================================================
 * Parallel chunk dispatch using pthreads
 * ============================================================================
 *
 * Runs `fn(&task)` concurrently over `particle_count` items split into
 * `effective` contiguous chunks. Each task receives a thread index.
 */
template <typename Task, typename Fn>
inline void RunParallelChunks(int effective, size_t particle_count,
                              const Task& seed, Fn fn) {
  int n = static_cast<int>(particle_count);
  int chunk_size = n / effective;
  if (chunk_size < 64) chunk_size = 64;

  std::vector<pthread_t> threads(effective);
  std::vector<Task> tasks(effective);

  for (int i = 0; i < effective; ++i) {
    tasks[i] = seed;
    tasks[i].start = i * chunk_size;
    tasks[i].end = std::min(tasks[i].start + chunk_size, n);
    pthread_create(&threads[i], nullptr, fn, &tasks[i]);
  }

  for (int i = 0; i < effective; ++i) {
    pthread_join(threads[i], nullptr);
  }
}

/**
 * ============================================================================
 * Runtime Buffers
 * ============================================================================
 */

inline std::vector<Entity> particle_entities;
inline std::vector<Vector2> predicted_positions;
inline std::vector<components::PositionComponent*> pos_cache;
inline std::vector<components::VelocityComponent*> vel_cache;
inline std::vector<components::CircleComponent*> circ_cache;
inline bool particle_entities_cached = false;

inline void CacheParticleEntities(ECS& ecs) {
  particle_entities.clear();
  ecs.group_view<components::PositionComponent>(
    [&](Entity e, components::PositionComponent&) {
      particle_entities.push_back(e);
    });

  size_t n = particle_entities.size();
  predicted_positions.resize(n);
  pos_cache.resize(n);
  vel_cache.resize(n);
  circ_cache.resize(n);

  for (size_t i = 0; i < n; ++i) {
    pos_cache[i] =
      &ecs.get<components::PositionComponent>(particle_entities[i]);
    vel_cache[i] =
      &ecs.get<components::VelocityComponent>(particle_entities[i]);
    circ_cache[i] = &ecs.get<components::CircleComponent>(particle_entities[i]);
  }

  particle_entities_cached = true;
  logger::info("[PHYSICS] Cached {} particles", particle_entities.size());
}

/**
 * ============================================================================
 * Runtime Buffers
 * ============================================================================
 */
inline std::unordered_map<GridCell, std::vector<size_t>, GridCellHash>
  spatial_grid;

inline void BuildSpatialGrid(float h) {
  spatial_grid.clear();

  for (size_t i = 0; i < predicted_positions.size(); ++i) {
    GridCell cell = PositionToCell(predicted_positions[i], h);
    spatial_grid[cell].push_back(i);
  }
}

inline float cached_gravity_accel = 0.f;
inline bool kernel_cache_valid = false;

inline void UpdateKernelCache(float gravity) {
  if (kernel_cache_valid && cached_gravity_accel == gravity * 10.f) return;
  cached_gravity_accel = gravity * 10.f;
  kernel_cache_valid = true;
}

inline float ConvertDensityToPressure(const components::SimulationComponent& sim,
                                      float density) {
  return (density - sim.target_density) * sim.pressure_multiplier;
}

/**
 * ============================================================================
 * Prediction
 * ============================================================================
 */

inline void PredictPositions(ECS& ecs, float dt) {
  if (!particle_entities_cached) {
    CacheParticleEntities(ecs);
  }

  auto& sim = entities::Simulation(ecs);

  float gravity = cached_gravity_accel;
  size_t n = particle_entities.size();
  int effective = GetEffectiveThreads(n);

  if (effective <= 1 || n < 256) {
    for (size_t i = 0; i < n; ++i) {
      vel_cache[i]->velocity.y += gravity * dt;
      predicted_positions[i] = {
        pos_cache[i]->position.x + vel_cache[i]->velocity.x * dt,
        pos_cache[i]->position.y + vel_cache[i]->velocity.y * dt};
    }
  } else {
    struct PredictTask {
      int start;
      int end;
      float grav;
      float dt;
    };
    PredictTask seed{0, 0, gravity, dt};
    RunParallelChunks(effective, n, seed, [](void* arg) -> void* {
      auto* tk = static_cast<PredictTask*>(arg);
      for (int i = tk->start; i < tk->end; ++i) {
        vel_cache[i]->velocity.y += tk->grav * tk->dt;
        predicted_positions[i] = {
          pos_cache[i]->position.x + vel_cache[i]->velocity.x * tk->dt,
          pos_cache[i]->position.y + vel_cache[i]->velocity.y * tk->dt};
      }
      return nullptr;
    });
  }

  BuildSpatialGrid(sim.smoothing_radius);
}

/**
 * ============================================================================
 * Density
 * ============================================================================
 */

struct ParallelDensityTask {
  int start;
  int end;
  float h;
  float mass;
};

inline std::vector<float> temp_densities;

inline void* ComputeDensityRange(void* arg) {
  auto* task = static_cast<ParallelDensityTask*>(arg);
  int start = task->start;
  int end = task->end;

  float h = task->h;
  float h2 = h * h;
  float mass = task->mass;

  for (int i = start; i < end && i < static_cast<int>(particle_entities.size());
       ++i) {
    Vector2 p = predicted_positions[i];
    GridCell cell = PositionToCell(p, h);

    float density = 0.f;

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        GridCell neighbor{cell.x + dx, cell.y + dy};

        auto it = spatial_grid.find(neighbor);
        if (it == spatial_grid.end()) continue;

        for (size_t j : it->second) {
          Vector2 p2 = predicted_positions[j];

          float rx = p2.x - p.x;
          float ry = p2.y - p.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= h2) density += mass * Poly6Kernel(r2, h);
        }
      }
    }

    temp_densities[i] = density;
  }

  return nullptr;
}

/**
 * ============================================================================
 * Pressure
 * ============================================================================
 */

inline void ComputeParticlePressure(ECS& ecs) {
  auto& sim = entities::Simulation(ecs);
  ecs.group_view<components::CircleComponent>(
    [&](Entity, components::CircleComponent& c) {
      c.pressure = ConvertDensityToPressure(sim, c.density);
    });
}

/**
 * ============================================================================
 * Pressure Force
 * ============================================================================
 */

struct ParallelForceTask {
  int start;
  int end;
  float h;
  float mass;
};

inline std::vector<Vector2> pressure_forces;
inline std::vector<Vector2> viscosity_forces;
inline std::vector<Vector2> cohesion_forces;
inline std::vector<float> densities;
inline std::vector<float> pressures;
inline std::vector<Vector2> velocities;

inline void* ComputePressureForceRange(void* arg) {
  auto* task = static_cast<ParallelForceTask*>(arg);
  int start = task->start;
  int end = task->end;

  float h = task->h;
  float h2 = h * h;
  float mass = task->mass;

  for (int i = start; i < end && i < static_cast<int>(particle_entities.size());
       ++i) {
    Vector2 p1 = predicted_positions[i];
    GridCell cell = PositionToCell(p1, h);

    float d1 = densities[i];
    float p1_pressure = pressures[i];
    Vector2 v1 = velocities[i];

    Vector2 pressure_force{0.f, 0.f};
    Vector2 viscosity_force{0.f, 0.f};
    Vector2 cohesion_force{0.f, 0.f};

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        GridCell neighbor{cell.x + dx, cell.y + dy};

        auto it = spatial_grid.find(neighbor);
        if (it == spatial_grid.end()) continue;

        for (size_t j : it->second) {
          if (i == j) continue;

          Vector2 p2 = predicted_positions[j];

          float rx = p1.x - p2.x;
          float ry = p1.y - p2.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= 0.f || r2 > h2) continue;

          float r = sqrtf(r2);

          Vector2 dir{rx / r, ry / r};

          float grad = SpikyKernelGradient(r, h);

          float d2 = densities[j];
          float p2_pressure = pressures[j];
          Vector2 v2 = velocities[j];

          float term = (p1_pressure / (d1 * d1)) + (p2_pressure / (d2 * d2));

          float factor = -mass * term * grad;

          pressure_force.x += dir.x * factor;
          pressure_force.y += dir.y * factor;

          float visc = ViscosityKernel(r, h);

          viscosity_force.x += (v2.x - v1.x) * visc;
          viscosity_force.y += (v2.y - v1.y) * visc;

          float cohes = CohesionKernel(r, h);
          cohesion_force.x += dir.x * cohes;
          cohesion_force.y += dir.y * cohes;
        }
      }
    }

    pressure_forces[i] = pressure_force;
    viscosity_forces[i] = viscosity_force;
    cohesion_forces[i] = cohesion_force;
  }

  return nullptr;
}

inline void ComputeParticlePressureForce(ECS& ecs, float dt) {
  auto& sim = entities::Simulation(ecs);

  int effective_threads = GetEffectiveThreads(particle_entities.size());
  if (!threads_initialized || effective_threads <= 1 ||
      particle_entities.size() < 256) {
    float h = sim.smoothing_radius;
    float h2 = h * h;
    float mass = sim.particle_size;
    float viscosity = sim.viscosity;
    float surface_tension = sim.surface_tension;

    for (size_t i = 0; i < particle_entities.size(); ++i) {
      Entity e1 = particle_entities[i];

      auto& c1 = ecs.get<components::CircleComponent>(e1);
      auto& v1 = ecs.get<components::VelocityComponent>(e1);

      Vector2 p1 = predicted_positions[i];
      GridCell cell = PositionToCell(p1, h);

      Vector2 pressure_force{0.f, 0.f};
      Vector2 viscosity_force{0.f, 0.f};
      Vector2 cohesion_force{0.f, 0.f};

      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          GridCell neighbor{cell.x + dx, cell.y + dy};

          auto it = spatial_grid.find(neighbor);
          if (it == spatial_grid.end()) continue;

          for (size_t j : it->second) {
            if (i == j) continue;

            Entity e2 = particle_entities[j];

            auto& c2 = ecs.get<components::CircleComponent>(e2);
            auto& v2 = ecs.get<components::VelocityComponent>(e2);

            Vector2 p2 = predicted_positions[j];

            float rx = p1.x - p2.x;
            float ry = p1.y - p2.y;

            float r2 = rx * rx + ry * ry;

            if (r2 <= 0.f || r2 > h2) continue;

            float r = sqrtf(r2);

            Vector2 dir{rx / r, ry / r};

            float grad = SpikyKernelGradient(r, h);

            float term = (c1.pressure / (c1.density * c1.density)) +
                         (c2.pressure / (c2.density * c2.density));

            float factor = -mass * term * grad;

            pressure_force.x += dir.x * factor;
            pressure_force.y += dir.y * factor;

            float visc = ViscosityKernel(r, h);

            viscosity_force.x += (v2.velocity.x - v1.velocity.x) * visc;
            viscosity_force.y += (v2.velocity.y - v1.velocity.y) * visc;

            float cohes = CohesionKernel(r, h);
            cohesion_force.x += dir.x * cohes;
            cohesion_force.y += dir.y * cohes;
          }
        }
      }

      v1.velocity.x += pressure_force.x * dt;
      v1.velocity.y += pressure_force.y * dt;

      v1.velocity.x += viscosity_force.x * viscosity;
      v1.velocity.y += viscosity_force.y * viscosity;

      v1.velocity.x += cohesion_force.x * surface_tension * mass;
      v1.velocity.y += cohesion_force.y * surface_tension * mass;
    }
    return;
  }

  int n = static_cast<int>(particle_entities.size());
  pressure_forces.resize(n);
  viscosity_forces.resize(n);
  cohesion_forces.resize(n);
  densities.resize(n);
  pressures.resize(n);
  velocities.resize(n);

  for (int i = 0; i < n; ++i) {
    auto& c = ecs_ptr->get<components::CircleComponent>(particle_entities[i]);
    auto& v = ecs_ptr->get<components::VelocityComponent>(particle_entities[i]);
    densities[i] = c.density;
    pressures[i] = c.pressure;
    velocities[i] = v.velocity;
  }

  ParallelForceTask seed{0, 0, sim.smoothing_radius, sim.particle_size};
  RunParallelChunks(effective_threads, n, seed, ComputePressureForceRange);

  float viscosity = sim.viscosity;
  float surface_tension = sim.surface_tension;
  float mass = sim.particle_size;

  for (int i = 0; i < n; ++i) {
    auto& v1 = ecs.get<components::VelocityComponent>(particle_entities[i]);

    v1.velocity.x += pressure_forces[i].x * dt;
    v1.velocity.y += pressure_forces[i].y * dt;

    v1.velocity.x += viscosity_forces[i].x * viscosity * 50.f;
    v1.velocity.y += viscosity_forces[i].y * viscosity * 50.f;

    v1.velocity.x += cohesion_forces[i].x * surface_tension * mass;
    v1.velocity.y += cohesion_forces[i].y * surface_tension * mass;
  }
}

/**
 * ============================================================================
 * Selection
 * ============================================================================
 */

inline bool IsCanvasHit(ECS& ecs, Vector2 mouse_world, float tolerance) {
  bool hit = false;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Vector2 local_mouse = WorldToCanvasLocal(mouse_world, canvas);

      auto within = [](float value, float edge, float tol) {
        return value >= edge - tol && value <= edge + tol;
      };

      bool inside =
        local_mouse.x >= -canvas.half_extents.x &&
        local_mouse.x <= canvas.half_extents.x &&
        local_mouse.y >= -canvas.half_extents.y &&
        local_mouse.y <= canvas.half_extents.y;

      bool touching_edge =
        within(local_mouse.x, -canvas.half_extents.x, tolerance) ||
        within(local_mouse.x, canvas.half_extents.x, tolerance) ||
        within(local_mouse.y, -canvas.half_extents.y, tolerance) ||
        within(local_mouse.y, canvas.half_extents.y, tolerance);

      hit = tolerance > 0.f ? touching_edge : inside;
    });
  return hit;
}

inline bool IsMouseOverCanvas(ECS& ecs, Vector2 mouse_world) {
  float tolerance = 0.f;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      tolerance = canvas.edge_tolerance;
    });
  return IsCanvasHit(ecs, mouse_world, tolerance);
}

inline bool IsMouseOnCanvas(ECS& ecs, Vector2 mouse_world) {
  return IsCanvasHit(ecs, mouse_world, 0.f);
}

inline void UpdateSelectionInput(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);
  if (!sim.selection_active) return;

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  if (UIConsumesMouse(ecs, mouse_screen) ||
      IsMouseOverAnyWindow(ecs, mouse_screen))
    return;

  if (IsMouseOverCanvas(ecs, mouse_world)) {
    sim.selection_locked = false;
    sim.selection_density = 0.f;
    return;
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    sim.selection_center = mouse_world;
    sim.selection_locked = true;
  }

  if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
    sim.selection_locked = false;
    sim.selection_density = 0.f;
  }
}

inline void UpdatePathInput(ECS& ecs,
                            const engine::components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  bool ui_consumes = UIConsumesMouse(ecs, mouse_screen);
  bool over_window = IsMouseOverAnyWindow(ecs, mouse_screen);

  if (ui_consumes || over_window) {
    return;
  }

  // Toggle drawing mode with 'B' key
  static bool draw_key_was_down = false;
  bool draw_key_down = IsKeyDown(KEY_B);
  if (draw_key_down && !draw_key_was_down) {
    sim.is_drawing_path = !sim.is_drawing_path;
    if (sim.is_drawing_path) {
      sim.user_path_points.clear();
      if (IsMouseOnCanvas(ecs, mouse_world)) {
        // Store in canvas-local coordinates
        ecs.group_view<components::CanvasComponent>(
          [&](Entity, components::CanvasComponent& canvas) {
            sim.user_path_points.push_back(
              WorldToCanvasLocal(mouse_world, canvas));
          });
      }
    }
  }
  draw_key_was_down = draw_key_down;

  // Continue drawing while in drawing mode and on canvas
  if (sim.is_drawing_path && IsMouseOnCanvas(ecs, mouse_world)) {
    if (!sim.user_path_points.empty()) {
      // Convert last stored point (canvas-local) back to world for distance check
      Vector2 last_world = mouse_world;
      ecs.group_view<components::CanvasComponent>(
        [&](Entity, components::CanvasComponent& canvas) {
          last_world =
            CanvasLocalToWorld(sim.user_path_points.back(), canvas);
        });

      float dx = mouse_world.x - last_world.x;
      float dy = mouse_world.y - last_world.y;
      float dist = sqrtf(dx * dx + dy * dy);

      if (dist >= sim.path_point_spacing) {
        // Store in canvas-local coordinates
        ecs.group_view<components::CanvasComponent>(
          [&](Entity, components::CanvasComponent& canvas) {
            sim.user_path_points.push_back(
              WorldToCanvasLocal(mouse_world, canvas));
          });
      }
    } else {
      ecs.group_view<components::CanvasComponent>(
        [&](Entity, components::CanvasComponent& canvas) {
          sim.user_path_points.push_back(
            WorldToCanvasLocal(mouse_world, canvas));
        });
    }
  }
}

inline void UpdateSelectionDensity(ECS& ecs) {
  auto& sim = entities::Simulation(ecs);
  if (!sim.selection_locked) return;

  float nearest = FLT_MAX;

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::CircleComponent& c) {
      float dx = pos.position.x - sim.selection_center.x;
      float dy = pos.position.y - sim.selection_center.y;

      float d = dx * dx + dy * dy;

      if (d < nearest) {
        nearest = d;
        sim.selection_density = c.density;
        sim.selected_particle = e;
      }
    });
}

/**
 * ============================================================================
 * Collisions
 * ============================================================================
 */
inline void ResolveCollisions(ECS& ecs) {
  const float particle_repulsion = 0.05f;
  auto& sim = entities::Simulation(ecs);

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      ecs
        .group_view<components::PositionComponent,
                    components::VelocityComponent, components::CircleComponent>(
          [&](Entity, components::PositionComponent& pos,
              components::VelocityComponent& vel,
              components::CircleComponent& c) {
            Vector2 local_pos = WorldToCanvasLocal(pos.position, canvas);

            float hx = canvas.half_extents.x - c.radius;
            float hy = canvas.half_extents.y - c.radius;

            float dist_from_center =
              std::sqrt(local_pos.x * local_pos.x + local_pos.y * local_pos.y);
            float corner_dist = std::sqrt(hx * hx + hy * hy);

            bool is_near_corner = dist_from_center > corner_dist * 0.8f;
            bool is_outside = local_pos.x < -hx || local_pos.x > hx ||
                              local_pos.y < -hy || local_pos.y > hy;

            if (is_outside) {
              local_pos.x = std::clamp(local_pos.x, -hx, hx);
              local_pos.y = std::clamp(local_pos.y, -hy, hy);

              Vector2 corrected_world = CanvasLocalToWorld(local_pos, canvas);
              pos.position.x = corrected_world.x;
              pos.position.y = corrected_world.y;

              float damp = is_near_corner ? 0.7f : 0.85f;
              vel.velocity.x *= damp;
              vel.velocity.y *= damp;
            }
          });
    });

  struct ParticleRef {
    components::PositionComponent* pos;
    components::VelocityComponent* vel;
    components::CircleComponent* circ;
  };
  std::vector<ParticleRef> particles;
  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      particles.push_back({&pos, &vel, &c});
    });

  if (particles.size() < 50) {
    for (size_t i = 0; i < particles.size(); ++i) {
      auto& a = particles[i];
      for (size_t j = i + 1; j < particles.size(); ++j) {
        auto& b = particles[j];

        Vector2 delta = {b.pos->position.x - a.pos->position.x,
                         b.pos->position.y - a.pos->position.y};
        float dist_sq = delta.x * delta.x + delta.y * delta.y;
        if (dist_sq <= 0.f) continue;

        float dist = std::sqrt(dist_sq);
        float radius_sum = a.circ->radius + b.circ->radius;
        Vector2 dir = {delta.x / dist, delta.y / dist};

        float repulse_dist = radius_sum * 3.f;
        if (dist < repulse_dist) {
          float repulse_strength = (repulse_dist - dist) / dist;
          a.vel->velocity.x -= dir.x * particle_repulsion * repulse_strength;
          a.vel->velocity.y -= dir.y * particle_repulsion * repulse_strength;
          b.vel->velocity.x += dir.x * particle_repulsion * repulse_strength;
          b.vel->velocity.y += dir.y * particle_repulsion * repulse_strength;
        }

        if (dist < radius_sum) {
          float overlap = radius_sum - dist;
          a.pos->position.x -= dir.x * overlap * 0.5f;
          a.pos->position.y -= dir.y * overlap * 0.5f;
          b.pos->position.x += dir.x * overlap * 0.5f;
          b.pos->position.y += dir.y * overlap * 0.5f;

          float dot = (b.vel->velocity.x - a.vel->velocity.x) * dir.x +
                      (b.vel->velocity.y - a.vel->velocity.y) * dir.y;
          a.vel->velocity.x += dir.x * dot * 0.5f;
          a.vel->velocity.y += dir.y * dot * 0.5f;
          b.vel->velocity.x -= dir.x * dot * 0.5f;
          b.vel->velocity.y -= dir.y * dot * 0.5f;
        }
      }
    }
  } else {
    float cell_size = sim.particle_size * 4.f;
    if (cell_size < 1.f) cell_size = 1.f;
    int cols = static_cast<int>(CANVAS_W / cell_size) + 3;
    int rows = static_cast<int>(CANVAS_H / cell_size) + 3;
    std::vector<std::vector<size_t>> grid(cols * rows);

    for (size_t i = 0; i < particles.size(); ++i) {
      float px = particles[i].pos->position.x;
      float py = particles[i].pos->position.y;
      if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
      int cx = static_cast<int>(px / cell_size) + 1;
      int cy = static_cast<int>(py / cell_size) + 1;
      if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
        grid[cy * cols + cx].push_back(i);
      }
    }

    for (size_t i = 0; i < particles.size(); ++i) {
      float px = particles[i].pos->position.x;
      float py = particles[i].pos->position.y;
      if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
      int cx = static_cast<int>(px / cell_size) + 1;
      int cy = static_cast<int>(py / cell_size) + 1;

      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;
        for (int dx = -1; dx <= 1; ++dx) {
          int nx = cx + dx;
          if (nx < 0 || nx >= cols) continue;

          for (size_t j : grid[ny * cols + nx]) {
            if (j <= i) continue;

            auto& a = particles[i];
            auto& b = particles[j];

            float dx_pos = b.pos->position.x - a.pos->position.x;
            float dy_pos = b.pos->position.y - a.pos->position.y;
            float dist_sq = dx_pos * dx_pos + dy_pos * dy_pos;
            if (dist_sq <= 0.f) continue;

            float dist = std::sqrt(dist_sq);
            float radius_sum = a.circ->radius + b.circ->radius;
            float dir_x = dx_pos / dist;
            float dir_y = dy_pos / dist;

            float repulse_dist = radius_sum * 3.f;
            if (dist < repulse_dist) {
              float repulse_strength = (repulse_dist - dist) / dist;
              a.vel->velocity.x -=
                dir_x * particle_repulsion * repulse_strength;
              a.vel->velocity.y -=
                dir_y * particle_repulsion * repulse_strength;
              b.vel->velocity.x +=
                dir_x * particle_repulsion * repulse_strength;
              b.vel->velocity.y +=
                dir_y * particle_repulsion * repulse_strength;
            }

            if (dist < radius_sum) {
              float overlap = radius_sum - dist;
              a.pos->position.x -= dir_x * overlap * 0.5f;
              a.pos->position.y -= dir_y * overlap * 0.5f;
              b.pos->position.x += dir_x * overlap * 0.5f;
              b.pos->position.y += dir_y * overlap * 0.5f;

              float dot = (b.vel->velocity.x - a.vel->velocity.x) * dir_x +
                          (b.vel->velocity.y - a.vel->velocity.y) * dir_y;
              a.vel->velocity.x += dir_x * dot * 0.5f;
              a.vel->velocity.y += dir_y * dot * 0.5f;
              b.vel->velocity.x -= dir_x * dot * 0.5f;
              b.vel->velocity.y -= dir_y * dot * 0.5f;
            }
          }
        }
      }
    }
  }

  // Path collision
  if (sim.user_path_points.size() >= 2) {
    ecs.group_view<components::CanvasComponent>(
      [&](Entity, components::CanvasComponent& canvas) {
        // Convert all points to world coordinates
        std::vector<Vector2> world_path;
        world_path.resize(sim.user_path_points.size());
        for (size_t i = 0; i < sim.user_path_points.size(); ++i) {
          world_path[i] =
            CanvasLocalToWorld(sim.user_path_points[i], canvas);
        }

        bool is_closed = false;
        if (world_path.size() >= 3) {
          float dx = world_path[0].x - world_path.back().x;
          float dy = world_path[0].y - world_path.back().y;
          if (sqrtf(dx * dx + dy * dy) < 20.f) is_closed = true;
        }

        ecs.group_view<components::PositionComponent,
                       components::VelocityComponent,
                       components::CircleComponent>(
          [&](Entity, components::PositionComponent& pos,
              components::VelocityComponent& vel,
              components::CircleComponent& c) {
            if (is_closed) {
              // Check if particle is inside the closed polygon using raycasting
              bool inside = false;
              for (size_t i = 0, j = world_path.size() - 1;
                   i < world_path.size(); j = i++) {
                if (((world_path[i].y > pos.position.y) !=
                     (world_path[j].y > pos.position.y)) &&
                    (pos.position.x < (world_path[j].x - world_path[i].x) *
                                          (pos.position.y - world_path[i].y) /
                                          (world_path[j].y - world_path[i].y) +
                                        world_path[i].x)) {
                  inside = !inside;
                }
              }

              if (inside) {
                // Find closest point on polygon boundary
                Vector2 closest_pt = world_path[0];
                float min_dist_sq = FLT_MAX;

                for (size_t i = 0; i < world_path.size(); ++i) {
                  Vector2 a = world_path[i];
                  Vector2 b = world_path[(i + 1) % world_path.size()];

                  Vector2 ab = {b.x - a.x, b.y - a.y};
                  Vector2 ap = {pos.position.x - a.x, pos.position.y - a.y};

                  float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
                  if (ab_len_sq <= 0.f) continue;

                  float t = (ap.x * ab.x + ap.y * ab.y) / ab_len_sq;
                  t = Clamp(t, 0.f, 1.f);

                  Vector2 closest = {a.x + t * ab.x, a.y + t * ab.y};
                  float dx = pos.position.x - closest.x;
                  float dy = pos.position.y - closest.y;
                  float dist_sq = dx * dx + dy * dy;

                  if (dist_sq < min_dist_sq) {
                    min_dist_sq = dist_sq;
                    closest_pt = closest;
                  }
                }

                // Push particle OUTSIDE the polygon
                float dx = pos.position.x - closest_pt.x;
                float dy = pos.position.y - closest_pt.y;
                float dist = sqrtf(min_dist_sq);
                float nx = dx / dist;
                float ny = dy / dist;

                // Gently push toward boundary
                float push = (c.radius + 2.f - dist) * 0.5f;
                pos.position.x += nx * push;
                pos.position.y += ny * push;

                // Gently reflect velocity
                float dot = vel.velocity.x * nx + vel.velocity.y * ny;
                vel.velocity.x -= dot * nx * 0.5f;
                vel.velocity.y -= dot * ny * 0.5f;
              }
            } else {
              // Line segment collision for open paths
              for (size_t i = 0; i < world_path.size() - 1; ++i) {
                Vector2 a = world_path[i];
                Vector2 b = world_path[i + 1];

                Vector2 ab = {b.x - a.x, b.y - a.y};
                Vector2 ap = {pos.position.x - a.x, pos.position.y - a.y};

                float ab_len_sq = ab.x * ab.x + ab.y * ab.y;
                if (ab_len_sq <= 0.f) continue;

                float t = (ap.x * ab.x + ap.y * ab.y) / ab_len_sq;
                t = Clamp(t, 0.f, 1.f);

                Vector2 closest = {a.x + t * ab.x, a.y + t * ab.y};

                float dx = pos.position.x - closest.x;
                float dy = pos.position.y - closest.y;
                float dist_sq = dx * dx + dy * dy;

                if (dist_sq < c.radius * c.radius && dist_sq > 0.f) {
                  float dist = sqrtf(dist_sq);
                  float overlap = c.radius - dist;
                  float nx = dx / dist;
                  float ny = dy / dist;

                  pos.position.x += nx * overlap;
                  pos.position.y += ny * overlap;

                  float dot = vel.velocity.x * nx + vel.velocity.y * ny;
                  vel.velocity.x -= dot * nx * 0.5f;
                  vel.velocity.y -= dot * ny * 0.5f;
                }
              }
            }
          });
      });
  }
}

/**
 * ============================================================================
 * Simulation
 * ============================================================================
 */
inline void ComputeParticleDensity(ECS& ecs) {
  auto& sim = entities::Simulation(ecs);

  int effective_threads = GetEffectiveThreads(particle_entities.size());
  if (!threads_initialized || effective_threads <= 1 ||
      particle_entities.size() < 256) {
    float h = sim.smoothing_radius;
    float h2 = h * h;
    float mass = sim.particle_size;

    for (size_t i = 0; i < particle_entities.size(); ++i) {
      Entity e = particle_entities[i];
      auto& c = ecs.get<components::CircleComponent>(e);

      Vector2 p = predicted_positions[i];
      GridCell cell = PositionToCell(p, h);

      float density = 0.f;

      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          GridCell neighbor{cell.x + dx, cell.y + dy};

          auto it = spatial_grid.find(neighbor);
          if (it == spatial_grid.end()) continue;

          for (size_t j : it->second) {
            Vector2 p2 = predicted_positions[j];

            float rx = p2.x - p.x;
            float ry = p2.y - p.y;

            float r2 = rx * rx + ry * ry;

            if (r2 <= h2) density += mass * Poly6Kernel(r2, h);
          }
        }
      }

      c.density = density;
    }
    return;
  }

  int n = static_cast<int>(particle_entities.size());
  temp_densities.resize(n);

  ParallelDensityTask seed{0, 0, sim.smoothing_radius, sim.particle_size};
  RunParallelChunks(effective_threads, n, seed, ComputeDensityRange);

  for (int i = 0; i < n; ++i) {
    auto& c = ecs_ptr->get<components::CircleComponent>(particle_entities[i]);
    c.density = temp_densities[i];
  }
}

/**
 * ============================================================================
 * Simulation
 * ============================================================================
 */
inline void SimulateFluid(ECS& ecs, float dt, bool force_simulate = false) {
  auto& sim = entities::Simulation(ecs);

  if (sim.particle_cache_dirty || !particle_entities_cached) {
    CacheParticleEntities(ecs);
    sim.particle_cache_dirty = false;
  }

  if (sim.is_paused && !force_simulate) return;

  UpdateKernelCache(sim.gravity);

  float effective_dt = dt * sim.sim_speed;

  PredictPositions(ecs, effective_dt);
  ComputeParticleDensity(ecs);
  ComputeParticlePressure(ecs);
  ComputeParticlePressureForce(ecs, effective_dt);

  size_t n = particle_entities.size();
  for (size_t i = 0; i < n; ++i) {
    circ_cache[i]->radius = sim.particle_size;
  }

  for (size_t i = 0; i < n; ++i) {
    pos_cache[i]->position.x += vel_cache[i]->velocity.x * effective_dt;
    pos_cache[i]->position.y += vel_cache[i]->velocity.y * effective_dt;
  }

  float damp = sim.velocity_damping;
  for (size_t i = 0; i < n; ++i) {
    vel_cache[i]->velocity.x *= damp;
    vel_cache[i]->velocity.y *= damp;
  }

  ResolveCollisions(ecs);
}

}  // namespace motrix::engine::systems
