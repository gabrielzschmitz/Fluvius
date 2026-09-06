// app/scenes/fluid_sim.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/ecs/ecs.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/fluid_render.h"
#include "engine/systems/physics.h"
#include "engine/systems/ui.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"
#include "entities/simulation.h"
#include "entities/ui.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline void InitFluidSim(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvas(state.ecs);
  m_ett::CreateFluid(state.ecs, m_ett::Simulation(state.ecs).particle_count,
                     m_ett::Simulation(state.ecs).create_centered);
  m_ett::CreateUI(state.ecs);
}

inline void UpdateFluidSim(AppState& state, float dt) {
  auto& sim = m_ett::Simulation(state.ecs);

  if (sim.needs_reset) {
    m_ett::ResetFluid(state.ecs);
    m_ett::ResetCanvasTransform(state.ecs);
  }

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  static bool key_p_was_down = false;
  if (IsKeyDown(KEY_P) && !key_p_was_down) {
    sim.is_paused = !sim.is_paused;
  }
  key_p_was_down = IsKeyDown(KEY_P);

  if (sim.is_paused) {
    if (IsKeyDown(KEY_LEFT)) {
      m_eng::systems::SimulateFluid(state.ecs, -dt * 4.0f, true);
    }
    if (IsKeyDown(KEY_RIGHT)) {
      m_eng::systems::SimulateFluid(state.ecs, dt * 4.0f, true);
    }
    m_eng::systems::UpdateCamera2D(state.ecs);
    return;
  }

  m_eng::systems::SimulateFluid(state.ecs, dt);

  m_eng::systems::UpdateSelectionInput(state.ecs, cam);
  m_eng::systems::UpdatePathInput(state.ecs, cam);

  m_eng::systems::UpdateSelectionDensity(state.ecs);
  m_eng::systems::UpdateDensityText(state.ecs);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderFluidSim(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  auto& sim = m_ett::Simulation(state.ecs);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  if (sim.render_fluid_filled) m_eng::systems::RenderFluidFilled(state.ecs, cam);

  if (sim.render_pressure_field)
    m_eng::systems::RenderPressureField(state.ecs, cam);

  if (sim.render_fluid_particles)
    m_eng::systems::RenderFluid(state.ecs, cam);

  m_eng::systems::RenderMouseSelectionCircle(state.ecs, cam);
  m_eng::systems::RenderUserPath(state.ecs, cam);
}

}  // namespace motrix::app
