// app/scenes/smoothing_demo.h
#pragma once

#include "engine/components/camera.h"
#include "engine/components/physics.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/canvas.h"

namespace motrix::engine::components {

struct SmoothingParticleTag {
  static constexpr std::string_view Name = "SmoothingParticle";
};

}  // namespace motrix::engine::components

namespace motrix::engine::systems {

inline float smoothing_radius = 80.f;
inline float strength = 200.f;

inline void CreateSmoothingDemo(ECS& ecs) {
  Entity e = ecs.create_entity();
  ecs.add<components::PositionComponent>(
    e, Vector2{CANVAS_W / 2.f, CANVAS_H / 2.f});
  ecs.add<components::CircleComponent>(e, 6.f, 1.f, WHITE);
  ecs.add<components::SmoothingParticleTag>(e);
  logger::info("[SMOOTHING] Created particle at center");
}

inline void RenderSmoothing(ECS& ecs, const components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::SmoothingParticleTag,
                 components::PositionComponent, components::CircleComponent>(
    [&](Entity, components::SmoothingParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float line_width = 2.f;
      DrawCircleV(pos.position, smoothing_radius + line_width,
                  Color{255, 255, 255, 255});
      DrawCircleV(pos.position, smoothing_radius, canvas_background_color());
      DrawCircleGradient(
        pos.position, smoothing_radius, Color{1, 64, 126, 255},
        Color{1, 64, 126, static_cast<unsigned char>(strength)});
      Vector2 edge_pos{pos.position.x + smoothing_radius, pos.position.y};
      DrawLineEx(pos.position, edge_pos, line_width, Color{255, 255, 255, 255});
      DrawCircleV(pos.position, circ.radius, circ.color);
    });

  EndMode2D();
}

}  // namespace motrix::engine::systems

#include "app/scenes/smoothing_demo.h"
#include "engine/components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"

namespace motrix::entities {

inline void CreateSmoothingDemoUI(engine::ECS& ecs) {
  engine::Entity window = ecs.create_entity();

  ecs.add<engine::components::UIWindowComponent>(
    window, engine::components::UIWindowComponent{
              {20.f, 20.f}, 250.f, 150.f, "Smoothing Controls"});
  ecs.get<engine::components::UIWindowComponent>(window).auto_height = false;

  engine::Entity radius_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    radius_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 10.f});
  ecs.add<engine::components::UIResolvedRectComponent>(radius_slider);
  ecs.add<engine::components::UISliderComponent>(
    radius_slider,
    engine::components::UISliderComponent{
      "Radius", &motrix::engine::systems::smoothing_radius, 0.f, 175.f, 1.f,
      [](float value) { smoothing_radius = value; }});
  ecs.add<engine::components::UITooltipComponent>(
    radius_slider, "Controls the influence radius around the particle.");

  engine::Entity strength_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    strength_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 10.f});
  ecs.add<engine::components::UIResolvedRectComponent>(strength_slider);
  ecs.add<engine::components::UISliderComponent>(
    strength_slider,
    engine::components::UISliderComponent{
      "Strength", &motrix::engine::systems::strength, 0.f, 255.f, 1.f,
      [](float value) { motrix::engine::systems::strength = value; }});
  ecs.add<engine::components::UITooltipComponent>(
    strength_slider,
    "Controls the strengh of influence from smoothing radius.");

  logger::info("[GUI] Created window '{}' (entity:{}:{})",
               ecs.get<engine::components::UIWindowComponent>(window).title,
               window.index, window.version);
}

}  // namespace motrix::entities

#include "app/app_state.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "entities/camera.h"
#include "entities/canvas.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline void InitSmoothingDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvas(state.ecs);
  m_eng::systems::CreateSmoothingDemo(state.ecs);
  m_ett::CreateSmoothingDemoUI(state.ecs);
}

inline void UpdateSmoothingDemo(AppState& state, float dt) {
  (void)dt;

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderSmoothingDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  m_eng::systems::RenderSmoothing(state.ecs, cam);
}

}  // namespace motrix::app
