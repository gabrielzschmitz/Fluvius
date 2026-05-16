// app/scenes/density_demo.h
#pragma once

#include "engine/components/camera.h"
#include "engine/components/physics.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"

namespace motrix::engine::components {

struct DensityParticleTag {
  static constexpr std::string_view Name = "DensityParticle";
};

}  // namespace motrix::engine::components

namespace motrix::engine::systems {

inline int grid_columns = 25;
inline int grid_rows = 15;
inline float grid_columns_float = 25.f;
inline float grid_rows_float = 15.f;
inline float grid_smoothing_radius = 125.f;
inline float grid_line_width = 2.f;
inline int pending_columns = -1;
inline int pending_rows = -1;

inline void CreateDensityDemo(ECS& ecs, int cols, int rows) {
  ecs.group_view<components::DensityParticleTag>(
    [&](Entity entity, components::DensityParticleTag&) {
      ecs.destroy_entity(entity);
    });

  float spacingX = static_cast<float>(CANVAS_W) / (cols + 1);
  float spacingY = static_cast<float>(CANVAS_H) / (rows + 1);

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      float x = spacingX * (col + 1);
      float y = spacingY * (row + 1);

      Entity e = ecs.create_entity();
      ecs.add<components::PositionComponent>(e, Vector2{x, y});
      ecs.add<components::CircleComponent>(e, 4.f, 1.f, GRAY);
      ecs.add<components::DensityParticleTag>(e);
    }
  }

  logger::info("[PARTICLE_GRID] Created {}x{} = {} particles", cols, rows,
               cols * rows);
}

inline void RenderDensityDemo(ECS& ecs,
                              const components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  Vector2 center{CANVAS_W / 2.f, CANVAS_H / 2.f};
  if (grid_smoothing_radius > 0) {
    DrawCircleV(center, grid_smoothing_radius + grid_line_width,
                Color{255, 255, 255, 255});
    DrawCircleV(center, grid_smoothing_radius, canvas_background_color());
    DrawCircleGradient(center, grid_smoothing_radius, Color{255, 255, 255, 25},
                       Color{255, 255, 255, 1});
  }

  ecs.group_view<components::DensityParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::DensityParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float dist = Vector2Distance(pos.position, center);
      Color particleColor;

      if (dist <= grid_smoothing_radius) {
        float t = 1.0f - (dist / grid_smoothing_radius);
        unsigned char c = static_cast<unsigned char>(150 + 105 * t);
        particleColor = Color{c, c, c, 255};

        Vector2 toCenter = Vector2Subtract(center, pos.position);
        float arrowRadius = circ.radius * 1.5f;
        systems::RenderArrow(pos.position, toCenter, arrowRadius, particleColor,
                             0.3f);
      }
    });
  ecs.group_view<components::DensityParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::DensityParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float dist = Vector2Distance(pos.position, center);
      Color particleColor;

      if (dist <= grid_smoothing_radius) {
        float t = 1.0f - (dist / grid_smoothing_radius);
        unsigned char c = static_cast<unsigned char>(150 + 105 * t);
        particleColor = Color{c, c, c, 255};
      } else
        particleColor = GRAY;

      float radius = (dist < 1.0f) ? circ.radius * 1.5f : circ.radius;
      DrawCircleV(pos.position, radius, particleColor);
    });

  EndMode2D();
}

}  // namespace motrix::engine::systems

#include "app/scenes/density_demo.h"
#include "engine/components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"

namespace motrix::entities {

inline void CreateDensityDemoUI(engine::ECS& ecs) {
  engine::Entity window = ecs.create_entity();

  ecs.add<engine::components::UIWindowComponent>(
    window, engine::components::UIWindowComponent{
              {20.f, 20.f}, 250.f, 130.f, "Density Controls"});
  ecs.get<engine::components::UIWindowComponent>(window).auto_height = true;

  engine::Entity count_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    count_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(count_slider);
  ecs.add<engine::components::UISliderComponent>(
    count_slider,
    engine::components::UISliderComponent{
      "Columns", &motrix::engine::systems::grid_columns_float, 2.f, 50.f, 1.f,
      [](float value) {
        motrix::engine::systems::grid_columns = static_cast<int>(value);
        motrix::engine::systems::grid_columns_float = value;
        motrix::engine::systems::pending_columns = static_cast<int>(value);
      }});
  ecs.add<engine::components::UITooltipComponent>(
    count_slider, "Number of columns in the grid.");

  engine::Entity rows_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    rows_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(rows_slider);
  ecs.add<engine::components::UISliderComponent>(
    rows_slider,
    engine::components::UISliderComponent{
      "Rows", &motrix::engine::systems::grid_rows_float, 2.f, 20.f, 1.f,
      [](float value) {
        motrix::engine::systems::grid_rows = static_cast<int>(value);
        motrix::engine::systems::grid_rows_float = value;
        motrix::engine::systems::pending_rows = static_cast<int>(value);
      }});
  ecs.add<engine::components::UITooltipComponent>(
    rows_slider, "Number of rows in the grid.");

  engine::Entity radius_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    radius_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(radius_slider);
  ecs.add<engine::components::UISliderComponent>(
    radius_slider, engine::components::UISliderComponent{
                     "Radius", &motrix::engine::systems::grid_smoothing_radius,
                     0.f, 320.f, 1.f, [](float value) {
                       motrix::engine::systems::grid_smoothing_radius = value;
                     }});
  ecs.add<engine::components::UITooltipComponent>(
    radius_slider, "Smoothing radius around center particle.");
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

inline void InitDensityDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvasWithHandles(state.ecs, false);
  m_eng::systems::CreateDensityDemo(state.ecs, m_eng::systems::grid_columns,
                                    m_eng::systems::grid_rows);
  m_ett::CreateDensityDemoUI(state.ecs);
}

inline void UpdateDensityDemo(AppState& state, float dt) {
  (void)dt;

  bool needsRebuild = false;
  if (m_eng::systems::pending_columns > 0) {
    m_eng::systems::grid_columns = m_eng::systems::pending_columns;
    m_eng::systems::pending_columns = -1;
    needsRebuild = true;
  }
  if (m_eng::systems::pending_rows > 0) {
    m_eng::systems::grid_rows = m_eng::systems::pending_rows;
    m_eng::systems::pending_rows = -1;
    needsRebuild = true;
  }

  if (needsRebuild) {
    m_eng::systems::CreateDensityDemo(state.ecs, m_eng::systems::grid_columns,
                                      m_eng::systems::grid_rows);
  }

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderDensityDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  m_eng::systems::RenderDensityDemo(state.ecs, cam);
}

}  // namespace motrix::app
