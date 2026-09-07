// engine/systems/physics.h
#pragma once

#include <cfloat>
#include <cmath>
#include <vector>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/canvas.h"
#include "../components/physics.h"
#include "../ecs/ecs.h"
#include "../systems/canvas.h"
#include "../systems/sph_kernels.h"
#include "../systems/thread_pool.h"
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

// Creates the persistent worker gang backing every parallel region. Safe to
// call more than once: a gang of the same size is left untouched, a different
// size tears down and respawns.
inline void InitThreads(int threads) {
  int count = threads > 0 ? threads : 1;
  num_threads = count;

  if (ThreadPool::Instance().Size() != static_cast<size_t>(count)) {
    ThreadPool::Instance().Start(static_cast<size_t>(count));
  }

  threads_initialized = true;
  logger::info("[APP] Created {} threads for simulation", count);
}

inline void ShutdownThreads() {
  ThreadPool::Instance().Stop();
  threads_initialized = false;
}

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
                              Task& seed, Fn fn) {
  ThreadPool::Instance().Run(effective, particle_count, seed, fn);
}

/**
 * ============================================================================
 * Runtime Buffers
 * ============================================================================
 *
 * Per-world scratch state for the physics pipeline (particle caches, spatial
 * grid, per-particle force/density arrays, kernel cache). Lives as a component
 * attached to the simulation root so each world owns its own buffers and the
 * benchmark no longer needs a cross-world reset for them.
 *
 * The buffer vectors hold pointers into the ECS's component storage; they are
 * only used within a single SimulateFluid step (no entity add/remove happens
 * mid-step), so the addresses stay valid for the duration of the step.
 */
struct PhysicsBuffers {
  static constexpr std::string_view Name = "PhysicsBuffers";

  std::vector<Entity> particle_entities;
  std::vector<Vector2> predicted_positions;
  std::vector<components::PositionComponent*> pos_cache;
  std::vector<components::VelocityComponent*> vel_cache;
  std::vector<components::CircleComponent*> circ_cache;
  bool particle_entities_cached = false;

  // Flat uniform grid (dense, indexed by cy*cols+cx). Rebuilt every step;
  // vector capacities are reused across steps so no per-frame allocation.
  int grid_cols = 0;
  int grid_rows = 0;
  float grid_cell_size = 0.f;
  std::vector<std::vector<size_t>> grid_cells;

  // Uniform grid used by the particle-particle collision pass (finer cell
  // size than the SPH grid). Persistent for the same reason.
  int collision_cols = 0;
  int collision_rows = 0;
  float collision_cell_size = 0.f;
  std::vector<std::vector<size_t>> collision_grid;

  std::vector<float> densities;
  std::vector<float> pressures;
  std::vector<Vector2> velocities;
  std::vector<float> pressure_forces_data;
  std::vector<float> viscosity_forces_data;
  std::vector<float> cohesion_forces_data;

  float cached_gravity_accel = 0.f;
  bool kernel_cache_valid = false;
};

inline PhysicsBuffers& Physics(ECS& ecs) {
  Entity root = motrix::entities::simulation_root;
  if (!ecs.has<PhysicsBuffers>(root)) ecs.add<PhysicsBuffers>(root);
  return ecs.get<PhysicsBuffers>(root);
}

inline void CacheParticleEntities(ECS& ecs, PhysicsBuffers& pb) {
  pb.particle_entities.clear();
  ecs.group_view<components::PositionComponent>(
    [&](Entity e, components::PositionComponent&) {
      pb.particle_entities.push_back(e);
    });

  size_t n = pb.particle_entities.size();
  pb.predicted_positions.resize(n);
  pb.pos_cache.resize(n);
  pb.vel_cache.resize(n);
  pb.circ_cache.resize(n);

  for (size_t i = 0; i < n; ++i) {
    pb.pos_cache[i] =
      &ecs.get<components::PositionComponent>(pb.particle_entities[i]);
    pb.vel_cache[i] =
      &ecs.get<components::VelocityComponent>(pb.particle_entities[i]);
    pb.circ_cache[i] = &ecs.get<components::CircleComponent>(pb.particle_entities[i]);
  }

  pb.particle_entities_cached = true;
  logger::info("[PHYSICS] Cached {} particles", pb.particle_entities.size());
}

inline int GridCellCountX(float h) {
  return static_cast<int>(CANVAS_W / h) + 3;
}

inline int GridCellCountY(float h) {
  return static_cast<int>(CANVAS_H / h) + 3;
}

// Maps a world position to clamped flat-grid cell coordinates. The +1 offset
// centers the particle domain so the one-cell margin keeps the 3x3 sweep in
// bounds even when particles drift slightly past the canvas edge.
inline void PositionToFlatCell(Vector2 p, float h, int cols, int rows,
                               int& cx, int& cy) {
  cx = std::clamp(static_cast<int>(std::floor(p.x / h)) + 1, 0, cols - 1);
  cy = std::clamp(static_cast<int>(std::floor(p.y / h)) + 1, 0, rows - 1);
}

inline void BuildSpatialGrid(PhysicsBuffers& pb, float h) {
  if (h <= 0.f) return;

  int cols = GridCellCountX(h);
  int rows = GridCellCountY(h);

  if (pb.grid_cols != cols || pb.grid_rows != rows ||
      pb.grid_cell_size != h) {
    pb.grid_cells.assign(static_cast<size_t>(cols) * rows, {});
    pb.grid_cols = cols;
    pb.grid_rows = rows;
    pb.grid_cell_size = h;
  }

  for (auto& cell : pb.grid_cells) cell.clear();

  for (size_t i = 0; i < pb.predicted_positions.size(); ++i) {
    int cx;
    int cy;
    PositionToFlatCell(pb.predicted_positions[i], h, cols, rows, cx, cy);
    pb.grid_cells[cy * cols + cx].push_back(i);
  }
}

inline void UpdateKernelCache(PhysicsBuffers& pb, float gravity) {
  if (pb.kernel_cache_valid && pb.cached_gravity_accel == gravity * 10.f)
    return;
  pb.cached_gravity_accel = gravity * 10.f;
  pb.kernel_cache_valid = true;
}

/**
 * ============================================================================
 * Prediction
 * ============================================================================
 */

inline void PredictPositions(ECS& ecs, PhysicsBuffers& pb, float dt) {
  if (!pb.particle_entities_cached) {
    CacheParticleEntities(ecs, pb);
  }

  auto& sim = entities::Simulation(ecs);

  float gravity = pb.cached_gravity_accel;
  size_t n = pb.particle_entities.size();
  pb.velocities.resize(n);

  struct PredictTask {
    int start;
    int end;
    float grav;
    float dt;
    PhysicsBuffers* pb;
  };
  PredictTask seed{0, static_cast<int>(n), gravity, dt, &pb};
  RunParallelChunks(GetEffectiveThreads(n), n, seed, [](void* arg) -> void* {
    auto* tk = static_cast<PredictTask*>(arg);
    for (int i = tk->start; i < tk->end; ++i) {
      tk->pb->vel_cache[i]->velocity.y += tk->grav * tk->dt;
      tk->pb->predicted_positions[i] = {
        tk->pb->pos_cache[i]->position.x +
          tk->pb->vel_cache[i]->velocity.x * tk->dt,
        tk->pb->pos_cache[i]->position.y +
          tk->pb->vel_cache[i]->velocity.y * tk->dt};
      tk->pb->velocities[i] = tk->pb->vel_cache[i]->velocity;
    }
    return nullptr;
  });

  BuildSpatialGrid(pb, sim.smoothing_radius);
}

/**
 * ============================================================================
 * Density
 * ============================================================================
 */

struct DensityTask {
  int start;
  int end;
  float h;
  float mass;
  float target_density;
  float pressure_multiplier;
  PhysicsBuffers* pb;
};

inline void* ComputeDensityRange(void* arg) {
  auto* task = static_cast<DensityTask*>(arg);
  const int start = task->start;
  const int end = task->end;

  const float h = task->h;
  const float h2 = h * h;
  const float mass = task->mass;
  const float target_density = task->target_density;
  const float pressure_multiplier = task->pressure_multiplier;
  PhysicsBuffers& pb = *task->pb;

  const int cols = pb.grid_cols;
  const int rows = pb.grid_rows;

  for (int i = start; i < end && i < static_cast<int>(pb.particle_entities.size());
       ++i) {
    Vector2 p = pb.predicted_positions[i];

    int cx;
    int cy;
    PositionToFlatCell(p, h, cols, rows, cx, cy);

    float density = 0.f;

    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      if (nx < 0 || nx >= cols) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;

        for (size_t j : pb.grid_cells[ny * cols + nx]) {
          Vector2 p2 = pb.predicted_positions[j];

          float rx = p2.x - p.x;
          float ry = p2.y - p.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= h2) density += mass * Poly6Kernel(r2, h);
        }
      }
    }

    pb.circ_cache[i]->density = density;
    pb.densities[i] = density;
    pb.pressures[i] = (density - target_density) * pressure_multiplier;
  }

  return nullptr;
}

/**
 * ============================================================================
 * Pressure Force
 * ============================================================================
 */

struct ForceTask {
  int start;
  int end;
  float h;
  float mass;
  PhysicsBuffers* pb;
};

inline void* ComputePressureForceRange(void* arg) {
  auto* task = static_cast<ForceTask*>(arg);
  const int start = task->start;
  const int end = task->end;

  const float h = task->h;
  const float h2 = h * h;
  const float mass = task->mass;
  PhysicsBuffers& pb = *task->pb;

  const int cols = pb.grid_cols;
  const int rows = pb.grid_rows;

  for (int i = start; i < end && i < static_cast<int>(pb.particle_entities.size());
       ++i) {
    Vector2 p1 = pb.predicted_positions[i];

    int cx;
    int cy;
    PositionToFlatCell(p1, h, cols, rows, cx, cy);

    float d1 = pb.densities[i];
    float p1_pressure = pb.pressures[i];
    Vector2 v1 = pb.velocities[i];

    Vector2 pressure_force{0.f, 0.f};
    Vector2 viscosity_force{0.f, 0.f};
    Vector2 cohesion_force{0.f, 0.f};

    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      if (nx < 0 || nx >= cols) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;

        for (size_t j : pb.grid_cells[ny * cols + nx]) {
          if (i == j) continue;

          Vector2 p2 = pb.predicted_positions[j];

          float rx = p1.x - p2.x;
          float ry = p1.y - p2.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= 0.f || r2 > h2) continue;

          float r = sqrtf(r2);

          Vector2 dir{rx / r, ry / r};

          float grad = SpikyKernelGradient(r, h);

          float d2 = pb.densities[j];
          float p2_pressure = pb.pressures[j];
          Vector2 v2 = pb.velocities[j];

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

    pb.pressure_forces_data[i * 2] = pressure_force.x;
    pb.pressure_forces_data[i * 2 + 1] = pressure_force.y;
    pb.viscosity_forces_data[i * 2] = viscosity_force.x;
    pb.viscosity_forces_data[i * 2 + 1] = viscosity_force.y;
    pb.cohesion_forces_data[i * 2] = cohesion_force.x;
    pb.cohesion_forces_data[i * 2 + 1] = cohesion_force.y;
  }

  return nullptr;
}

struct ApplyTask {
  int start;
  int end;
  float dt;
  float viscosity;
  float surface_tension;
  float mass;
  float damp;
  float particle_size;
  PhysicsBuffers* pb;
};

// Fused integration pass: applies pressure/viscosity/cohesion to velocity,
// advances positions, resets the radius and applies velocity damping — one
// loop instead of the previous three independent full-array walks.
inline void* ApplyFluidForcesRange(void* arg) {
  auto* task = static_cast<ApplyTask*>(arg);
  const float dt = task->dt;
  const float viscosity = task->viscosity;
  const float surface_tension = task->surface_tension;
  const float mass = task->mass;
  const float damp = task->damp;
  const float particle_size = task->particle_size;
  PhysicsBuffers& pb = *task->pb;

  for (int i = task->start;
       i < task->end && i < static_cast<int>(pb.particle_entities.size()); ++i) {
    auto* vel = pb.vel_cache[i];
    auto* pos = pb.pos_cache[i];
    auto* circ = pb.circ_cache[i];

    circ->radius = particle_size;

    vel->velocity.x += pb.pressure_forces_data[i * 2] * dt;
    vel->velocity.y += pb.pressure_forces_data[i * 2 + 1] * dt;

    vel->velocity.x += pb.viscosity_forces_data[i * 2] * viscosity * 50.f;
    vel->velocity.y += pb.viscosity_forces_data[i * 2 + 1] * viscosity * 50.f;

    vel->velocity.x +=
      pb.cohesion_forces_data[i * 2] * surface_tension * mass;
    vel->velocity.y +=
      pb.cohesion_forces_data[i * 2 + 1] * surface_tension * mass;

    pos->position.x += vel->velocity.x * dt;
    pos->position.y += vel->velocity.y * dt;

    vel->velocity.x *= damp;
    vel->velocity.y *= damp;
  }

  return nullptr;
}

inline void ComputeParticlePressureForce(ECS& ecs, float dt, float damp) {
  auto& sim = entities::Simulation(ecs);
  PhysicsBuffers& pb = Physics(ecs);

  int n = static_cast<int>(pb.particle_entities.size());
  float mass = sim.particle_size;
  pb.pressure_forces_data.resize(static_cast<size_t>(n) * 2);
  pb.viscosity_forces_data.resize(static_cast<size_t>(n) * 2);
  pb.cohesion_forces_data.resize(static_cast<size_t>(n) * 2);

  ForceTask force_seed{0, n, sim.smoothing_radius, mass, &pb};
  RunParallelChunks(GetEffectiveThreads(n), n, force_seed,
                    ComputePressureForceRange);

  ApplyTask apply_seed{0, n, dt, sim.viscosity, sim.surface_tension, mass,
                       damp, sim.particle_size, &pb};
  RunParallelChunks(GetEffectiveThreads(n), n, apply_seed, ApplyFluidForcesRange);
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
  PhysicsBuffers& pb = Physics(ecs);

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

    if (pb.collision_cols != cols || pb.collision_rows != rows ||
        pb.collision_cell_size != cell_size) {
      pb.collision_grid.assign(static_cast<size_t>(cols) * rows, {});
      pb.collision_cols = cols;
      pb.collision_rows = rows;
      pb.collision_cell_size = cell_size;
    }
    for (auto& cell : pb.collision_grid) cell.clear();

    for (size_t i = 0; i < particles.size(); ++i) {
      float px = particles[i].pos->position.x;
      float py = particles[i].pos->position.y;
      if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
      int cx = static_cast<int>(px / cell_size) + 1;
      int cy = static_cast<int>(py / cell_size) + 1;
      if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
        pb.collision_grid[cy * cols + cx].push_back(i);
      }
    }

    for (size_t i = 0; i < particles.size(); ++i) {
      float px = particles[i].pos->position.x;
      float py = particles[i].pos->position.y;
      if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
      int cx = static_cast<int>(px / cell_size) + 1;
      int cy = static_cast<int>(py / cell_size) + 1;

      auto& a = particles[i];
      const float a_radius = a.circ->radius;

      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;
        for (int dx = -1; dx <= 1; ++dx) {
          int nx = cx + dx;
          if (nx < 0 || nx >= cols) continue;

          for (size_t j : pb.collision_grid[ny * cols + nx]) {
            if (j <= i) continue;

            auto& b = particles[j];

            float dx_pos = b.pos->position.x - px;
            float dy_pos = b.pos->position.y - py;
            float dist_sq = dx_pos * dx_pos + dy_pos * dy_pos;
            if (dist_sq <= 0.f) continue;

            float radius_sum = a_radius + b.circ->radius;
            float repulse_dist = radius_sum * 3.f;
            if (dist_sq >= repulse_dist * repulse_dist) continue;

            float dist = std::sqrt(dist_sq);
            float inv_dist = 1.f / dist;
            float dir_x = dx_pos * inv_dist;
            float dir_y = dy_pos * inv_dist;

            if (dist < repulse_dist) {
              float repulse_strength = (repulse_dist - dist) * inv_dist;
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
  PhysicsBuffers& pb = Physics(ecs);

  int n = static_cast<int>(pb.particle_entities.size());
  pb.densities.resize(n);
  pb.pressures.resize(n);

  DensityTask seed{0, n, sim.smoothing_radius, sim.particle_size,
                   sim.target_density, sim.pressure_multiplier, &pb};
  RunParallelChunks(GetEffectiveThreads(n), n, seed, ComputeDensityRange);
}

/**
 * ============================================================================
 * Simulation
 * ============================================================================
 */
inline void SimulateFluid(ECS& ecs, float dt, bool force_simulate = false) {
  auto& sim = entities::Simulation(ecs);
  PhysicsBuffers& pb = Physics(ecs);

  if (sim.particle_cache_dirty || !pb.particle_entities_cached) {
    CacheParticleEntities(ecs, pb);
    sim.particle_cache_dirty = false;
  }

  if (sim.is_paused && !force_simulate) return;

  UpdateKernelCache(pb, sim.gravity);

  float effective_dt = dt * sim.sim_speed;

  PredictPositions(ecs, pb, effective_dt);
  ComputeParticleDensity(ecs);
  ComputeParticlePressureForce(ecs, effective_dt, sim.velocity_damping);

  ResolveCollisions(ecs);
}

}  // namespace motrix::engine::systems
