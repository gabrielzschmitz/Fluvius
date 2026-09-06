// engine/systems/fluid_render.h
#pragma once

#include <algorithm>
#include <vector>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/canvas.h"
#include "../ecs/ecs.h"
#include "../systems/canvas.h"
#include "../systems/sph_kernels.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Pressure Color
 * ============================================================================
 */

inline Color PressureToColor(float pressure,
                             const components::SimulationComponent& sim) {
  float max_pressure = sim.target_density * sim.pressure_multiplier;

  if (max_pressure <= 0.f) return sim.pressure_mid_color;

  float scale = Clamp(pressure / max_pressure, -1.f, 1.f);

  auto lerp = [](unsigned char a, unsigned char b, float t) {
    return static_cast<unsigned char>(a + (b - a) * t);
  };

  if (scale < 0.f) {
    float t = scale + 1.f;

    return {
      lerp(sim.pressure_low_color.r, sim.pressure_mid_color.r, t),
      lerp(sim.pressure_low_color.g, sim.pressure_mid_color.g, t),
      lerp(sim.pressure_low_color.b, sim.pressure_mid_color.b, t),
      130};
  }

  return {lerp(sim.pressure_mid_color.r, sim.pressure_high_color.r, scale),
          lerp(sim.pressure_mid_color.g, sim.pressure_high_color.g, scale),
          lerp(sim.pressure_mid_color.b, sim.pressure_high_color.b, scale),
          130};
}

/**
 * ============================================================================
 * Rendering
 * ============================================================================
 */

inline void RenderArrow(Vector2 center, Vector2 vector, float radius,
                        Color color, float scale = 1.0f) {
  float speed = Vector2Length(vector);

  if (speed <= 0.001f) return;

  Vector2 dir = Vector2Normalize(vector);

  Vector2 start = {center.x + dir.x * radius, center.y + dir.y * radius};

  float arrow_length = radius + speed * scale;

  Vector2 end = {start.x + dir.x * arrow_length,
                 start.y + dir.y * arrow_length};

  float head_size = radius * 0.8f;
  float thickness = radius * 0.5f;

  DrawLineEx(start, end, thickness, color);

  Vector2 left = {end.x - dir.x * head_size + dir.y * head_size * 0.5f,
                  end.y - dir.y * head_size - dir.x * head_size * 0.5f};

  Vector2 right = {end.x - dir.x * head_size - dir.y * head_size * 0.5f,
                   end.y - dir.y * head_size + dir.x * head_size * 0.5f};

  DrawLineEx(end, left, thickness, color);
  DrawLineEx(end, right, thickness, color);
  DrawCircleV(end, thickness * 0.5f, color);
}

inline Color SpeedToColor(float speed,
                          const components::SimulationComponent& sim) {
  constexpr float max_speed = 80.f;
  float speed_ratio = Clamp(speed / max_speed, 0.f, 1.f);

  auto LerpChannel = [](unsigned char a, unsigned char b, float factor) {
    return static_cast<unsigned char>(a + (b - a) * factor);
  };

  auto BlendColor = [&](Color c1, Color c2, float factor) {
    return Color{LerpChannel(c1.r, c2.r, factor),
                 LerpChannel(c1.g, c2.g, factor),
                 LerpChannel(c1.b, c2.b, factor), 255};
  };

  if (speed_ratio < 0.33f) {
    float t = speed_ratio / 0.33f;
    return BlendColor(sim.particle_low_color, sim.particle_mid_low_color, t);
  } else if (speed_ratio < 0.66f) {
    float t = (speed_ratio - 0.33f) / 0.33f;
    return BlendColor(sim.particle_mid_low_color, sim.particle_mid_high_color,
                      t);
  } else {
    float t = (speed_ratio - 0.66f) / 0.34f;
    return BlendColor(sim.particle_mid_high_color, sim.particle_high_color, t);
  }
}

inline Color VelocityToColor(const Vector2& velocity,
                             const components::SimulationComponent& sim) {
  return SpeedToColor(Vector2Length(velocity), sim);
}

inline void RenderFluid(ECS& ecs,
                        const engine::components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);

  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      Color particle_color = VelocityToColor(vel.velocity, sim);
      bool selected = e == sim.selected_particle;

      if (selected && sim.selection_locked) {
        DrawCircleV(pos.position, c.radius * 1.5f, WHITE);
        if (sim.render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius * 1.5f, RED, 0.25f);
      } else {
        DrawCircleV(pos.position, c.radius, particle_color);
        if (sim.render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius, particle_color,
                      0.25f);
      }
    });

  EndMode2D();
}

inline void RenderPressureField(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);

  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::CircleComponent& c) {
      Color col = PressureToColor(c.pressure, sim);

      DrawCircleGradient(pos.position, sim.smoothing_radius * 0.8f, col,
                         Color{col.r, col.g, col.b, 0});
    });

  EndMode2D();
}

inline void DrawTriangleCCW(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
  float area = (v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y);
  if (area < 0.f) std::swap(v2, v3);

  float minX = std::min({v1.x, v2.x, v3.x});
  float maxX = std::max({v1.x, v2.x, v3.x});
  float minY = std::min({v1.y, v2.y, v3.y});
  float maxY = std::max({v1.y, v2.y, v3.y});

  Rectangle rec{minX, minY, maxX - minX, maxY - minY};

  DrawRectangleGradientEx(rec, color, color, color, color);
}

inline void RenderFluidFilled(ECS& ecs,
                              const engine::components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);

  BeginMode2D(cam.camera);

  components::CanvasComponent* canvas_ptr = nullptr;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) { canvas_ptr = &canvas; });

  if (!canvas_ptr) {
    EndMode2D();
    return;
  }

  auto& canvas = *canvas_ptr;

  constexpr int cell_size = 4;

  int grid_w = (CANVAS_W + cell_size + 2) / cell_size;
  int grid_h = (CANVAS_H + cell_size + 1) / cell_size;

  static std::vector<float> field(grid_w * grid_h);
  static std::vector<float> speed_field(grid_w * grid_h);

  std::fill(field.begin(), field.end(), 0.f);
  std::fill(speed_field.begin(), speed_field.end(), 0.f);

  constexpr float radius = 20.f;
  constexpr float radius_sq = radius * radius;
  constexpr float threshold = 0.5f;

  auto CellIndex = [&](int x, int y) { return y * grid_w + x; };
  float he_x = canvas.half_extents.x;
  float he_y = canvas.half_extents.y;
  auto LocalToWorld = [&](float lx, float ly) {
    return CanvasLocalToWorld({lx, ly}, canvas);
  };

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent&) {
      Vector2 local_pos = WorldToCanvasLocal(pos.position, canvas);
      int gx = static_cast<int>((local_pos.x + he_x) / cell_size);
      int gy = static_cast<int>((local_pos.y + he_y) / cell_size);

      int reach = static_cast<int>(radius / cell_size) + 1;

      float speed = Vector2Length(vel.velocity);

      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          int nx = gx + ox;
          int ny = gy + oy;

          if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h) continue;

          float px = nx * cell_size - he_x;
          float py = ny * cell_size - he_y;

          float dx = px - local_pos.x;
          float dy = py - local_pos.y;

          float dist_sq = dx * dx + dy * dy;

          float influence = FluidFieldKernel(dist_sq, radius_sq);

          int idx = CellIndex(nx, ny);

          field[idx] += influence;
          speed_field[idx] += speed * influence;
        }
      }
    });

  for (int y = 0; y < grid_h - 1; ++y) {
    for (int x = 0; x < grid_w - 1; ++x) {
      float v0 = field[CellIndex(x, y)];
      float v1 = field[CellIndex(x + 1, y)];
      float v2 = field[CellIndex(x + 1, y + 1)];
      float v3 = field[CellIndex(x, y + 1)];

      if (v0 < threshold && v1 < threshold && v2 < threshold && v3 < threshold)
        continue;

      int state = 0;

      if (v0 > threshold) state |= 1;
      if (v1 > threshold) state |= 2;
      if (v2 > threshold) state |= 4;
      if (v3 > threshold) state |= 8;

      if (state == 0) continue;

      Vector2 p0{float(x * cell_size) - he_x,
                 float(y * cell_size) - he_y};
      Vector2 p1{float((x + 1) * cell_size) - he_x,
                 float(y * cell_size) - he_y};
      Vector2 p2{float((x + 1) * cell_size) - he_x,
                 float((y + 1) * cell_size) - he_y};
      Vector2 p3{float(x * cell_size) - he_x,
                 float((y + 1) * cell_size) - he_y};

      Vector2 a = InterpolateEdge(p0, p1, v0, v1, threshold);
      Vector2 b = InterpolateEdge(p1, p2, v1, v2, threshold);
      Vector2 c = InterpolateEdge(p2, p3, v2, v3, threshold);
      Vector2 d = InterpolateEdge(p3, p0, v3, v0, threshold);

      Vector2 aw = LocalToWorld(a.x, a.y);
      Vector2 bw = LocalToWorld(b.x, b.y);
      Vector2 cw = LocalToWorld(c.x, c.y);
      Vector2 dw = LocalToWorld(d.x, d.y);
      Vector2 p0w = LocalToWorld(p0.x, p0.y);
      Vector2 p1w = LocalToWorld(p1.x, p1.y);
      Vector2 p2w = LocalToWorld(p2.x, p2.y);
      Vector2 p3w = LocalToWorld(p3.x, p3.y);

      float avg = (v0 + v1 + v2 + v3) * 0.25f;

      float speed = avg > 0.f
                      ? (speed_field[CellIndex(x, y)] / field[CellIndex(x, y)])
                      : 0.f;

      Color col = SpeedToColor(speed, sim);
      float cell_value = std::max({v0, v1, v2, v3});
      float influence_alpha = (cell_value - threshold) / (1.f - threshold);
      influence_alpha = Clamp(influence_alpha, 0.f, 1.f);
      col.a = static_cast<unsigned char>(influence_alpha * 200.f);

      switch (state) {
        case 1:
          DrawTriangleCCW(p0w, dw, aw, col);
          break;

        case 2:
          DrawTriangleCCW(p1w, aw, bw, col);
          break;

        case 3:
          DrawTriangleCCW(p0w, p1w, bw, col);
          DrawTriangleCCW(p0w, bw, dw, col);
          break;

        case 4:
          DrawTriangleCCW(p2w, bw, cw, col);
          break;

        case 5:
          DrawTriangleCCW(p0w, dw, aw, col);
          DrawTriangleCCW(p2w, bw, cw, col);
          break;

        case 6:
          DrawTriangleCCW(aw, p1w, p2w, col);
          DrawTriangleCCW(aw, p2w, cw, col);
          break;

        case 7:
          DrawTriangleCCW(p0w, p1w, p2w, col);
          DrawTriangleCCW(p0w, p2w, cw, col);
          DrawTriangleCCW(p0w, cw, dw, col);
          break;

        case 8:
          DrawTriangleCCW(p3w, cw, dw, col);
          break;

        case 9:
          DrawTriangleCCW(p0w, aw, p3w, col);
          DrawTriangleCCW(p3w, aw, cw, col);
          break;

        case 10:
          DrawTriangleCCW(p1w, aw, bw, col);
          DrawTriangleCCW(p3w, cw, dw, col);
          break;

        case 11:
          DrawTriangleCCW(p0w, p1w, bw, col);
          DrawTriangleCCW(p0w, bw, cw, col);
          DrawTriangleCCW(p0w, cw, p3w, col);
          break;

        case 12:
          DrawTriangleCCW(p3w, p2w, bw, col);
          DrawTriangleCCW(p3w, bw, dw, col);
          break;

        case 13:
          DrawTriangleCCW(p0w, aw, p2w, col);
          DrawTriangleCCW(p0w, p2w, p3w, col);
          break;

        case 14:
          DrawTriangleCCW(p1w, p2w, p3w, col);
          DrawTriangleCCW(p1w, p3w, dw, col);
          DrawTriangleCCW(p1w, dw, aw, col);
          break;

        case 15:
          DrawTriangleCCW(p0w, p1w, p2w, col);
          DrawTriangleCCW(p0w, p2w, p3w, col);
          break;
      }

      Color glow = {200, 200, 200, 180};
      switch (state) {
        case 1:
        case 14:
          DrawLineEx(dw, aw, 2.f, glow);
          break;
        case 2:
        case 13:
          DrawLineEx(aw, bw, 2.f, glow);
          break;
        case 3:
        case 12:
          DrawLineEx(dw, bw, 2.f, glow);
          break;
        case 4:
        case 11:
          DrawLineEx(bw, cw, 2.f, glow);
          break;
        case 5:
          DrawLineEx(dw, cw, 2.f, glow);
          DrawLineEx(aw, bw, 2.f, glow);
          break;
        case 6:
        case 9:
          DrawLineEx(aw, cw, 2.f, glow);
          break;
        case 7:
        case 8:
          DrawLineEx(dw, cw, 2.f, glow);
          break;
        case 10:
          DrawLineEx(aw, dw, 2.f, glow);
          DrawLineEx(bw, cw, 2.f, glow);
          break;
      }
    }
  }

  EndMode2D();
}

inline void RenderMouseSelectionCircle(ECS& ecs,
                                       const components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);
  if (!sim.selection_locked) return;

  BeginMode2D(cam.camera);

  DrawCircleLines(sim.selection_center.x, sim.selection_center.y,
                  sim.smoothing_radius, sim.selection_color);

  EndMode2D();
}

inline void RenderUserPath(ECS& ecs, const components::CameraComponent& cam) {
  auto& sim = entities::Simulation(ecs);
  if (sim.user_path_points.size() < 2) return;

  BeginMode2D(cam.camera);

  Color path_color = WHITE;
  float thickness = 4.f;

  // Convert canvas-local points to world coordinates
  std::vector<Vector2> world_points;
  world_points.resize(sim.user_path_points.size());

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      for (size_t i = 0; i < sim.user_path_points.size(); ++i) {
        world_points[i] =
          CanvasLocalToWorld(sim.user_path_points[i], canvas);
      }
    });

  // Draw path using splines for smooth curves
  if (world_points.size() >= 4) {
    DrawSplineCatmullRom(world_points.data(), world_points.size(), thickness, path_color);
  } else if (world_points.size() >= 2) {
    DrawSplineLinear(world_points.data(), world_points.size(), thickness, path_color);
  }

  EndMode2D();
}

}  // namespace motrix::engine::systems