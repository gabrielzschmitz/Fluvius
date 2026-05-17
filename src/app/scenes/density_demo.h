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

inline int grid_columns = 13;
inline int grid_rows = 7;
inline float grid_columns_float = 13.f;
inline float grid_rows_float = 7.f;
inline float grid_smoothing_radius = 150.f;
inline float grid_line_width = 2.f;
inline float grid_particle_mass = 300.0f;
inline float grid_font_size = 13.0f;
inline float grid_particle_size = 11.0f;
inline float grid_arrow_radius = 6.0f;
inline int pending_columns = -1;
inline int pending_rows = -1;

inline float DensityPoly6Kernel(float r2, float h) {
  float h2 = h * h;

  if (r2 > h2) return 0.0f;

  float diff = h2 - r2;

  float coeff = 315.0f / (64.0f * PI * pow(h, 9));

  return coeff * pow(diff, 3);
}

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
      ecs.add<components::CircleComponent>(e, grid_particle_size, 1.f, GRAY);
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
  float h = grid_smoothing_radius;
  float h2 = h * h;
  float m = grid_particle_mass;

  if (h > 0) {
    DrawCircleV(center, h + grid_line_width, Color{255, 255, 255, 255});
    DrawCircleV(center, h, canvas_background_color());
    DrawCircleGradient(center, h, Color{255, 255, 255, 25},
                       Color{255, 255, 255, 1});
  }

  std::vector<std::pair<Vector2, float>> contributions;

  ecs.group_view<components::DensityParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::DensityParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float dx = pos.position.x - center.x;
      float dy = pos.position.y - center.y;
      float r2 = dx * dx + dy * dy;
      float dist = sqrtf(r2);

      if (dist > 1.0f && r2 <= h2) {
        float kernelVal = DensityPoly6Kernel(r2, h);
        float contribution = m * kernelVal;
        contributions.push_back({pos.position, contribution});
      }
    });

  float totalDensity = 0.0f;
  for (auto& c : contributions) {
    totalDensity += c.second;
  }
  float totalDensityScaled = totalDensity * 10000.0f;

  float maxContribution = 0.0f;
  for (auto& c : contributions) {
    if (c.second > maxContribution) maxContribution = c.second;
  }

  for (auto& contrib : contributions) {
    float intensity =
      (maxContribution > 0.0f) ? (contrib.second / maxContribution) : 0.0f;
    unsigned char brightness =
      static_cast<unsigned char>(150 + 105 * intensity);
    Color arrowColor = Color{brightness, brightness, brightness, 255};

    Vector2 toCenter = Vector2Subtract(center, contrib.first);
    float dist = 1.0f - Vector2Distance(contrib.first, center);
    systems::RenderArrow(contrib.first, toCenter, grid_arrow_radius, arrowColor,
                         0.3f);
  }

  ecs.group_view<components::DensityParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::DensityParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float dx = pos.position.x - center.x;
      float dy = pos.position.y - center.y;
      float r2 = dx * dx + dy * dy;
      float dist = sqrtf(r2);
      Color particleColor;

      if (r2 <= h2 && dist > 1.0f) {
        float kernelVal = DensityPoly6Kernel(r2, h);
        float contribution = m * kernelVal;
        float intensity =
          (maxContribution > 0.0f) ? (contribution / maxContribution) : 0.0f;
        unsigned char c = static_cast<unsigned char>(150 + 105 * intensity);
        particleColor = Color{c, c, c, 255};
      } else if (dist <= 1.0f) {
        particleColor = WHITE;
      } else {
        particleColor = GRAY;
      }

      float baseRadius = grid_particle_size;
      float radius = (dist < 1.0f) ? baseRadius * 1.5f : baseRadius;
      DrawCircleV(pos.position, radius, particleColor);

      if (r2 <= h2 && dist > 1.0f) {
        float kernelVal = DensityPoly6Kernel(r2, h);
        float contribution = m * kernelVal;
        float fraction =
          (totalDensity > 0.0f) ? (contribution / totalDensity) * 100.0f : 0.0f;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.0f%%", fraction);
        Vector2 textSize =
          MeasureTextEx(defaultFont, buffer, grid_font_size, 0.0f);
        DrawTextEx(defaultFont, buffer,
                   Vector2{pos.position.x - textSize.x / 2.0f,
                           pos.position.y - (textSize.y / 2.0f) + 1.0f},
                   grid_font_size, 0.0f, particleColor);
      } else if (dist <= 1.0f) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.0f", totalDensityScaled);
        Vector2 textSize =
          MeasureTextEx(defaultFont, buffer, grid_font_size * 1.5f, 0.0f);
        DrawTextEx(defaultFont, buffer,
                   Vector2{pos.position.x - textSize.x / 2.0f,
                           pos.position.y - (textSize.y / 2.0f) + 1.0f},
                   grid_font_size * 1.5f, 0.0f, RED);
      }
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
              {20.f, 20.f}, 250.f, 250.f, "Density Controls"});
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
                     0.f, 320.f, 1.f, nullptr});
  ecs.add<engine::components::UITooltipComponent>(
    radius_slider, "Smoothing radius around center particle.");

  engine::Entity font_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    font_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(font_slider);
  ecs.add<engine::components::UISliderComponent>(
    font_slider, engine::components::UISliderComponent{
                   "Font Size", &motrix::engine::systems::grid_font_size, 6.f,
                   20.f, 1.f, nullptr});
  ecs.add<engine::components::UITooltipComponent>(font_slider,
                                                  "Text font size.");

  engine::Entity mass_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    mass_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(mass_slider);
  ecs.add<engine::components::UISliderComponent>(
    mass_slider, engine::components::UISliderComponent{
                   "Mass", &motrix::engine::systems::grid_particle_mass, 100.f,
                   10000.f, 100.f, nullptr});
  ecs.add<engine::components::UITooltipComponent>(
    mass_slider, "Particle mass for density calculation.");

  engine::Entity size_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    size_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(size_slider);
  ecs.add<engine::components::UISliderComponent>(
    size_slider,
    engine::components::UISliderComponent{
      "Particle Size", &motrix::engine::systems::grid_particle_size, 2.f, 15.f,
      0.5f, nullptr});
  ecs.add<engine::components::UITooltipComponent>(size_slider,
                                                  "Particle circle radius.");

  engine::Entity arrow_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    arrow_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(arrow_slider);
  ecs.add<engine::components::UISliderComponent>(
    arrow_slider, engine::components::UISliderComponent{
                    "Arrow Radius", &motrix::engine::systems::grid_arrow_radius,
                    0.f, 20.f, 0.5f, nullptr});
  ecs.add<engine::components::UITooltipComponent>(
    arrow_slider, "Arrow start radius from particle center.");
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
