// bench/bench_fluid.cpp
//
// Performance benchmark for the Fluvius SPH fluid simulation.
//
// For a fixed set of simulation parameters it runs the exact same test (fixed
// number of steps Ns) for every particle count, sweeping from `min` up to
// `max`, and reports the resulting simulation FPS.
//
// Simulation parameters (matching the paper's Table):
//   particle size........ 2.0
//   smoothing radius h... 50.0
//   time step dt......... 0.01
//   target density rho... 0.000425
//   viscosity nu......... 0.8
//   pressure multiplier.. 250.0
//   gravity g............ 1.0
//   window resolution.... 1920 x 1080 (render mode only, canvas 640 x 360)
//
// By default the benchmark runs headless (physics pipeline only, no window),
// which makes it reproducible and runnable on machines without a display.
// Pass --render to open a 1920x1080 window that replicates the interactive
// app's scene - camera, canvas, particles spawned centered and the same frame
// rendering pipeline - and include full-frame rendering in the measurement
// (the FPS shown by the interactive app). A small on-screen HUD reports the
// particle count, the current step and the live FPS.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/globals.h"
#include "engine/logger.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"
#include "raylib.h"

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

struct BenchConfig {
  int min_particles = 50;
  int max_particles = 10000;
  int step = 50;
  int steps = 250;          // Ns
  int warmup = 0;           // un-timed setup steps before measuring
  int runs = 1;             // repetitions per particle count (averaged)
  int threads = 0;          // 0 -> hardware concurrency (matches the app)
  float dt = 0.2f;          // fixed passo temporal dt
  double settle_sec = 0.5;  // pause between particle counts
  bool render = false;
  bool show_help = false;
  std::string csv_path = "bench_results.csv";  // ./bench_results.csv
};

static void PrintUsage(const char* app) {
  printf(
    "Usage: %s [options]\n"
    "\n"
    "Runs the SPH simulation with fixed parameters for Ns steps at every\n"
    "particle count between --min and --max, measuring simulation FPS.\n"
    "\n"
    "Options:\n"
    "  --min <n>          first particle count (default 50)\n"
    "  --max <n>          last particle count (default 10000)\n"
    "  --step <n>         particle count increment (default 50)\n"
    "  --steps <n>        fixed number of steps Ns per particle count "
    "(alias --ns, default 2500)\n"
    "  --warmup <n>       un-timed steps before measurement (default 0)\n"
    "  --runs <n>         repetitions per particle count, reported FPS is the "
    "average (default 1)\n"
    "  --threads <n>      simulation threads (default: hardware concurrency)\n"
    "  --dt <t>           fixed time step dt (default 0.01)\n"
    "  --settle <sec>     seconds to pause between particle counts, letting "
    "the\n"
    "                     system settle before the next clean run (default "
    "2.0)\n"
    "  --render           open a 1920x1080 window with the app's scene\n"
    "                     (camera, canvas, centered particles) and include\n"
    "                     full-frame rendering in the measurement; a small "
    "HUD\n"
    "                     shows particles / step / live FPS on screen\n"
    "  --csv <file>       write results as CSV (default ./bench_results.csv)\n"
    "  -h, --help         show this help\n",
    app);
}

static bool ParseArgs(int argc, char** argv, BenchConfig& cfg) {
  auto need_value = [&](int& i, const char* opt, const char** out) -> bool {
    if (i + 1 >= argc) {
      fprintf(stderr, "error: %s requires a value\n", opt);
      return false;
    }
    *out = argv[++i];
    return true;
  };

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = nullptr;
    const char* opt = arg;

    // Strip leading "--name=" or "-name=" form
    std::string key_eq;
    const char* eq = std::strchr(arg, '=');
    if (eq) {
      key_eq = std::string(arg, eq - arg);
      opt = key_eq.c_str();
      value = eq + 1;
    }

    auto as_int = [](const char* v, int def) { return v ? std::atoi(v) : def; };

    if (std::strcmp(opt, "-h") == 0 || std::strcmp(opt, "--help") == 0) {
      cfg.show_help = true;
      return true;
    } else if (std::strcmp(opt, "--min") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.min_particles = std::atoi(value);
    } else if (std::strcmp(opt, "--max") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.max_particles = std::atoi(value);
    } else if (std::strcmp(opt, "--step") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.step = std::atoi(value);
    } else if (std::strcmp(opt, "--steps") == 0 ||
               std::strcmp(opt, "--ns") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.steps = std::atoi(value);
    } else if (std::strcmp(opt, "--warmup") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.warmup = std::atoi(value);
    } else if (std::strcmp(opt, "--runs") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.runs = std::atoi(value);
    } else if (std::strcmp(opt, "--threads") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.threads = std::atoi(value);
    } else if (std::strcmp(opt, "--dt") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.dt = static_cast<float>(std::atof(value));
    } else if (std::strcmp(opt, "--settle") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.settle_sec = std::atof(value);
    } else if (std::strcmp(opt, "--render") == 0) {
      cfg.render = true;
    } else if (std::strcmp(opt, "--csv") == 0) {
      if (!value && !need_value(i, opt, &value)) return false;
      cfg.csv_path = value;
    } else {
      fprintf(stderr, "error: unknown option '%s'\n", arg);
      return false;
    }
  }
  return true;
}

static bool Validate(const BenchConfig& cfg) {
  if (cfg.min_particles <= 0 || cfg.max_particles < cfg.min_particles) {
    fprintf(stderr, "error: invalid particle range [%d, %d]\n",
            cfg.min_particles, cfg.max_particles);
    return false;
  }
  if (cfg.step <= 0) {
    fprintf(stderr, "error: --step must be > 0\n");
    return false;
  }
  if (cfg.steps <= 0) {
    fprintf(stderr, "error: --steps (Ns) must be > 0\n");
    return false;
  }
  if (cfg.runs <= 0 || cfg.warmup < 0) {
    fprintf(stderr, "error: --runs and --warmup must be >= 0\n");
    return false;
  }
  if (cfg.dt <= 0.f) {
    fprintf(stderr, "error: --dt must be > 0\n");
    return false;
  }
  if (cfg.settle_sec < 0.0) {
    fprintf(stderr, "error: --settle must be >= 0\n");
    return false;
  }
  return true;
}

// Applies the fixed simulation parameters used by the experiments.
static void ApplySimulationParameters() {
  m_ett::particle_size = 2.0f;     // particle size
  m_ett::smoothing_radius = 50.f;  // support radius h
  m_ett::target_density = 0.000425f;
  m_ett::pressure_multiplier = 250.f;
  m_ett::viscosity = 0.8f;
  m_ett::gravity = 1.0f;
  m_ett::sim_speed = 1.0f;
  m_ett::is_paused = false;
  m_ett::create_centered = true;

  // Not listed in the table but required for a valid SPH run; keep the same
  // values the interactive app uses.
  m_ett::velocity_damping = 0.997f;
  m_ett::surface_tension = 0.25f;
}

// Fully resets every piece of state the physics module and entity factories
// keep across runs (caches, spatial grid, force/density buffers, particle and
// camera handles, kernel cache) so no leftover data from a previous particle
// count can influence the next measurement.
static void ResetPhysicsModule() {
  // physics module buffers / caches
  m_eng::systems::particle_entities.clear();
  m_eng::systems::predicted_positions.clear();
  m_eng::systems::pos_cache.clear();
  m_eng::systems::vel_cache.clear();
  m_eng::systems::circ_cache.clear();
  m_eng::systems::particle_entities_cached = false;
  m_eng::systems::spatial_grid.clear();
  m_eng::systems::temp_densities.clear();
  m_eng::systems::pressure_forces.clear();
  m_eng::systems::viscosity_forces.clear();
  m_eng::systems::cohesion_forces.clear();
  m_eng::systems::densities.clear();
  m_eng::systems::pressures.clear();
  m_eng::systems::velocities.clear();
  m_eng::systems::kernel_cache_valid = false;

  // entity factory state
  m_ett::fluid_particles.clear();
  m_ett::user_path_points.clear();
  m_ett::is_drawing_path = false;
  m_ett::selection_locked = false;
  m_ett::particle_cache_dirty = true;
}

// Creates a fresh, empty world with the simulation parameters applied and the
// physics module fully pointed at it. Each run gets its own clean EC/state.
static void CreateWorld(m_eng::ECS& world, int threads, bool render,
                        m_eng::Entity& cam_entity) {
  ResetPhysicsModule();
  m_eng::systems::ShutdownThreads();
  m_eng::systems::InitThreads(threads, world);

  // Canvas keeps particles confined like the interactive app does.
  motrix::entities::CreateCanvas(world);

  if (render) cam_entity = motrix::entities::CreateCamera(world);
}

// Rebuilds the fluid with `count` particles (deterministic centered grid).
static void PrepareFluid(m_eng::ECS& ecs, int count) {
  m_ett::CreateFluid(ecs, static_cast<size_t>(count), true);
  m_ett::particle_cache_dirty = true;
  m_eng::systems::particle_entities_cached = false;
  m_eng::systems::spatial_grid.clear();
}

// Renders a full 1920x1080 frame using the same pipeline (and scene) as the
// interactive app's Fluid scene: camera-space canvas + particles inside the
// app's outer camera transform. An on-screen HUD reports the current particle
// count, the step progress, either the live FPS or "Warming up", and all of
// the run's configurable (--arg) parameters.
static void RenderStep(m_eng::ECS& ecs, m_eng::Entity cam_entity,
                       const BenchConfig& cfg, int particles, int step,
                       bool warming_up, double fps) {
  auto& cam = ecs.get<m_eng::components::CameraComponent>(cam_entity);

  BeginDrawing();
  ClearBackground({1, 87, 87, 255});

  // Matches RenderApp: the scene runs inside the camera transform and the
  // canvas + fluid each manage their own 2D-mode push/pop.
  BeginMode2D(cam.camera);
  m_eng::systems::RenderCanvas(ecs, cam);
  m_eng::systems::RenderFluid(ecs, cam);
  EndMode2D();

  Color text = {246, 120, 232, 255};
  int y = 12;
  int line = 20;
  int gap = 26;
  DrawText(TextFormat("Particles: %d  (min: %d, max: %d, step: %d)", particles,
                      cfg.min_particles, cfg.max_particles, cfg.step),
           12, y, line, text);
  y += gap;
  if (warming_up) {
    DrawText("Warming up", 12, y, line, text);
  } else {
    DrawText(TextFormat("FPS: %.1f", fps), 12, y, line, text);
    y += gap;
    DrawText(TextFormat("Step: %d / %d", step, cfg.steps), 12, y, line, text);
  }
  y += gap;
  DrawText(TextFormat("dt: %.3f  Ns: %d  warmup: %d  runs: %d", cfg.dt,
                      cfg.steps, cfg.warmup, cfg.runs),
           12, y, line, text);
  y += gap;
  DrawText(TextFormat("threads: %d  settle: %.1fs  render: %s", cfg.threads,
                      cfg.settle_sec, cfg.render ? "yes" : "no"),
           12, y, line, text);
  y += gap;
  DrawText(
    TextFormat("csv: %s", cfg.csv_path.empty() ? "-" : cfg.csv_path.c_str()),
    12, y, line, text);
  EndDrawing();
}

int main(int argc, char** argv) {
  BenchConfig cfg;
  if (!ParseArgs(argc, argv, cfg)) {
    PrintUsage(argv[0]);
    return 1;
  }
  if (cfg.show_help) {
    PrintUsage(argv[0]);
    return 0;
  }
  if (!Validate(cfg)) {
    PrintUsage(argv[0]);
    return 1;
  }

  if (cfg.threads == 0) {
    cfg.threads = std::thread::hardware_concurrency();
    if (cfg.threads == 0) cfg.threads = 1;
  }

  ApplySimulationParameters();

  std::ostringstream header;
  header << "particle_size=" << m_ett::particle_size
         << ", h=" << m_ett::smoothing_radius
         << ", target_density=" << m_ett::target_density
         << ", pressure_multiplier=" << m_ett::pressure_multiplier
         << ", viscosity=" << m_ett::viscosity << ", gravity=" << m_ett::gravity
         << ", dt=" << cfg.dt << ", threads=" << cfg.threads
         << ", steps(Ns)=" << cfg.steps << ", warmup=" << cfg.warmup
         << ", runs=" << cfg.runs << ", settle=" << cfg.settle_sec << "s"
         << ", render=" << (cfg.render ? "yes" : "no");

  logger::info("{}", header.str());
  logger::info("Window resolution: {}x{} (canvas {}x{})", CANVAS_W * SCALE,
               CANVAS_H * SCALE, CANVAS_W, CANVAS_H);

  if (cfg.render) {
    InitWindow(CANVAS_W * SCALE, CANVAS_H * SCALE, "Fluvius Benchmark");
    if (!IsWindowReady()) {
      fprintf(stderr,
              "error: could not open window (is a display available?)\n");
      return 1;
    }
    SetTargetFPS(0);
  }

  // Particle counts: min .. max by step, always including max.
  std::vector<int> counts;
  for (int c = cfg.min_particles; c <= cfg.max_particles; c += cfg.step)
    counts.push_back(c);
  if (counts.back() != cfg.max_particles) counts.push_back(cfg.max_particles);

  std::ofstream csv;
  if (!cfg.csv_path.empty()) {
    csv.open(cfg.csv_path);
    if (!csv.is_open()) {
      fprintf(stderr, "error: cannot open CSV file '%s'\n",
              cfg.csv_path.c_str());
      return 1;
    }
  }
  auto write_csv = [&](int particles, double avg_ms, double fps) {
    if (!csv.is_open()) return;
    csv << particles << "," << cfg.steps << "," << std::fixed
        << std::setprecision(6) << avg_ms << "," << std::setprecision(3) << fps
        << "\n";
  };

  if (csv.is_open()) csv << "particles,steps,time_per_step_ms,fps\n";

  // Suppress per-case engine logs ([FLUID] Created..., [PHYSICS] Cached...)
  // while measuring so the results table stays clean.
  logger::setLevel(logger::Level::Error);

  printf("%-10s %-16s %-10s\n", "particles", "ms/step", "FPS");
  printf("%-10s %-16s %-10s\n", "----------", "----------------", "----------");

  using Clock = std::chrono::high_resolution_clock;

  for (int count : counts) {
    std::vector<double> per_step_ms;
    per_step_ms.reserve(cfg.runs);

    for (int r = 0; r < cfg.runs; ++r) {
      // Each run builds a completely fresh world and tears it down afterwards
      // (every entity and component is released), and the physics module is
      // fully reset before it points at the new ECS.
      {
        m_eng::ECS world;
        m_eng::Entity cam_entity{};
        CreateWorld(world, cfg.threads, cfg.render, cam_entity);
        PrepareFluid(world, count);

        for (int w = 0; w < cfg.warmup; ++w) {
          m_eng::systems::SimulateFluid(world, cfg.dt);
          if (cfg.render)
            RenderStep(world, cam_entity, cfg, count, w + 1, true, 0.0);
        }

        auto t0 = Clock::now();
        for (int s = 0; s < cfg.steps; ++s) {
          m_eng::systems::SimulateFluid(world, cfg.dt);
          if (cfg.render) {
            double elapsed =
              std::chrono::duration<double>(Clock::now() - t0).count();
            double fps =
              (elapsed > 0.0) ? static_cast<double>(s + 1) / elapsed : 0.0;
            RenderStep(world, cam_entity, cfg, count, s + 1, false, fps);
          }
        }
        auto t1 = Clock::now();

        double total_ms =
          std::chrono::duration<double, std::milli>(t1 - t0).count();
        per_step_ms.push_back(total_ms / static_cast<double>(cfg.steps));
      }  // world destroyed: all ECS state is fully released
    }

    double avg_ms = 0.0;
    for (double ms : per_step_ms) avg_ms += ms;
    avg_ms /= static_cast<double>(per_step_ms.size());

    double fps = 1000.0 / avg_ms;

    printf("%-10d %-16.3f %-10.2f\n", count, avg_ms, fps);
    fflush(stdout);

    write_csv(count, avg_ms, fps);

    // Give the system time to settle before the next particle count.
    if (cfg.settle_sec > 0.0 && count != counts.back()) {
      std::this_thread::sleep_for(
        std::chrono::duration<double>(cfg.settle_sec));
    }
  }

  if (csv.is_open()) {
    csv.flush();
    if (!cfg.csv_path.empty())
      logger::info("Results written to {}", cfg.csv_path);
  }

  if (cfg.render) CloseWindow();

  return 0;
}
