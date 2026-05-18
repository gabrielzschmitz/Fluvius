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
#include "../systems/ui_helpers.h"
#include "entities/fluid.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Thread Pool
 * ============================================================================
 */
inline int num_threads = 1;
inline pthread_t* thread_pool = nullptr;
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
 * Parallel task system using pthreads
 * ============================================================================
 */
struct ParallelTask {
  int start;
  int end;
  int thread_id;
};

inline pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;
inline int current_task_idx = 0;
inline int total_tasks = 0;
inline void* (*task_func)(void*) = nullptr;
inline void* task_user_data = nullptr;
inline pthread_mutex_t sim_mutex = PTHREAD_MUTEX_INITIALIZER;

inline void* ThreadWorker(void* arg) {
  int tid = *(int*)arg;

  while (true) {
    pthread_mutex_lock(&task_mutex);
    if (current_task_idx >= total_tasks) {
      pthread_mutex_unlock(&task_mutex);
      break;
    }
    int start = current_task_idx;
    current_task_idx += std::min(100, total_tasks - current_task_idx);
    pthread_mutex_unlock(&task_mutex);

    for (int i = start; i < std::min(start + 100, total_tasks) &&
                        i < start + (total_tasks - start);
         ++i) {
      if (task_func) {
        ParallelTask task;
        task.start = i;
        task.end = std::min(i + 1, total_tasks);
        task.thread_id = tid;
        task_func(&task);
      }
    }
  }
  return nullptr;
}

inline void RunParallel(int total, void* user_data, void* (*func)(void*)) {
  if (!threads_initialized || num_threads <= 1 || total < 500) {
    task_user_data = user_data;
    task_func = func;
    current_task_idx = 0;
    total_tasks = total;
    ParallelTask single_task{0, total, 0};
    if (func) func(&single_task);
    return;
  }

  task_user_data = user_data;
  task_func = func;
  current_task_idx = 0;
  total_tasks = total;

  std::vector<pthread_t> threads(num_threads);
  std::vector<int> thread_ids(num_threads);

  for (int i = 0; i < num_threads; ++i) {
    thread_ids[i] = i;
    pthread_create(&threads[i], nullptr, ThreadWorker, &thread_ids[i]);
  }

  for (int i = 0; i < num_threads; ++i) {
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
 * Spatial Hash
 * ============================================================================
 */

struct GridCell {
  int x;
  int y;

  bool operator==(const GridCell& other) const {
    return x == other.x && y == other.y;
  }
};

struct GridCellHash {
  size_t operator()(const GridCell& c) const {
    return std::hash<int>()(c.x * 73856093) ^ std::hash<int>()(c.y * 19349663);
  }
};

inline std::unordered_map<GridCell, std::vector<size_t>, GridCellHash>
  spatial_grid;

inline GridCell PositionToCell(Vector2 p, float cell_size) {
  return {static_cast<int>(std::floor(p.x / cell_size)),
          static_cast<int>(std::floor(p.y / cell_size))};
}

inline void BuildSpatialGrid() {
  spatial_grid.clear();
  float h = entities::smoothing_radius;

  for (size_t i = 0; i < predicted_positions.size(); ++i) {
    GridCell cell = PositionToCell(predicted_positions[i], h);
    spatial_grid[cell].push_back(i);
  }
}

/**
 * ============================================================================
 * Kernels
 * ============================================================================
 */
inline float cached_h = 0.f;
inline float cached_h2 = 0.f;
inline float cached_viscosity = 0.f;
inline float cached_surface_tension = 0.f;
inline float cached_mass = 0.f;
inline float cached_gravity_accel = 0.f;
inline bool kernel_cache_valid = false;

inline void UpdateKernelCache() {
  if (kernel_cache_valid && cached_h == entities::smoothing_radius
      && cached_gravity_accel == entities::gravity * 10.f) return;
  cached_h = entities::smoothing_radius;
  cached_h2 = cached_h * cached_h;
  cached_gravity_accel = entities::gravity * 10.f;
  cached_viscosity = entities::viscosity;
  cached_surface_tension = entities::surface_tension;
  cached_mass = entities::particle_size;
  kernel_cache_valid = true;
}

inline float Poly6Kernel(float r2, float h) {
  float h2 = h * h;
  if (r2 >= h2) return 0.f;
  float diff = h2 - r2;
  float h9 = h * h * h * h * h * h * h * h * h;
  return 315.f / (64.f * PI * h9) * diff * diff * diff;
}

inline float SpikyKernelGradient(float r, float h) {
  if (r <= 0.f || r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  float v = h - r;
  return -15.f / (PI * h5) * v * v;
}

inline float ViscosityKernel(float r, float h) {
  if (r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  return 15.f / (2.f * PI * h5) * (h - r);
}

inline float FluidFieldKernel(float dist_sq, float radius_sq) {
  if (dist_sq >= radius_sq) return 0.f;

  float x = 1.f - (dist_sq / radius_sq);

  return x * x;
}

inline Vector2 InterpolateEdge(Vector2 a, Vector2 b, float va, float vb,
                               float threshold) {
  if (fabsf(vb - va) < 0.0001f) return a;

  float t = (threshold - va) / (vb - va);

  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

/**
 * ============================================================================
 * Pressure
 * ============================================================================
 */

inline float ConvertDensityToPressure(float density) {
  return (density - entities::target_density) * entities::pressure_multiplier;
}

/**
 * ============================================================================
 * Pressure Color
 * ============================================================================
 */

inline Color PressureToColor(float pressure) {
  float max_pressure = entities::target_density * entities::pressure_multiplier;

  if (max_pressure <= 0.f) return entities::pressure_mid_color;

  float scale = Clamp(pressure / max_pressure, -1.f, 1.f);

  auto lerp = [](unsigned char a, unsigned char b, float t) {
    return static_cast<unsigned char>(a + (b - a) * t);
  };

  if (scale < 0.f) {
    float t = scale + 1.f;

    return {
      lerp(entities::pressure_low_color.r, entities::pressure_mid_color.r, t),
      lerp(entities::pressure_low_color.g, entities::pressure_mid_color.g, t),
      lerp(entities::pressure_low_color.b, entities::pressure_mid_color.b, t),
      130};
  }

  return {lerp(entities::pressure_mid_color.r, entities::pressure_high_color.r,
               scale),
          lerp(entities::pressure_mid_color.g, entities::pressure_high_color.g,
               scale),
          lerp(entities::pressure_mid_color.b, entities::pressure_high_color.b,
               scale),
          130};
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

  float gravity = cached_gravity_accel;
  size_t n = particle_entities.size();
  int effective = GetEffectiveThreads(n);

  if (effective > 1 && n >= 256) {
    struct PredictTask {
      int start;
      int end;
      float grav;
      float dt;
    };
    std::vector<pthread_t> threads(effective);
    std::vector<PredictTask> tasks(effective);

    for (int ti = 0; ti < effective; ++ti) {
      tasks[ti].start = ti * n / effective;
      tasks[ti].end = (ti + 1) * n / effective;
      tasks[ti].grav = gravity;
      tasks[ti].dt = dt;
      pthread_create(
        &threads[ti], nullptr,
        [](void* arg) -> void* {
          auto* tk = (PredictTask*)arg;
          for (int i = tk->start; i < tk->end; ++i) {
            vel_cache[i]->velocity.y += tk->grav * tk->dt;
            predicted_positions[i] = {
              pos_cache[i]->position.x + vel_cache[i]->velocity.x * tk->dt,
              pos_cache[i]->position.y + vel_cache[i]->velocity.y * tk->dt};
          }
          return nullptr;
        },
        &tasks[ti]);
    }
    for (int ti = 0; ti < effective; ++ti) pthread_join(threads[ti], nullptr);
  } else {
    for (size_t i = 0; i < n; ++i) {
      vel_cache[i]->velocity.y += gravity * dt;
      predicted_positions[i] = {
        pos_cache[i]->position.x + vel_cache[i]->velocity.x * dt,
        pos_cache[i]->position.y + vel_cache[i]->velocity.y * dt};
    }
  }

  BuildSpatialGrid();
}

/**
 * ============================================================================
 * Density
 * ============================================================================
 */

struct ParallelDensityTask {
  int start;
  int end;
};

inline std::vector<float> temp_densities;

inline void* ComputeDensityRange(void* arg) {
  auto* task = static_cast<ParallelDensityTask*>(arg);
  int start = task->start;
  int end = task->end;

  float h = entities::smoothing_radius;
  float h2 = h * h;
  float mass = entities::particle_size;

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
  ecs.group_view<components::CircleComponent>(
    [&](Entity, components::CircleComponent& c) {
      c.pressure = ConvertDensityToPressure(c.density);
    });
}

/**
 * ============================================================================
 * Pressure Force
 * ============================================================================
 */

inline float CohesionKernel(float r, float h) {
  if (r >= h * 0.5f) return 0.f;
  float q = r / (h * 0.5f);
  return (1.f - q) * (1.f - q);
}

struct ParallelForceTask {
  int start;
  int end;
  float dt;
};

inline std::vector<Vector2> pressure_forces;
inline std::vector<Vector2> viscosity_forces;
inline std::vector<Vector2> cohesion_forces;
inline std::vector<float> densities;
inline std::vector<float> pressures;
inline std::vector<float> mass_densities;
inline std::vector<Vector2> velocities;

inline void* ComputePressureForceRange(void* arg) {
  auto* task = static_cast<ParallelForceTask*>(arg);
  int start = task->start;
  int end = task->end;
  float dt = task->dt;

  float h = entities::smoothing_radius;
  float h2 = h * h;
  float mass = entities::particle_size;
  float viscosity = entities::viscosity;
  float surface_tension = entities::surface_tension;

  for (int i = start; i < end && i < static_cast<int>(particle_entities.size());
       ++i) {
    Vector2 p1 = predicted_positions[i];
    GridCell cell = PositionToCell(p1, h);

    float d1 = densities[i];
    float p1_pressure = pressures[i];
    float md1 = mass_densities[i];
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
          float md2 = mass_densities[j];
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
  int effective_threads = GetEffectiveThreads(particle_entities.size());
  if (!threads_initialized || effective_threads <= 1 ||
      particle_entities.size() < 256) {
    float h = entities::smoothing_radius;
    float h2 = h * h;
    float mass = entities::particle_size;
    float viscosity = entities::viscosity;
    float surface_tension = entities::surface_tension;

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
  mass_densities.resize(n);
  velocities.resize(n);

  for (int i = 0; i < n; ++i) {
    auto& c = ecs_ptr->get<components::CircleComponent>(particle_entities[i]);
    auto& v = ecs_ptr->get<components::VelocityComponent>(particle_entities[i]);
    densities[i] = c.density;
    pressures[i] = c.pressure;
    mass_densities[i] = c.density * c.density;
    velocities[i] = v.velocity;
  }

  int chunk_size = n / effective_threads;
  if (chunk_size < 64) chunk_size = 64;

  std::vector<pthread_t> threads(effective_threads);
  std::vector<ParallelForceTask> tasks(effective_threads);

  for (int i = 0; i < effective_threads; ++i) {
    tasks[i].start = i * chunk_size;
    tasks[i].end = std::min(tasks[i].start + chunk_size, n);
    tasks[i].dt = dt;
    pthread_create(&threads[i], nullptr, ComputePressureForceRange, &tasks[i]);
  }

  for (int i = 0; i < effective_threads; ++i) {
    pthread_join(threads[i], nullptr);
  }

  float viscosity = entities::viscosity;
  float surface_tension = entities::surface_tension;
  float mass = entities::particle_size;

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

inline bool IsMouseOverCanvas(ECS& ecs, Vector2 mouse_world) {
  bool over_canvas = false;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Vector2 local_mouse = WorldToCanvasLocal(mouse_world, canvas);

      bool touching_left =
        local_mouse.x >= -canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= -canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_right =
        local_mouse.x >= canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_top =
        local_mouse.y >= -canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= -canvas.half_extents.y + canvas.edge_tolerance;
      bool touching_bottom =
        local_mouse.y >= canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= canvas.half_extents.y + canvas.edge_tolerance;

      if (touching_left || touching_right || touching_top || touching_bottom) {
        over_canvas = true;
      }
    });
  return over_canvas;
}

inline void UpdateSelectionInput(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  if (!entities::selection_active) return;

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  if (UIConsumesMouse(ecs, mouse_screen) ||
      IsMouseOverAnyWindow(ecs, mouse_screen))
    return;

  if (IsMouseOverCanvas(ecs, mouse_world)) {
    entities::selection_locked = false;
    entities::selection_density = 0.f;
    return;
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    entities::selection_center = mouse_world;
    entities::selection_locked = true;
  }

  if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
    entities::selection_locked = false;
    entities::selection_density = 0.f;
  }
}

inline bool IsMouseOnCanvas(ECS& ecs, Vector2 mouse_world) {
  bool on_canvas = false;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Vector2 local_mouse = WorldToCanvasLocal(mouse_world, canvas);
      if (local_mouse.x >= -canvas.half_extents.x &&
          local_mouse.x <= canvas.half_extents.x &&
          local_mouse.y >= -canvas.half_extents.y &&
          local_mouse.y <= canvas.half_extents.y) {
        on_canvas = true;
      }
    });
  return on_canvas;
}

inline void UpdatePathInput(ECS& ecs,
                            const engine::components::CameraComponent& cam) {
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
    entities::is_drawing_path = !entities::is_drawing_path;
    if (entities::is_drawing_path) {
      entities::user_path_points.clear();
      if (IsMouseOnCanvas(ecs, mouse_world)) {
        // Store in canvas-local coordinates
        ecs.group_view<components::CanvasComponent>(
          [&](Entity, components::CanvasComponent& canvas) {
            entities::user_path_points.push_back(
              WorldToCanvasLocal(mouse_world, canvas));
          });
      }
    }
  }
  draw_key_was_down = draw_key_down;

  // Continue drawing while in drawing mode and on canvas
  if (entities::is_drawing_path && IsMouseOnCanvas(ecs, mouse_world)) {
    if (!entities::user_path_points.empty()) {
      // Convert last stored point (canvas-local) back to world for distance check
      Vector2 last_world = mouse_world;
      ecs.group_view<components::CanvasComponent>(
        [&](Entity, components::CanvasComponent& canvas) {
          last_world =
            CanvasLocalToWorld(entities::user_path_points.back(), canvas);
        });

      float dx = mouse_world.x - last_world.x;
      float dy = mouse_world.y - last_world.y;
      float dist = sqrtf(dx * dx + dy * dy);

      if (dist >= entities::path_point_spacing) {
        // Store in canvas-local coordinates
        ecs.group_view<components::CanvasComponent>(
          [&](Entity, components::CanvasComponent& canvas) {
            entities::user_path_points.push_back(
              WorldToCanvasLocal(mouse_world, canvas));
          });
      }
    } else {
      ecs.group_view<components::CanvasComponent>(
        [&](Entity, components::CanvasComponent& canvas) {
          entities::user_path_points.push_back(
            WorldToCanvasLocal(mouse_world, canvas));
        });
    }
  }
}

inline void UpdateSelectionDensity(ECS& ecs) {
  if (!entities::selection_locked) return;

  float nearest = FLT_MAX;

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::CircleComponent& c) {
      float dx = pos.position.x - entities::selection_center.x;
      float dy = pos.position.y - entities::selection_center.y;

      float d = dx * dx + dy * dy;

      if (d < nearest) {
        nearest = d;
        entities::selection_density = c.density;
        entities::selected_particle = e;
      }
    });
}

/**
 * ============================================================================
 * Rendering
 * ============================================================================
 */

inline void RenderArrow(Vector2 center, Vector2 vector, float radius,
                        Color color, float scale = 1.0f) {
  float speed = Vector2Length(vector);

  if (speed <= 0.001f) return;

  Vector2 dir = Vector2Normalize(vector);

  Vector2 start = {center.x + dir.x * radius, center.y + dir.y * radius};

  float arrow_length = radius + speed * scale;

  Vector2 end = {start.x + dir.x * arrow_length,
                 start.y + dir.y * arrow_length};

  float head_size = radius * 0.8f;
  float thickness = radius * 0.5f;

  DrawLineEx(start, end, thickness, color);

  Vector2 left = {end.x - dir.x * head_size + dir.y * head_size * 0.5f,
                  end.y - dir.y * head_size - dir.x * head_size * 0.5f};

  Vector2 right = {end.x - dir.x * head_size - dir.y * head_size * 0.5f,
                   end.y - dir.y * head_size + dir.x * head_size * 0.5f};

  DrawLineEx(end, left, thickness, color);
  DrawLineEx(end, right, thickness, color);
  DrawCircleV(end, thickness * 0.5f, color);
}

inline Color VelocityToColor(const Vector2& velocity) {
  const float speed = Vector2Length(velocity);
  constexpr float max_speed = 80.f;

  float speed_ratio = Clamp(speed / max_speed, 0.f, 1.f);

  auto LerpChannel = [](unsigned char a, unsigned char b, float factor) {
    return static_cast<unsigned char>(a + (b - a) * factor);
  };

  auto LerpColor = [&](Color c1, Color c2, float factor) {
    return Color{LerpChannel(c1.r, c2.r, factor),
                 LerpChannel(c1.g, c2.g, factor),
                 LerpChannel(c1.b, c2.b, factor), 255};
  };

  if (speed_ratio < 0.33f) {
    float t = speed_ratio / 0.33f;
    return LerpColor(entities::particle_low_color,
                     entities::particle_mid_low_color, t);
  } else if (speed_ratio < 0.66f) {
    float t = (speed_ratio - 0.33f) / 0.33f;
    return LerpColor(entities::particle_mid_low_color,
                     entities::particle_mid_high_color, t);
  } else {
    float t = (speed_ratio - 0.66f) / 0.34f;
    return LerpColor(entities::particle_mid_high_color,
                     entities::particle_high_color, t);
  }
}

inline Color SpeedToColor(float speed) {
  constexpr float max_speed = 80.f;
  float speed_ratio = Clamp(speed / max_speed, 0.f, 1.f);

  auto LerpChannel = [](unsigned char a, unsigned char b, float factor) {
    return static_cast<unsigned char>(a + (b - a) * factor);
  };

  auto BlendColor = [&](Color c1, Color c2, float factor) {
    return Color{LerpChannel(c1.r, c2.r, factor),
                 LerpChannel(c1.g, c2.g, factor),
                 LerpChannel(c1.b, c2.b, factor), 255};
  };

  if (speed_ratio < 0.33f) {
    float t = speed_ratio / 0.33f;
    return BlendColor(entities::particle_low_color,
                      entities::particle_mid_low_color, t);
  } else if (speed_ratio < 0.66f) {
    float t = (speed_ratio - 0.33f) / 0.33f;
    return BlendColor(entities::particle_mid_low_color,
                      entities::particle_mid_high_color, t);
  } else {
    float t = (speed_ratio - 0.66f) / 0.34f;
    return BlendColor(entities::particle_mid_high_color,
                      entities::particle_high_color, t);
  }
}

inline void RenderFluid(ECS& ecs,
                        const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      Color particle_color = VelocityToColor(vel.velocity);
      bool selected = e == entities::selected_particle;

      if (selected && entities::selection_locked) {
        DrawCircleV(pos.position, c.radius * 1.5f, WHITE);
        if (entities::render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius * 1.5f, RED, 0.25f);
      } else {
        DrawCircleV(pos.position, c.radius, particle_color);
        if (entities::render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius, particle_color,
                      0.25f);
      }
    });

  EndMode2D();
}

inline void RenderPressureField(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::CircleComponent& c) {
      Color col = PressureToColor(c.pressure);

      DrawCircleGradient(pos.position, entities::smoothing_radius * 0.8f, col,
                         Color{col.r, col.g, col.b, 0});
    });

  EndMode2D();
}

inline void DrawTriangleCCW(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
  float area = (v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y);
  if (area < 0.f) std::swap(v2, v3);

  float minX = std::min({v1.x, v2.x, v3.x});
  float maxX = std::max({v1.x, v2.x, v3.x});
  float minY = std::min({v1.y, v2.y, v3.y});
  float maxY = std::max({v1.y, v2.y, v3.y});

  Rectangle rec{minX, minY, maxX - minX, maxY - minY};

  DrawRectangleGradientEx(rec, color, color, color, color);
}

inline void RenderFluidFilled(ECS& ecs,
                              const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  components::CanvasComponent* canvas_ptr = nullptr;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) { canvas_ptr = &canvas; });

  if (!canvas_ptr) {
    EndMode2D();
    return;
  }

  auto& canvas = *canvas_ptr;

  constexpr int cell_size = 4;

  int grid_w = (CANVAS_W + cell_size + 2) / cell_size;
  int grid_h = (CANVAS_H + cell_size + 1) / cell_size;

  static std::vector<float> field(grid_w * grid_h);
  static std::vector<float> speed_field(grid_w * grid_h);

  std::fill(field.begin(), field.end(), 0.f);
  std::fill(speed_field.begin(), speed_field.end(), 0.f);

  constexpr float radius = 20.f;
  constexpr float radius_sq = radius * radius;
  constexpr float threshold = 0.5f;

  auto CellIndex = [&](int x, int y) { return y * grid_w + x; };
  float he_x = canvas.half_extents.x;
  float he_y = canvas.half_extents.y;
  auto LocalToWorld = [&](float lx, float ly) {
    return CanvasLocalToWorld({lx, ly}, canvas);
  };

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent&) {
      Vector2 local_pos = WorldToCanvasLocal(pos.position, canvas);
      int gx = static_cast<int>((local_pos.x + he_x) / cell_size);
      int gy = static_cast<int>((local_pos.y + he_y) / cell_size);

      int reach = static_cast<int>(radius / cell_size) + 1;

      float speed = Vector2Length(vel.velocity);

      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          int nx = gx + ox;
          int ny = gy + oy;

          if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h) continue;

          float px = nx * cell_size - he_x;
          float py = ny * cell_size - he_y;

          float dx = px - local_pos.x;
          float dy = py - local_pos.y;

          float dist_sq = dx * dx + dy * dy;

          float influence = FluidFieldKernel(dist_sq, radius_sq);

          int idx = CellIndex(nx, ny);

          field[idx] += influence;
          speed_field[idx] += speed * influence;
        }
      }
    });

  for (int y = 0; y < grid_h - 1; ++y) {
    for (int x = 0; x < grid_w - 1; ++x) {
      float v0 = field[CellIndex(x, y)];
      float v1 = field[CellIndex(x + 1, y)];
      float v2 = field[CellIndex(x + 1, y + 1)];
      float v3 = field[CellIndex(x, y + 1)];

      if (v0 < threshold && v1 < threshold && v2 < threshold && v3 < threshold)
        continue;

      int state = 0;

      if (v0 > threshold) state |= 1;
      if (v1 > threshold) state |= 2;
      if (v2 > threshold) state |= 4;
      if (v3 > threshold) state |= 8;

      if (state == 0) continue;

      float offset_x = 0.f;
      Vector2 p0{float(x * cell_size) - he_x - offset_x,
                 float(y * cell_size) - he_y};
      Vector2 p1{float((x + 1) * cell_size) - he_x - offset_x,
                 float(y * cell_size) - he_y};
      Vector2 p2{float((x + 1) * cell_size) - he_x - offset_x,
                 float((y + 1) * cell_size) - he_y};
      Vector2 p3{float(x * cell_size) - he_x - offset_x,
                 float((y + 1) * cell_size) - he_y};

      Vector2 a = InterpolateEdge(p0, p1, v0, v1, threshold);
      Vector2 b = InterpolateEdge(p1, p2, v1, v2, threshold);
      Vector2 c = InterpolateEdge(p2, p3, v2, v3, threshold);
      Vector2 d = InterpolateEdge(p3, p0, v3, v0, threshold);

      Vector2 aw = LocalToWorld(a.x, a.y);
      Vector2 bw = LocalToWorld(b.x, b.y);
      Vector2 cw = LocalToWorld(c.x, c.y);
      Vector2 dw = LocalToWorld(d.x, d.y);
      Vector2 p0w = LocalToWorld(p0.x, p0.y);
      Vector2 p1w = LocalToWorld(p1.x, p1.y);
      Vector2 p2w = LocalToWorld(p2.x, p2.y);
      Vector2 p3w = LocalToWorld(p3.x, p3.y);

      float avg = (v0 + v1 + v2 + v3) * 0.25f;

      float speed = avg > 0.f
                      ? (speed_field[CellIndex(x, y)] / field[CellIndex(x, y)])
                      : 0.f;

      Color col = SpeedToColor(speed);
      float cell_value = std::max({v0, v1, v2, v3});
      float influence_alpha = (cell_value - threshold) / (1.f - threshold);
      influence_alpha = Clamp(influence_alpha, 0.f, 1.f);
      col.a = static_cast<unsigned char>(influence_alpha * 200.f);

      switch (state) {
        case 1:
          DrawTriangleCCW(p0w, dw, aw, col);
          break;

        case 2:
          DrawTriangleCCW(p1w, aw, bw, col);
          break;

        case 3:
          DrawTriangleCCW(p0w, p1w, bw, col);
          DrawTriangleCCW(p0w, bw, dw, col);
          break;

        case 4:
          DrawTriangleCCW(p2w, bw, cw, col);
          break;

        case 5:
          DrawTriangleCCW(p0w, dw, aw, col);
          DrawTriangleCCW(p2w, bw, cw, col);
          break;

        case 6:
          DrawTriangleCCW(aw, p1w, p2w, col);
          DrawTriangleCCW(aw, p2w, cw, col);
          break;

        case 7:
          DrawTriangleCCW(p0w, p1w, p2w, col);
          DrawTriangleCCW(p0w, p2w, cw, col);
          DrawTriangleCCW(p0w, cw, dw, col);
          break;

        case 8:
          DrawTriangleCCW(p3w, cw, dw, col);
          break;

        case 9:
          DrawTriangleCCW(p0w, aw, p3w, col);
          DrawTriangleCCW(p3w, aw, cw, col);
          break;

        case 10:
          DrawTriangleCCW(p1w, aw, bw, col);
          DrawTriangleCCW(p3w, cw, dw, col);
          break;

        case 11:
          DrawTriangleCCW(p0w, p1w, bw, col);
          DrawTriangleCCW(p0w, bw, cw, col);
          DrawTriangleCCW(p0w, cw, p3w, col);
          break;

        case 12:
          DrawTriangleCCW(p3w, p2w, bw, col);
          DrawTriangleCCW(p3w, bw, dw, col);
          break;

        case 13:
          DrawTriangleCCW(p0w, aw, p2w, col);
          DrawTriangleCCW(p0w, p2w, p3w, col);
          break;

        case 14:
          DrawTriangleCCW(p1w, p2w, p3w, col);
          DrawTriangleCCW(p1w, p3w, dw, col);
          DrawTriangleCCW(p1w, dw, aw, col);
          break;

        case 15:
          DrawTriangleCCW(p0w, p1w, p2w, col);
          DrawTriangleCCW(p0w, p2w, p3w, col);
          break;
      }

      Color glow = {200, 200, 200, 180};
      switch (state) {
        case 1:
        case 14:
          DrawLineEx(dw, aw, 2.f, glow);
          break;
        case 2:
        case 13:
          DrawLineEx(aw, bw, 2.f, glow);
          break;
        case 3:
        case 12:
          DrawLineEx(dw, bw, 2.f, glow);
          break;
        case 4:
        case 11:
          DrawLineEx(bw, cw, 2.f, glow);
          break;
        case 5:
          DrawLineEx(dw, cw, 2.f, glow);
          DrawLineEx(aw, bw, 2.f, glow);
          break;
        case 6:
        case 9:
          DrawLineEx(aw, cw, 2.f, glow);
          break;
        case 7:
        case 8:
          DrawLineEx(dw, cw, 2.f, glow);
          break;
        case 10:
          DrawLineEx(aw, dw, 2.f, glow);
          DrawLineEx(bw, cw, 2.f, glow);
          break;
      }
    }
  }

  EndMode2D();
}

inline void RenderMouseSelectionCircle(const components::CameraComponent& cam) {
  if (!entities::selection_locked) return;

  BeginMode2D(cam.camera);

  DrawCircleLines(entities::selection_center.x, entities::selection_center.y,
                  entities::smoothing_radius, entities::selection_color);

  EndMode2D();
}

inline void RenderUserPath(ECS& ecs, const components::CameraComponent& cam) {
  if (entities::user_path_points.size() < 2) return;

  BeginMode2D(cam.camera);

  Color path_color = WHITE;
  float thickness = 4.f;

  // Convert canvas-local points to world coordinates
  std::vector<Vector2> world_points;
  world_points.resize(entities::user_path_points.size());

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      for (size_t i = 0; i < entities::user_path_points.size(); ++i) {
        world_points[i] =
          CanvasLocalToWorld(entities::user_path_points[i], canvas);
      }
    });

  // Draw path using splines for smooth curves
  if (world_points.size() >= 4) {
    DrawSplineCatmullRom(world_points.data(), world_points.size(), thickness, path_color);
  } else if (world_points.size() >= 2) {
    DrawSplineLinear(world_points.data(), world_points.size(), thickness, path_color);
  }

  EndMode2D();
}

/**
 * ============================================================================
 * Collisions
 * ============================================================================
 */
inline void ResolveCollisions(ECS& ecs) {
  const float particle_repulsion = 0.05f;

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
    float cell_size = entities::particle_size * 4.f;
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
  if (entities::user_path_points.size() >= 2) {
    ecs.group_view<components::CanvasComponent>(
      [&](Entity, components::CanvasComponent& canvas) {
        // Convert all points to world coordinates
        std::vector<Vector2> world_path;
        world_path.resize(entities::user_path_points.size());
        for (size_t i = 0; i < entities::user_path_points.size(); ++i) {
          world_path[i] =
            CanvasLocalToWorld(entities::user_path_points[i], canvas);
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
  int effective_threads = GetEffectiveThreads(particle_entities.size());
  if (!threads_initialized || effective_threads <= 1 ||
      particle_entities.size() < 256) {
    float h = entities::smoothing_radius;
    float h2 = h * h;
    float mass = entities::particle_size;

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
  int chunk_size = n / effective_threads;
  if (chunk_size < 64) chunk_size = 64;

  temp_densities.resize(n);

  std::vector<pthread_t> threads(effective_threads);
  std::vector<ParallelDensityTask> tasks(effective_threads);

  for (int i = 0; i < effective_threads; ++i) {
    tasks[i].start = i * chunk_size;
    tasks[i].end = std::min(tasks[i].start + chunk_size, n);
    pthread_create(&threads[i], nullptr, ComputeDensityRange, &tasks[i]);
  }

  for (int i = 0; i < effective_threads; ++i) {
    pthread_join(threads[i], nullptr);
  }

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
  if (entities::particle_cache_dirty || !particle_entities_cached) {
    CacheParticleEntities(ecs);
    entities::particle_cache_dirty = false;
  }

  if (entities::is_paused && !force_simulate) return;

  UpdateKernelCache();

  float effective_dt = dt * entities::sim_speed;

  PredictPositions(ecs, effective_dt);
  ComputeParticleDensity(ecs);
  ComputeParticlePressure(ecs);
  ComputeParticlePressureForce(ecs, effective_dt);

  size_t n = particle_entities.size();
  for (size_t i = 0; i < n; ++i) {
    components::CircleComponent* c = circ_cache[i];
    c->radius = entities::particle_size;
    c->particle_size = entities::particle_size;
  }

  for (size_t i = 0; i < n; ++i) {
    pos_cache[i]->position.x += vel_cache[i]->velocity.x * effective_dt;
    pos_cache[i]->position.y += vel_cache[i]->velocity.y * effective_dt;
  }

  float damp = entities::velocity_damping;
  for (size_t i = 0; i < n; ++i) {
    vel_cache[i]->velocity.x *= damp;
    vel_cache[i]->velocity.y *= damp;
  }

  ResolveCollisions(ecs);
}

}  // namespace motrix::engine::systems
