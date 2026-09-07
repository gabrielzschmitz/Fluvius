// engine/systems/physics.h
#pragma once

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

  // Uniform grid built by counting sort (see BuildSpatialGrid). grid_particles
  // packs, for each cell c, the particle indices in [grid_start[c],
  // grid_start[c+1]); the runs are concatenated so neighbor sweeps stream
  // linearly. Invariant: sum(grid_counts) == particle count for the step.
  int grid_cols = 0;
  int grid_rows = 0;
  float grid_cell_size = 0.f;
  int grid_cell_count = 0;
  std::vector<int> grid_counts;
  std::vector<int> grid_start;
  std::vector<size_t> grid_particles;

  // Cell-sorted soa copies of the per-particle hot data, produced by the
  // grid scatter, so neighbor loops stream contiguous floats instead of
  // random-access gather by unpredictable index. sorted data at position k
  // belongs to the same particle whose id is grid_particles[k].
  std::vector<float> sorted_px;
  std::vector<float> sorted_py;
  std::vector<float> sorted_dens;
  std::vector<float> sorted_press;
  std::vector<float> sorted_vx;
  std::vector<float> sorted_vy;

  // Inverse scatter map used to skip the self term: id_to_sorted[i] is the
  // sorted position of particle i in the current grid.
  std::vector<int> id_to_sorted;

  // Per-worker histograms and scatter offsets for the parallel grid build
  // (row w is [w*cell_count + c]). Sized to num_threads at build time.
  std::vector<int> grid_hist;
  std::vector<int> grid_base;

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
  int cell_count = cols * rows;
  size_t n = pb.predicted_positions.size();

  if (pb.grid_cols != cols || pb.grid_rows != rows ||
      pb.grid_cell_size != h || pb.grid_cell_count != cell_count) {
    pb.grid_counts.assign(static_cast<size_t>(cell_count), 0);
    pb.grid_start.assign(static_cast<size_t>(cell_count) + 1, 0);
    pb.grid_cell_count = cell_count;
    pb.grid_cols = cols;
    pb.grid_rows = rows;
    pb.grid_cell_size = h;
  }
  int workers = num_threads > 0 ? num_threads : 1;
  if (static_cast<size_t>(workers) * cell_count != pb.grid_hist.size()) {
    pb.grid_hist.assign(static_cast<size_t>(workers) * cell_count, 0);
    pb.grid_base.assign(static_cast<size_t>(workers) * cell_count, 0);
  }
  pb.grid_particles.resize(n);
  pb.sorted_px.resize(n);
  pb.sorted_py.resize(n);
  pb.sorted_dens.resize(n);
  pb.sorted_press.resize(n);
  pb.sorted_vx.resize(n);
  pb.sorted_vy.resize(n);
  pb.id_to_sorted.resize(n);

  std::fill(pb.grid_hist.begin(), pb.grid_hist.end(), 0);

  int effective = GetEffectiveThreads(n);
  int row_stride = cell_count;

  struct GridCountTask {
    int start;
    int end;
    int cols;
    int rows;
    int cell_count;
    float h;
    int row_stride;
    size_t n;
    std::vector<Vector2>* positions;
    std::vector<int>* hist;
  };
  GridCountTask count_seed{0, 0, cols, rows, cell_count, h, row_stride, n,
                           &pb.predicted_positions, &pb.grid_hist};
  RunParallelChunks(effective, n, count_seed, [](void* arg, int thread) -> void* {
    auto* tk = static_cast<GridCountTask*>(arg);
    int cell_count = tk->cell_count;
    std::vector<int>& hist = *tk->hist;
    int* row = hist.data() + static_cast<size_t>(thread) * cell_count;
    const std::vector<Vector2>& positions = *tk->positions;

    int cols = tk->cols;
    int rows = tk->rows;
    float h = tk->h;

    for (int i = tk->start; i < tk->end; ++i) {
      int cx;
      int cy;
      PositionToFlatCell(positions[i], h, cols, rows, cx, cy);
      ++row[cy * cols + cx];
    }
    return nullptr;
  });

  // Serial prefix-sum merge: per-cell totals plus each worker's scatter base.
  int total = 0;
  for (int c = 0; c < cell_count; ++c) {
    int sum = 0;
    for (int w = 0; w < workers; ++w) {
      int* hw = pb.grid_hist.data() + static_cast<size_t>(w) * cell_count;
      pb.grid_base[static_cast<size_t>(w) * cell_count + c] = total + sum;
      sum += hw[c];
    }
    pb.grid_counts[c] = sum;
    pb.grid_start[c] = total;
    total += sum;
  }
  pb.grid_start[cell_count] = total;

  struct GridScatterTask {
    int start;
    int end;
    int cols;
    int rows;
    int cell_count;
    float h;
    int row_stride;
    size_t n;
    std::vector<Vector2>* positions;
    std::vector<size_t>* packed;
    std::vector<int>* base;
    std::vector<float>* spx;
    std::vector<float>* spy;
    std::vector<int>* id_to_sorted;
  };
  GridScatterTask scatter_seed{0, 0, cols, rows, cell_count, h, row_stride, n,
                               &pb.predicted_positions, &pb.grid_particles,
                               &pb.grid_base, &pb.sorted_px, &pb.sorted_py,
                               &pb.id_to_sorted};
  RunParallelChunks(effective, n, scatter_seed,
                    [](void* arg, int thread) -> void* {
    auto* tk = static_cast<GridScatterTask*>(arg);
    int cell_count = tk->cell_count;
    int cols = tk->cols;
    int rows = tk->rows;
    float h = tk->h;
    const std::vector<Vector2>& positions = *tk->positions;
    std::vector<size_t>& packed = *tk->packed;
    int* base_row = tk->base->data() + static_cast<size_t>(thread) * cell_count;

    for (int i = tk->start; i < tk->end; ++i) {
      int cx;
      int cy;
      PositionToFlatCell(positions[i], h, cols, rows, cx, cy);
      int& cursor = base_row[cy * cols + cx];
      int k = cursor;
      packed[static_cast<size_t>(k)] = static_cast<size_t>(i);
      (*tk->spx)[static_cast<size_t>(k)] = positions[i].x;
      (*tk->spy)[static_cast<size_t>(k)] = positions[i].y;
      (*tk->id_to_sorted)[i] = k;
      ++cursor;
    }
    return nullptr;
  });

  // Gather the remaining per-particle hot data into sorted order so the
  // force kernel reads nothing by unpredictable index.
  struct GatherTask {
    int start;
    int end;
    size_t n;
    std::vector<int>* id_to_sorted;
    std::vector<Vector2>* velocities;
    std::vector<float>* densities;
    std::vector<float>* pressures;
    std::vector<float>* svx;
    std::vector<float>* svy;
    std::vector<float>* sdens;
    std::vector<float>* spress;
  };
  GatherTask gather_seed{0, 0, n, &pb.id_to_sorted, &pb.velocities,
                         &pb.densities, &pb.pressures, &pb.sorted_vx,
                         &pb.sorted_vy, &pb.sorted_dens, &pb.sorted_press};
  RunParallelChunks(effective, n, gather_seed, [](void* arg, int) -> void* {
    auto* tk = static_cast<GatherTask*>(arg);
    const std::vector<int>& map = *tk->id_to_sorted;
    const std::vector<Vector2>& velocities = *tk->velocities;
    const std::vector<float>& densities = *tk->densities;
    const std::vector<float>& pressures = *tk->pressures;

    for (int i = tk->start; i < tk->end; ++i) {
      int k = map[static_cast<size_t>(i)];
      (*tk->svx)[static_cast<size_t>(k)] = velocities[static_cast<size_t>(i)].x;
      (*tk->svy)[static_cast<size_t>(k)] = velocities[static_cast<size_t>(i)].y;
      (*tk->sdens)[static_cast<size_t>(k)] = densities[static_cast<size_t>(i)];
      (*tk->spress)[static_cast<size_t>(k)] =
        pressures[static_cast<size_t>(i)];
    }
    return nullptr;
  });
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
  pb.densities.resize(n);
  pb.pressures.resize(n);

  struct PredictTask {
    int start;
    int end;
    float grav;
    float dt;
    PhysicsBuffers* pb;
  };
  PredictTask seed{0, static_cast<int>(n), gravity, dt, &pb};
  RunParallelChunks(GetEffectiveThreads(n), n, seed, [](void* arg, int) -> void* {
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

inline void* ComputeDensityRange(void* arg, int) {
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

        int cell = ny * cols + nx;
        for (int k = pb.grid_start[cell]; k < pb.grid_start[cell + 1]; ++k) {
          float p2x = pb.sorted_px[k];
          float p2y = pb.sorted_py[k];

          float rx = p2x - p.x;
          float ry = p2y - p.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= h2) density += mass * Poly6Kernel(r2, h);
        }
      }
    }

    float pressure = (density - target_density) * pressure_multiplier;
    pb.circ_cache[i]->density = density;
    pb.densities[i] = density;
    pb.pressures[i] = pressure;

    int k_self = pb.id_to_sorted[i];
    pb.sorted_dens[k_self] = density;
    pb.sorted_press[k_self] = pressure;
  }

  return nullptr;
}

// AVX2 density pass. The sorted grid hands us contiguous per-cell position
// runs, so the inner neighbor loop reduces to 8-wide squared-distance +
// masked Poly6 accumulation with no per-neighbor scalar loads. Compiled for
// AVX2 regardless of the global flag set, but only selected when the CPU
// actually supports it (see ComputeParticleDensity).
#if defined(__x86_64__) && defined(__GNUC__) || defined(__clang__)
#pragma GCC push_options
#pragma GCC target("avx2")
#include <immintrin.h>
inline bool Avx2DensityAvailable() {
  return __builtin_cpu_supports("avx2");
}

// Debugging nail for A/B-ing the SIMD kernels without a rebuild.
inline bool NoAvx2ForDebug() {
  const char* v = std::getenv("FLUVIUS_NO_AVX2");
  return v && v[0] == '1';
}

inline void* ComputeDensityRangeAvx2(void* arg, int) {
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
  const float* px = pb.sorted_px.data();
  const float* py = pb.sorted_py.data();

  // 315 / (64 * PI * h^9), folded with mass so each lane does mul-by-const.
  const float poly6_scale = (315.f / (64.f * PI)) * (1.f / (h2 * h2 * h2 * h2)) *
                            (1.f / h) * mass;
  const __m256 h2v = _mm256_set1_ps(h2);
  const __m256 scalev = _mm256_set1_ps(poly6_scale);

  for (int i = start; i < end && i < static_cast<int>(pb.particle_entities.size());
       ++i) {
    Vector2 p = pb.predicted_positions[i];

    int cx;
    int cy;
    PositionToFlatCell(p, h, cols, rows, cx, cy);

    __m256 pxv = _mm256_set1_ps(p.x);
    __m256 pyv = _mm256_set1_ps(p.y);
    __m256 acc = _mm256_setzero_ps();

    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      if (nx < 0 || nx >= cols) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;

        int cell = ny * cols + nx;
        int k0 = pb.grid_start[cell];
        int k1 = pb.grid_start[cell + 1];

        int k = k0;
        for (; k + 8 <= k1; k += 8) {
          __m256 sx = _mm256_loadu_ps(px + k);
          __m256 sy = _mm256_loadu_ps(py + k);
          __m256 rx = _mm256_sub_ps(sx, pxv);
          __m256 ry = _mm256_sub_ps(sy, pyv);
          __m256 r2 = _mm256_add_ps(_mm256_mul_ps(rx, rx),
                                    _mm256_mul_ps(ry, ry));
          __m256 mask = _mm256_cmp_ps(r2, h2v, _CMP_LE_OQ);
          __m256 diff = _mm256_sub_ps(h2v, r2);
          __m256 diff2 = _mm256_mul_ps(diff, diff);
          __m256 diff3 = _mm256_mul_ps(diff2, diff);
          __m256 contrib = _mm256_mul_ps(scalev, diff3);
          contrib = _mm256_and_ps(contrib, mask);
          acc = _mm256_add_ps(acc, contrib);
        }

        for (; k < k1; ++k) {
          float rx = px[k] - p.x;
          float ry = py[k] - p.y;
          float r2 = rx * rx + ry * ry;
          if (r2 <= h2) acc = _mm256_add_ps(acc, _mm256_set1_ps(mass * Poly6Kernel(r2, h)));
        }
      }
    }

    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    float density = _mm_cvtss_f32(s);

    float pressure = (density - target_density) * pressure_multiplier;
    pb.circ_cache[i]->density = density;
    pb.densities[i] = density;
    pb.pressures[i] = pressure;

    int k_self = pb.id_to_sorted[i];
    pb.sorted_dens[k_self] = density;
    pb.sorted_press[k_self] = pressure;
  }

  return nullptr;
}
#pragma GCC pop_options
#endif

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

inline void* ComputePressureForceRange(void* arg, int) {
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

    // p_pressure/d^2 of the center particle is shared by every neighbor.
    float p1_term = p1_pressure / (d1 * d1);
    int k_self = pb.id_to_sorted[i];

    Vector2 pressure_force{0.f, 0.f};
    Vector2 viscosity_force{0.f, 0.f};
    Vector2 cohesion_force{0.f, 0.f};

    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      if (nx < 0 || nx >= cols) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;

        int cell = ny * cols + nx;
        for (int k = pb.grid_start[cell]; k < pb.grid_start[cell + 1]; ++k) {
          if (k == k_self) continue;

          float p2x = pb.sorted_px[k];
          float p2y = pb.sorted_py[k];

          float rx = p1.x - p2x;
          float ry = p1.y - p2y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= 0.f || r2 > h2) continue;

          float r = sqrtf(r2);
          float inv_r = 1.f / r;

          Vector2 dir{rx * inv_r, ry * inv_r};

          float grad = SpikyKernelGradient(r, h);

          float d2 = pb.sorted_dens[k];
          float p2_pressure = pb.sorted_press[k];
          float v2x = pb.sorted_vx[k];
          float v2y = pb.sorted_vy[k];

          float term = p1_term + (p2_pressure / (d2 * d2));

          float factor = -mass * term * grad;

          pressure_force.x += dir.x * factor;
          pressure_force.y += dir.y * factor;

          float visc = ViscosityKernel(r, h);

          viscosity_force.x += (v2x - v1.x) * visc;
          viscosity_force.y += (v2y - v1.y) * visc;

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

// AVX2 force pass: the same counting-sort runs plus sorted soa neighbor data
// let 8 pairs evaluate per iteration. The r2 > 0 test naturally excludes the
// self term, and masked lanes are forced to +0.0 bitwise to avoid any
// NaN/inf propagation from the reciprocal of a zero-distance lane.
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC target("avx2")
inline bool Avx2ForceAvailable() {
  return __builtin_cpu_supports("avx2");
}

inline void* ComputePressureForceRangeAvx2(void* arg, int) {
  auto* task = static_cast<ForceTask*>(arg);
  const int start = task->start;
  const int end = task->end;

  const float h = task->h;
  const float h2 = h * h;
  const float mass = task->mass;
  PhysicsBuffers& pb = *task->pb;

  const int cols = pb.grid_cols;
  const int rows = pb.grid_rows;
  const float* px = pb.sorted_px.data();
  const float* py = pb.sorted_py.data();
  const float* dens = pb.sorted_dens.data();
  const float* press = pb.sorted_press.data();
  const float* vx = pb.sorted_vx.data();
  const float* vy = pb.sorted_vy.data();

  const float h5 = h * h * h * h * h;
  const __m256 h2v = _mm256_set1_ps(h2);
  const __m256 spiky_scale = _mm256_set1_ps(-15.f / (PI * h5));
  const __m256 visc_scale = _mm256_set1_ps(15.f / (2.f * PI * h5));
  const __m256 inv_half_h = _mm256_set1_ps(2.f / h);
  const __m256 minus_mass = _mm256_set1_ps(-mass);

  for (int i = start; i < end && i < static_cast<int>(pb.particle_entities.size());
       ++i) {
    Vector2 p1 = pb.predicted_positions[i];

    int cx;
    int cy;
    PositionToFlatCell(p1, h, cols, rows, cx, cy);

    float d1 = pb.densities[i];
    float p1_pressure = pb.pressures[i];
    float v1x = pb.velocities[i].x;
    float v1y = pb.velocities[i].y;

    float p1_term = p1_pressure / (d1 * d1);

    __m256 pxv = _mm256_set1_ps(p1.x);
    __m256 pyv = _mm256_set1_ps(p1.y);
    __m256 p1_termv = _mm256_set1_ps(p1_term);
    __m256 v1xv = _mm256_set1_ps(v1x);
    __m256 v1yv = _mm256_set1_ps(v1y);

    __m256 pfx = _mm256_setzero_ps();
    __m256 pfy = _mm256_setzero_ps();
    __m256 vfx = _mm256_setzero_ps();
    __m256 vfy = _mm256_setzero_ps();
    __m256 cfx = _mm256_setzero_ps();
    __m256 cfy = _mm256_setzero_ps();

    for (int dx = -1; dx <= 1; ++dx) {
      int nx = cx + dx;
      if (nx < 0 || nx >= cols) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        int ny = cy + dy;
        if (ny < 0 || ny >= rows) continue;

        int cell = ny * cols + nx;
        int k0 = pb.grid_start[cell];
        int k1 = pb.grid_start[cell + 1];

        int k = k0;
        for (; k + 8 <= k1; k += 8) {
          __m256 sx = _mm256_loadu_ps(px + k);
          __m256 sy = _mm256_loadu_ps(py + k);

          __m256 rx = _mm256_sub_ps(pxv, sx);
          __m256 ry = _mm256_sub_ps(pyv, sy);
          __m256 r2 = _mm256_add_ps(_mm256_mul_ps(rx, rx),
                                    _mm256_mul_ps(ry, ry));

          __m256 in = _mm256_cmp_ps(r2, _mm256_setzero_ps(), _CMP_GT_OQ);
          __m256 le = _mm256_cmp_ps(r2, h2v, _CMP_LE_OQ);
          __m256 mask = _mm256_and_ps(in, le);

          __m256 r = _mm256_sqrt_ps(r2);
          __m256 inv_r = _mm256_div_ps(_mm256_set1_ps(1.f), r);

          __m256 dirx = _mm256_and_ps(_mm256_mul_ps(rx, inv_r), mask);
          __m256 diry = _mm256_and_ps(_mm256_mul_ps(ry, inv_r), mask);

          __m256 vh = _mm256_sub_ps(_mm256_set1_ps(h), r);
          __m256 vh2 = _mm256_mul_ps(vh, vh);
          __m256 grad = _mm256_and_ps(_mm256_mul_ps(spiky_scale, vh2), mask);

          __m256 d2v = _mm256_loadu_ps(dens + k);
          __m256 p2v = _mm256_loadu_ps(press + k);
          __m256 d2sq = _mm256_mul_ps(d2v, d2v);
          __m256 term = _mm256_add_ps(p1_termv, _mm256_div_ps(p2v, d2sq));

          __m256 factor = _mm256_mul_ps(minus_mass, _mm256_mul_ps(term, grad));

          pfx = _mm256_add_ps(pfx, _mm256_mul_ps(dirx, factor));
          pfy = _mm256_add_ps(pfy, _mm256_mul_ps(diry, factor));

          __m256 visc = _mm256_and_ps(_mm256_mul_ps(visc_scale, vh), mask);
          __m256 v2xv = _mm256_loadu_ps(vx + k);
          __m256 v2yv = _mm256_loadu_ps(vy + k);
          vfx = _mm256_add_ps(vfx, _mm256_mul_ps(_mm256_sub_ps(v2xv, v1xv), visc));
          vfy = _mm256_add_ps(vfy, _mm256_mul_ps(_mm256_sub_ps(v2yv, v1yv), visc));

          __m256 q = _mm256_mul_ps(r, inv_half_h);
          __m256 alpha = _mm256_sub_ps(_mm256_set1_ps(1.f), q);
          __m256 coh_mask =
            _mm256_and_ps(mask, _mm256_cmp_ps(r,
                                              _mm256_set1_ps(h * 0.5f),
                                              _CMP_LT_OQ));
          __m256 cohes =
            _mm256_and_ps(_mm256_mul_ps(alpha, alpha), coh_mask);

          cfx = _mm256_add_ps(cfx, _mm256_mul_ps(dirx, cohes));
          cfy = _mm256_add_ps(cfy, _mm256_mul_ps(diry, cohes));
        }

        for (; k < k1; ++k) {
          float p2x = px[k];
          float p2y = py[k];

          float rx = p1.x - p2x;
          float ry = p1.y - p2y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= 0.f || r2 > h2) continue;

          float r = sqrtf(r2);
          float inv_r = 1.f / r;

          Vector2 dir{rx * inv_r, ry * inv_r};

          float grad = SpikyKernelGradient(r, h);

          float d2 = dens[k];
          float p2_pressure = press[k];
          float v2x = vx[k];
          float v2y = vy[k];

          float term = p1_term + (p2_pressure / (d2 * d2));
          float factor = -mass * term * grad;

          pfx = _mm256_add_ps(pfx, _mm256_set1_ps(dir.x * factor));
          pfy = _mm256_add_ps(pfy, _mm256_set1_ps(dir.y * factor));

          float visc = ViscosityKernel(r, h);
          vfx = _mm256_add_ps(vfx, _mm256_set1_ps((v2x - v1x) * visc));
          vfy = _mm256_add_ps(vfy, _mm256_set1_ps((v2y - v1y) * visc));

          float cohes = CohesionKernel(r, h);
          cfx = _mm256_add_ps(cfx, _mm256_set1_ps(dir.x * cohes));
          cfy = _mm256_add_ps(cfy, _mm256_set1_ps(dir.y * cohes));
        }
      }
    }

    auto horiz = [](__m256 v) {
      __m128 lo = _mm256_castps256_ps128(v);
      __m128 hi = _mm256_extractf128_ps(v, 1);
      __m128 s = _mm_add_ps(lo, hi);
      s = _mm_hadd_ps(s, s);
      s = _mm_hadd_ps(s, s);
      return _mm_cvtss_f32(s);
    };

    pb.pressure_forces_data[i * 2] = horiz(pfx);
    pb.pressure_forces_data[i * 2 + 1] = horiz(pfy);
    pb.viscosity_forces_data[i * 2] = horiz(vfx);
    pb.viscosity_forces_data[i * 2 + 1] = horiz(vfy);
    pb.cohesion_forces_data[i * 2] = horiz(cfx);
    pb.cohesion_forces_data[i * 2 + 1] = horiz(cfy);
  }

  return nullptr;
}
#endif

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
inline void* ApplyFluidForcesRange(void* arg, int) {
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
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
  static const bool use_avx2 = Avx2ForceAvailable();
  RunParallelChunks(GetEffectiveThreads(n), n, force_seed,
                    (use_avx2 && !NoAvx2ForDebug()) ? ComputePressureForceRangeAvx2
                                                    : ComputePressureForceRange);
#else
  RunParallelChunks(GetEffectiveThreads(n), n, force_seed,
                    ComputePressureForceRange);
#endif

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

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
  static const bool use_avx2 = Avx2DensityAvailable();
  RunParallelChunks(GetEffectiveThreads(n), n, seed,
                    (use_avx2 && !NoAvx2ForDebug()) ? ComputeDensityRangeAvx2
                                                    : ComputeDensityRange);
#else
  RunParallelChunks(GetEffectiveThreads(n), n, seed, ComputeDensityRange);
#endif
}

/**
 * ============================================================================
 * Simulation
 * ============================================================================
 */
inline void SimulateFluid(ECS& ecs, float dt, bool force_simulate = false) {
  auto& sim = entities::Simulation(ecs);
  PhysicsBuffers& pb = Physics(ecs);

  static bool profile = [] {
    const char* v = std::getenv("FLUVIUS_PROFILE");
    return v && v[0] == '1';
  }();
  static size_t profile_step = 0;
  static double t_predict = 0, t_density = 0, t_force = 0, t_collide = 0,
                t_total = 0;

  using Clock = std::chrono::steady_clock;

  if (sim.particle_cache_dirty || !pb.particle_entities_cached) {
    CacheParticleEntities(ecs, pb);
    sim.particle_cache_dirty = false;
  }

  if (sim.is_paused && !force_simulate) return;

  UpdateKernelCache(pb, sim.gravity);

  float effective_dt = dt * sim.sim_speed;

  auto t0 = Clock::now();
  PredictPositions(ecs, pb, effective_dt);
  auto t1 = Clock::now();
  ComputeParticleDensity(ecs);
  auto t2 = Clock::now();
  ComputeParticlePressureForce(ecs, effective_dt, sim.velocity_damping);
  auto t3 = Clock::now();
  ResolveCollisions(ecs);
  auto t4 = Clock::now();

  if (profile) {
    t_predict += std::chrono::duration<double, std::milli>(t1 - t0).count();
    t_density += std::chrono::duration<double, std::milli>(t2 - t1).count();
    t_force += std::chrono::duration<double, std::milli>(t3 - t2).count();
    t_collide += std::chrono::duration<double, std::milli>(t4 - t3).count();
    t_total += std::chrono::duration<double, std::milli>(t4 - t0).count();
    ++profile_step;
    if (profile_step % 50 == 0) {
      std::fprintf(stderr,
                   "[profile] n=%zu total=%5.2f predict=%5.2f density=%5.2f "
                   "force=%5.2f collide=%5.2f ms/step\n",
                   pb.particle_entities.size(), t_total / profile_step,
                   t_predict / profile_step, t_density / profile_step,
                   t_force / profile_step, t_collide / profile_step);
    }
  }
}

}  // namespace motrix::engine::systems
