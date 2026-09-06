// app/scenes/pressure_demo.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/fluid_render.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"
#include "entities/ui.h"

namespace {

using namespace motrix::engine;
using namespace motrix::entities;

inline void ResetPressureDemo(motrix::engine::ECS& ecs) {
  smoothing_radius = 25.0f;
  target_density = 0.000390f;
  pressure_multiplier = 250.0f;
  particle_size = 4.0f;
  sim_speed = 5.0f;
  is_paused = true;
  render_pressure_field = true;
  render_fluid_particles = true;
  render_particle_velocity = true;

  motrix::entities::ResetCanvasTransform(ecs);

  ecs.group_view<components::CircleComponent>(
    [&](Entity e, components::CircleComponent& circ) {
      ecs.destroy_entity(e);
    });
  ecs.group_view<components::PositionComponent>(
    [&](Entity e, components::PositionComponent& pos) {
      (void)pos;
      ecs.destroy_entity(e);
    });
  ecs.group_view<components::VelocityComponent>(
    [&](Entity e, components::VelocityComponent& vel) {
      (void)vel;
      ecs.destroy_entity(e);
    });

  CreateFluid(ecs, 500, true);
}

inline void CreatePressureDemoUI(motrix::engine::ECS& ecs) {
  Entity window = AddWindow(ecs, {20.f, 20.f}, 220.f, 160.f, "Pressure Demo");

  AddSlider(ecs, window, INVALID_ENTITY, "Smoothing", &smoothing_radius, 10.f,
            100.f, 1.f, nullptr, "Smoothing radius (h).");
  AddSlider(ecs, window, INVALID_ENTITY, "Target Density", &target_density,
            0.0001f, 0.001f, 0.00001f, nullptr, "Target fluid density.");
  AddSlider(ecs, window, INVALID_ENTITY, "Pressure", &pressure_multiplier, 50.f,
            500.f, 1.f, nullptr, "Pressure multiplier.");
  AddSlider(ecs, window, INVALID_ENTITY, "Size", &particle_size, 1.f, 10.f,
            0.1f, nullptr, "Particle size.");
  AddSlider(ecs, window, INVALID_ENTITY, "Speed", &sim_speed, 0.1f, 10.f, 0.1f,
            nullptr, "Simulation speed.");

  AddCheckbox(ecs, window, INVALID_ENTITY, "Pause", &is_paused, {}, {}, 0.f,
              25.f);
  AddButton(ecs, window, INVALID_ENTITY, "Reset",
            [&ecs]() { ResetPressureDemo(ecs); }, "Reset to default values.",
            -1.f, 25.f);
  AddCheckbox(ecs, window, INVALID_ENTITY, "Pressure Field",
              &render_pressure_field, {}, "Render pressure field.", 0.f, 25.f);
  AddCheckbox(ecs, window, INVALID_ENTITY, "Particles", &render_fluid_particles,
              {}, "Render fluid particles.", 0.f, 25.f);
  AddCheckbox(ecs, window, INVALID_ENTITY, "Velocity Vectors",
              &render_particle_velocity, {}, "Render velocity vectors.", 0.f,
              25.f);
}

}  // anonymous namespace

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline void InitPressureDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvas(state.ecs);

  m_ett::viscosity = 0.0f;
  m_ett::surface_tension = 0.0f;
  m_ett::velocity_damping = 1.000f;
  m_ett::gravity = 0.0f;

  m_ett::smoothing_radius = 25.0f;
  m_ett::target_density = 0.000390f;
  m_ett::pressure_multiplier = 250.0f;
  m_ett::particle_size = 4.0f;
  m_ett::sim_speed = 5.0f;
  m_ett::is_paused = true;

  m_ett::render_pressure_field = true;
  m_ett::render_fluid_particles = true;
  m_ett::render_particle_velocity = true;

  m_ett::is_paused = true;

  ResetPressureDemo(state.ecs);
  CreatePressureDemoUI(state.ecs);
}

inline void UpdatePressureDemo(AppState& state, float dt) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  static bool key_p_was_down = false;
  if (IsKeyDown(KEY_P) && !key_p_was_down) {
    entities::is_paused = !entities::is_paused;
  }
  key_p_was_down = IsKeyDown(KEY_P);

  if (entities::is_paused) {
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

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderPressureDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  if (m_ett::render_pressure_field)
    m_eng::systems::RenderPressureField(state.ecs, cam);

  if (m_ett::render_fluid_particles)
    m_eng::systems::RenderFluid(state.ecs, cam);
}

}  // namespace motrix::app
