// app/app.cpp
#include "app/app.h"

#include <thread>

#include "app/app_state.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"
#include "engine/systems/ui.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"
#include "entities/ui.h"
#include "raylib.h"
#include "resource_dir.h"

namespace m_app = motrix::app;
namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

static void InitApp(m_app::AppState& state) {
  InitWindow(CANVAS_W * SCALE, CANVAS_H * SCALE, "Fluvius");

  SearchAndSetResourceDir("resources");

  defaultFont = LoadFont("fonts/simple-font.png");

  state.cameraEntity = m_ett::CreateCamera(state.ecs);

  state.canvasEntity = m_ett::CreateCanvas(state.ecs);

  m_ett::CreateFluid(state.ecs, PARTICLE_NUMBER,
                     motrix::entities::create_centered);
  m_ett::CreateUI(state.ecs);

  int num_cores = std::thread::hardware_concurrency();
  if (num_cores == 0) num_cores = 8;
  m_eng::systems::InitThreads(num_cores, state.ecs);

  state.ecs.print_entities(logger::Level::Debug, {"Circle"});
}

static void UpdateApp(m_app::AppState& state, float dt) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  m_eng::systems::SimulateFluid(state.ecs, dt);

  m_eng::systems::UpdateSelectionInput(state.ecs, cam);
  m_eng::systems::UpdatePathInput(state.ecs, cam);

  m_eng::systems::UpdateSelectionDensity(state.ecs);
  m_eng::systems::UpdateDensityText(state.ecs);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

static void RenderApp(m_app::AppState& state) {
  BeginDrawing();

  ClearBackground({1, 87, 87, 255});

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  BeginMode2D(cam.camera);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  if (motrix::entities::render_fluid_filled)
    m_eng::systems::RenderFluidFilled(state.ecs, cam);

  if (motrix::entities::render_pressure_field)
    m_eng::systems::RenderPressureField(state.ecs, cam);

  if (motrix::entities::render_fluid_particles)
    m_eng::systems::RenderFluid(state.ecs, cam);

  m_eng::systems::RenderMouseSelectionCircle(cam);
  m_eng::systems::RenderUserPath(state.ecs, cam);

  EndMode2D();

  m_eng::systems::RenderUI(state.ecs);

  DrawTextEx(defaultFont, TextFormat("FPS: %d", GetFPS()), Vector2{10, 10},
             defaultFont.baseSize * cam.uiScale * 1.5f, 1,
             Color{246, 120, 232, 255});

  EndDrawing();
}

int RunApp(int argc, char** argv) {
  (void)argc;
  (void)argv;

  m_app::AppState state;

  InitApp(state);

  while (!WindowShouldClose()) {
    const float dt = GetFrameTime();

    UpdateApp(state, dt);
    RenderApp(state);
  }

  UnloadFont(defaultFont);
  CloseWindow();

  return 0;
}
