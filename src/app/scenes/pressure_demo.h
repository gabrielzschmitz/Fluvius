// app/scenes/pressure_demo.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/fluid.h"

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

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      canvas.position = {CANVAS_W / 2.f, CANVAS_H / 2.f};
      canvas.size = {CANVAS_W, CANVAS_H};
      canvas.rotation = 0.f;
      canvas.half_extents = {CANVAS_W / 2.f, CANVAS_H / 2.f};
      canvas.rotation_dirty = true;
    });

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
  Entity window = ecs.create_entity();
  ecs.add<components::UIWindowComponent>(
    window,
    components::UIWindowComponent{{20.f, 20.f}, 220.f, 160.f, "Pressure Demo"});
  ecs.get<components::UIWindowComponent>(window).auto_height = true;

  Entity smoothing_slider = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    smoothing_slider, components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<components::UIResolvedRectComponent>(smoothing_slider);
  ecs.add<components::UISliderComponent>(
    smoothing_slider,
    components::UISliderComponent("Smoothing", &smoothing_radius, 10.f, 100.f,
                                  1.f, nullptr));
  ecs.add<components::UITooltipComponent>(smoothing_slider,
                                          "Smoothing radius (h).");

  Entity target_density_slider = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    target_density_slider,
    components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<components::UIResolvedRectComponent>(target_density_slider);
  ecs.add<components::UISliderComponent>(
    target_density_slider,
    components::UISliderComponent("Target Density", &target_density, 0.0001f,
                                  0.001f, 0.00001f, nullptr));
  ecs.add<components::UITooltipComponent>(target_density_slider,
                                          "Target fluid density.");

  Entity pressure_slider = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    pressure_slider, components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<components::UIResolvedRectComponent>(pressure_slider);
  ecs.add<components::UISliderComponent>(
    pressure_slider,
    components::UISliderComponent("Pressure", &pressure_multiplier, 50.f, 500.f,
                                  1.f, nullptr));
  ecs.add<components::UITooltipComponent>(pressure_slider,
                                          "Pressure multiplier.");

  Entity size_slider = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    size_slider, components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<components::UIResolvedRectComponent>(size_slider);
  ecs.add<components::UISliderComponent>(
    size_slider, components::UISliderComponent("Size", &particle_size, 1.f,
                                               10.f, 0.1f, nullptr));
  ecs.add<components::UITooltipComponent>(size_slider, "Particle size.");

  Entity speed_slider = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    speed_slider, components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<components::UIResolvedRectComponent>(speed_slider);
  ecs.add<components::UISliderComponent>(
    speed_slider, components::UISliderComponent("Speed", &sim_speed, 0.1f, 10.f,
                                                0.1f, nullptr));
  ecs.add<components::UITooltipComponent>(speed_slider, "Simulation speed.");

  Entity pause_checkbox = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    pause_checkbox, components::UILayoutChildComponent{window, -1.f, 25.f});
  ecs.add<components::UIResolvedRectComponent>(pause_checkbox);
  ecs.add<components::UICheckboxComponent>(
    pause_checkbox, components::UICheckboxComponent("Pause", &is_paused));
  ecs.add<components::UITooltipComponent>(pause_checkbox, "Pause simulation.");

  Entity reset_button = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    reset_button, components::UILayoutChildComponent{window, -1.f, 25.f});
  ecs.add<components::UIResolvedRectComponent>(reset_button);
  ecs.add<components::UIButtonComponent>(
    reset_button, components::UIButtonComponent{
                    "Reset", false, [&ecs]() { ResetPressureDemo(ecs); }});
  ecs.add<components::UITooltipComponent>(reset_button,
                                          "Reset to default values.");

  Entity pf_checkbox = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    pf_checkbox, components::UILayoutChildComponent{window, -1.f, 25.f});
  ecs.add<components::UIResolvedRectComponent>(pf_checkbox);
  ecs.add<components::UICheckboxComponent>(
    pf_checkbox,
    components::UICheckboxComponent("Pressure Field", &render_pressure_field));
  ecs.add<components::UITooltipComponent>(pf_checkbox,
                                          "Render pressure field.");

  Entity particles_checkbox = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    particles_checkbox, components::UILayoutChildComponent{window, -1.f, 25.f});
  ecs.add<components::UIResolvedRectComponent>(particles_checkbox);
  ecs.add<components::UICheckboxComponent>(
    particles_checkbox,
    components::UICheckboxComponent("Particles", &render_fluid_particles));
  ecs.add<components::UITooltipComponent>(particles_checkbox,
                                          "Render fluid particles.");

  Entity velocity_checkbox = ecs.create_entity();
  ecs.add<components::UILayoutChildComponent>(
    velocity_checkbox, components::UILayoutChildComponent{window, -1.f, 25.f});
  ecs.add<components::UIResolvedRectComponent>(velocity_checkbox);
  ecs.add<components::UICheckboxComponent>(
    velocity_checkbox, components::UICheckboxComponent(
                         "Velocity Vectors", &render_particle_velocity));
  ecs.add<components::UITooltipComponent>(velocity_checkbox,
                                          "Render velocity vectors.");
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
