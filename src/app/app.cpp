// app/app.cpp
#include "app/app.h"

#include <cstdio>
#include <thread>

#include "app/app_state.h"
#include "app/scene_registry.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "engine/systems/physics.h"
#include "engine/systems/ui.h"
#include "entities/simulation.h"
#include "raylib.h"
#include "resource_dir.h"

static bool SHOW_FPS = true;

namespace m_app = motrix::app;
namespace m_eng = motrix::engine;

static void ParseCLIFlags(int argc, char** argv, m_app::SceneType& sceneType,
                          bool& print_and_exit) {
  auto is_valid_scene_name = [](const char* name) -> bool {
    const auto& entry = m_app::Scenes::SCENE_NAMES;
    for (size_t i = 0; i < sizeof(entry) / sizeof(entry[0]); ++i) {
      if (strcmp(entry[i].name, name) == 0) return true;
    }
    return false;
  };

  auto print_scene_list = []() {
    printf("Available scenes:\n");
    const auto& entry = m_app::Scenes::SCENE_NAMES;
    for (size_t i = 0; i < sizeof(entry) / sizeof(entry[0]); ++i) {
      printf("  - %s\n", entry[i].name);
    }
  };

  bool scene_set = false;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];

    if (strcmp(arg, "-s") == 0) {
      if (i + 1 < argc) {
        const char* next = argv[i + 1];
        if (next && next[0] == '-') {
          logger::error(
            "[CLI] Invalid -s value '{}', expects scene name (not another "
            "flag)",
            next);
          continue;
        }
        if (next && is_valid_scene_name(next)) {
          sceneType = m_app::Scenes::FindByName(next);
          scene_set = true;
          logger::info("[CLI] Using scene: '{}'", next);
          ++i;
        } else if (next) {
          logger::error("[CLI] Unknown scene '{}'", next);
        }
      } else {
        logger::warn("[CLI] -s requires a value");
      }
    } else if (strcmp(arg, "--scene") == 0) {
      if (i + 1 < argc) {
        const char* next = argv[i + 1];
        if (next && next[0] == '-') {
          logger::error(
            "[CLI] Invalid --scene value '{}', expects scene name (not another "
            "flag)",
            next);
          continue;
        }
        if (next && is_valid_scene_name(next)) {
          sceneType = m_app::Scenes::FindByName(next);
          scene_set = true;
          logger::info("[CLI] Using scene: '{}'", next);
          ++i;
        } else if (next) {
          logger::error("[CLI] Unknown scene '{}'", next);
        }
      } else {
        logger::warn("[CLI] --scene requires a value");
      }
    } else if (strncmp(arg, "--scene=", 8) == 0) {
      const char* value = arg + 8;
      if (is_valid_scene_name(value)) {
        sceneType = m_app::Scenes::FindByName(value);
        scene_set = true;
        logger::info("[CLI] Using scene: '{}'", value);
      } else {
        logger::error("[CLI] Unknown scene '{}'", value);
      }
    } else if (strncmp(arg, "-s", 2) == 0 && strlen(arg) > 2) {
      const char* value = arg + 2;
      if (value[0] == '-') {
        logger::error("[CLI] Invalid flag '{}', did you mean --scene?", arg);
      } else if (is_valid_scene_name(value)) {
        sceneType = m_app::Scenes::FindByName(value);
        scene_set = true;
        logger::info("[CLI] Using scene: '{}'", value);
      } else {
        logger::error("[CLI] Unknown scene '{}'", value);
      }
    } else if (strcmp(arg, "--list-scenes") == 0 || strcmp(arg, "-ls") == 0) {
      print_scene_list();
      print_and_exit = true;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      printf("Usage: fluvius [OPTIONS]\n");
      printf("  -s, --scene <name>    Specify scene (fluid, kernel, "
             "smoothing, density, pressure, viscosity)\n");
      printf("  -ls, --list-scenes    List available scenes and exit\n");
      printf("  -h, --help            Show this help message\n");
      print_and_exit = true;
    } else if (arg[0] == '-') {
      logger::warn("[CLI] Unknown option: '{}'", arg);
    }
  }

  if (!scene_set && !print_and_exit) {
    logger::info("[CLI] No scene specified, using default (fluid)");
  }
}

static void InitApp(m_app::AppState& state, m_app::SceneType sceneType) {
  InitWindow(CANVAS_W * SCALE, CANVAS_H * SCALE, "Fluvius");

  SearchAndSetResourceDir("resources");

  defaultFont = LoadFont("fonts/simple-font.png");

  state.currentScene = sceneType;

  int num_cores = std::thread::hardware_concurrency();
  m_eng::systems::InitThreads(num_cores, state.ecs);

  motrix::entities::RegisterSimulationRoot(state.ecs);

  m_app::Scenes::Get(state.currentScene).init(state);

#if defined(LOG_LEVEL_DEBUG)
  state.ecs.print_entities(logger::Level::Debug, {"Circle"});
#endif
}

static void UpdateApp(m_app::AppState& state, float dt) {
  m_app::Scenes::Get(state.currentScene).update(state, dt);
}

static void RenderApp(m_app::AppState& state) {
  BeginDrawing();

  ClearBackground({1, 87, 87, 255});

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  BeginMode2D(cam.camera);

  m_app::Scenes::Get(state.currentScene).render(state);

  EndMode2D();

  m_eng::systems::RenderUI(state.ecs, state.currentScene);

  if (IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_F)) SHOW_FPS = !SHOW_FPS;

  if (SHOW_FPS)
    DrawTextEx(defaultFont, TextFormat("FPS: %d", GetFPS()), Vector2{10, 10},
               defaultFont.baseSize * cam.uiScale * 1.5f, 1,
               Color{246, 120, 232, 255});

  EndDrawing();
}

int RunApp(int argc, char** argv) {
  m_app::SceneType sceneType = m_app::SceneType::FLUID_SIM;
  bool print_and_exit = false;
  ParseCLIFlags(argc, argv, sceneType, print_and_exit);

  if (print_and_exit) return 0;

  m_app::AppState state;

  InitApp(state, sceneType);

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    UpdateApp(state, dt);
    RenderApp(state);
  }

  UnloadFont(defaultFont);
  CloseWindow();

  return 0;
}
