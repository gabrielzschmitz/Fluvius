// app/scenes/fluid_sim.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"
#include "entities/ui.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline void InitFluidSim(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvas(state.ecs);
  m_ett::CreateFluid(state.ecs, PARTICLE_NUMBER, m_ett::create_centered);
  m_ett::CreateUI(state.ecs);
}

inline void UpdateFluidSim(AppState& state, float dt) {
  if (m_ett::needs_reset) {
    m_ett::ResetFluid(state.ecs);
    m_ett::needs_reset = false;
  }

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

inline void RenderFluidSim(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  if (m_ett::render_fluid_filled)
    m_eng::systems::RenderFluidFilled(state.ecs, cam);

  if (m_ett::render_pressure_field)
    m_eng::systems::RenderPressureField(state.ecs, cam);

  if (m_ett::render_fluid_particles)
    m_eng::systems::RenderFluid(state.ecs, cam);

  m_eng::systems::RenderMouseSelectionCircle(cam);
  m_eng::systems::RenderUserPath(state.ecs, cam);
}

}  // namespace motrix::app
