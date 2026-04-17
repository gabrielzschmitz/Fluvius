// engine/systems/physics.h
#pragma once

#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/physics.h"
#include "../ecs/ecs.h"
#include "../systems/ui_helpers.h"
#include "entities/fluid.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Runtime Buffers
 * ============================================================================
 */

inline std::vector<Entity> particle_entities;
inline std::vector<Vector2> predicted_positions;
inline std::unordered_map<Entity, size_t> particle_index;

/**
 * ============================================================================
 * Spatial Hash
 * ============================================================================
 */

struct GridCell {
  int x;
  int y;

  bool operator==(const GridCell& other) const {
    return x == other.x && y == other.y;
  }
};

struct GridCellHash {
  size_t operator()(const GridCell& c) const {
    return std::hash<int>()(c.x * 73856093) ^ std::hash<int>()(c.y * 19349663);
  }
};

inline std::unordered_map<GridCell, std::vector<size_t>, GridCellHash>
  spatial_grid;

inline GridCell PositionToCell(Vector2 p, float cell_size) {
  return {static_cast<int>(std::floor(p.x / cell_size)),
          static_cast<int>(std::floor(p.y / cell_size))};
}

inline void BuildSpatialGrid() {
  spatial_grid.clear();

  float h = entities::smoothing_radius;

  for (size_t i = 0; i < predicted_positions.size(); ++i) {
    GridCell cell = PositionToCell(predicted_positions[i], h);
    spatial_grid[cell].push_back(i);
  }
}

/**
 * ============================================================================
 * Kernels
 * ============================================================================
 */

inline float Poly6Kernel(float r2, float h) {
  float h2 = h * h;
  if (r2 >= h2) return 0.f;

  float diff = h2 - r2;
  float h9 = std::pow(h, 9.f);

  return 315.f / (64.f * PI * h9) * diff * diff * diff;
}

inline float SpikyKernelGradient(float r, float h) {
  if (r <= 0.f || r >= h) return 0.f;

  float h5 = std::pow(h, 5.f);
  float v = h - r;

  return -15.f / (PI * h5) * v * v;
}

inline float ViscosityKernel(float r, float h) {
  if (r >= h) return 0.f;

  float h5 = std::pow(h, 5.f);

  return 15.f / (2.f * PI * h5) * (h - r);
}

inline float FluidFieldKernel(float dist_sq, float radius_sq) {
  if (dist_sq >= radius_sq) return 0.f;

  float x = 1.f - (dist_sq / radius_sq);

  return x * x;
}

inline Vector2 InterpolateEdge(Vector2 a, Vector2 b, float va, float vb,
                               float threshold) {
  if (fabsf(vb - va) < 0.0001f) return a;

  float t = (threshold - va) / (vb - va);

  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

/**
 * ============================================================================
 * Pressure
 * ============================================================================
 */

inline float ConvertDensityToPressure(float density) {
  return (density - entities::target_density) * entities::pressure_multiplier;
}

/**
 * ============================================================================
 * Pressure Color
 * ============================================================================
 */

inline Color PressureToColor(float pressure) {
  float max_pressure = entities::target_density * entities::pressure_multiplier;

  if (max_pressure <= 0.f) return entities::pressure_mid_color;

  float scale = Clamp(pressure / max_pressure, -1.f, 1.f);

  auto lerp = [](unsigned char a, unsigned char b, float t) {
    return static_cast<unsigned char>(a + (b - a) * t);
  };

  if (scale < 0.f) {
    float t = scale + 1.f;

    return {
      lerp(entities::pressure_low_color.r, entities::pressure_mid_color.r, t),
      lerp(entities::pressure_low_color.g, entities::pressure_mid_color.g, t),
      lerp(entities::pressure_low_color.b, entities::pressure_mid_color.b, t),
      130};
  }

  return {lerp(entities::pressure_mid_color.r, entities::pressure_high_color.r,
               scale),
          lerp(entities::pressure_mid_color.g, entities::pressure_high_color.g,
               scale),
          lerp(entities::pressure_mid_color.b, entities::pressure_high_color.b,
               scale),
          130};
}

/**
 * ============================================================================
 * Prediction
 * ============================================================================
 */

inline void PredictPositions(ECS& ecs, float dt) {
  float gravity = entities::gravity * 10.f;

  particle_entities.clear();

  size_t count = 0;

  ecs.group_view<components::PositionComponent>(
    [&](Entity e, components::PositionComponent&) {
      particle_entities.push_back(e);
      particle_index[e] = count++;
    });

  predicted_positions.resize(count);

  for (size_t i = 0; i < particle_entities.size(); ++i) {
    Entity e = particle_entities[i];

    auto& pos = ecs.get<components::PositionComponent>(e);
    auto& vel = ecs.get<components::VelocityComponent>(e);

    vel.velocity.y += gravity * dt;

    predicted_positions[i] = {pos.position.x + vel.velocity.x * dt,
                              pos.position.y + vel.velocity.y * dt};
  }

  BuildSpatialGrid();
}

/**
 * ============================================================================
 * Density
 * ============================================================================
 */

inline void ComputeParticleDensity(ECS& ecs) {
  float h = entities::smoothing_radius;
  float h2 = h * h;
  float mass = entities::particle_size;

  for (size_t i = 0; i < particle_entities.size(); ++i) {
    Entity e = particle_entities[i];
    auto& c = ecs.get<components::CircleComponent>(e);

    Vector2 p = predicted_positions[i];
    GridCell cell = PositionToCell(p, h);

    float density = 0.f;

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        GridCell neighbor{cell.x + dx, cell.y + dy};

        auto it = spatial_grid.find(neighbor);
        if (it == spatial_grid.end()) continue;

        for (size_t j : it->second) {
          Vector2 p2 = predicted_positions[j];

          float rx = p2.x - p.x;
          float ry = p2.y - p.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= h2) density += mass * Poly6Kernel(r2, h);
        }
      }
    }

    c.density = density;
  }
}

/**
 * ============================================================================
 * Pressure
 * ============================================================================
 */

inline void ComputeParticlePressure(ECS& ecs) {
  ecs.group_view<components::CircleComponent>(
    [&](Entity, components::CircleComponent& c) {
      c.pressure = ConvertDensityToPressure(c.density);
    });
}

/**
 * ============================================================================
 * Pressure Force
 * ============================================================================
 */

inline void ComputeParticlePressureForce(ECS& ecs, float dt) {
  float h = entities::smoothing_radius;
  float h2 = h * h;
  float mass = entities::particle_size;
  float viscosity = 0.08f;

  for (size_t i = 0; i < particle_entities.size(); ++i) {
    Entity e1 = particle_entities[i];

    auto& c1 = ecs.get<components::CircleComponent>(e1);
    auto& v1 = ecs.get<components::VelocityComponent>(e1);

    Vector2 p1 = predicted_positions[i];
    GridCell cell = PositionToCell(p1, h);

    Vector2 pressure_force{0.f, 0.f};
    Vector2 viscosity_force{0.f, 0.f};

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        GridCell neighbor{cell.x + dx, cell.y + dy};

        auto it = spatial_grid.find(neighbor);
        if (it == spatial_grid.end()) continue;

        for (size_t j : it->second) {
          if (i == j) continue;

          Entity e2 = particle_entities[j];

          auto& c2 = ecs.get<components::CircleComponent>(e2);
          auto& v2 = ecs.get<components::VelocityComponent>(e2);

          Vector2 p2 = predicted_positions[j];

          float rx = p1.x - p2.x;
          float ry = p1.y - p2.y;

          float r2 = rx * rx + ry * ry;

          if (r2 <= 0.f || r2 > h2) continue;

          float r = sqrtf(r2);

          Vector2 dir{rx / r, ry / r};

          float grad = SpikyKernelGradient(r, h);

          float term = (c1.pressure / (c1.density * c1.density)) +
                       (c2.pressure / (c2.density * c2.density));

          float factor = -mass * term * grad;

          pressure_force.x += dir.x * factor;
          pressure_force.y += dir.y * factor;

          float visc = ViscosityKernel(r, h);

          viscosity_force.x += (v2.velocity.x - v1.velocity.x) * visc;
          viscosity_force.y += (v2.velocity.y - v1.velocity.y) * visc;
        }
      }
    }

    v1.velocity.x += pressure_force.x * dt;
    v1.velocity.y += pressure_force.y * dt;

    v1.velocity.x += viscosity_force.x * viscosity;
    v1.velocity.y += viscosity_force.y * viscosity;
  }
}

/**
 * ============================================================================
 * Selection
 * ============================================================================
 */

inline void UpdateSelectionInput(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  if (!entities::selection_active) return;

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  if (UIConsumesMouse(ecs, mouse_screen) ||
      IsMouseOverAnyWindow(ecs, mouse_screen))
    return;

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    entities::selection_center = mouse_world;
    entities::selection_locked = true;
  }

  if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
    entities::selection_locked = false;
    entities::selection_density = 0.f;
  }
}

inline void UpdateSelectionDensity(ECS& ecs) {
  if (!entities::selection_locked) return;

  float nearest = FLT_MAX;

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::CircleComponent& c) {
      float dx = pos.position.x - entities::selection_center.x;
      float dy = pos.position.y - entities::selection_center.y;

      float d = dx * dx + dy * dy;

      if (d < nearest) {
        nearest = d;
        entities::selection_density = c.density;
        entities::selected_particle = e;
      }
    });
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

  // Arrow starts at particle edge
  Vector2 start = {center.x + dir.x * radius, center.y + dir.y * radius};

  // Length proportional to radius + speed
  float arrow_length = radius + speed * scale;

  Vector2 end = {start.x + dir.x * arrow_length,
                 start.y + dir.y * arrow_length};

  // Proportional sizes
  float head_size = radius * 0.8f;
  float thickness = radius * 0.5f;

  // Shaft
  DrawLineEx(start, end, thickness, color);

  // Head
  Vector2 left = {end.x - dir.x * head_size + dir.y * head_size * 0.5f,
                  end.y - dir.y * head_size - dir.x * head_size * 0.5f};

  Vector2 right = {end.x - dir.x * head_size - dir.y * head_size * 0.5f,
                   end.y - dir.y * head_size + dir.x * head_size * 0.5f};

  DrawLineEx(end, left, thickness, color);
  DrawLineEx(end, right, thickness, color);
  DrawCircleV(end, thickness * 0.5f, color);
}

inline Color VelocityToColor(const Vector2& velocity) {
  const float speed = Vector2Length(velocity);
  constexpr float max_speed = 80.f;

  float speed_ratio = Clamp(speed / max_speed, 0.f, 1.f);

  auto LerpChannel = [](unsigned char a, unsigned char b, float factor) {
    return static_cast<unsigned char>(a + (b - a) * factor);
  };

  auto LerpColor = [&](Color c1, Color c2, float factor) {
    return Color{LerpChannel(c1.r, c2.r, factor),
                 LerpChannel(c1.g, c2.g, factor),
                 LerpChannel(c1.b, c2.b, factor), 255};
  };

  constexpr Color blue = {0, 120, 255, 255};
  constexpr Color green = {0, 255, 120, 255};
  constexpr Color yellow = {255, 220, 0, 255};
  constexpr Color red = {255, 40, 40, 255};

  if (speed_ratio < 0.33f) {
    return LerpColor(blue, green, speed_ratio / 0.33f);
  } else if (speed_ratio < 0.66f) {
    return LerpColor(green, yellow, (speed_ratio - 0.33f) / 0.33f);
  } else {
    return LerpColor(yellow, red, (speed_ratio - 0.66f) / 0.34f);
  }
}

inline Color SpeedToColor(float speed) {
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

  constexpr Color blue = {0, 120, 255, 255};
  constexpr Color green = {0, 255, 120, 255};
  constexpr Color yellow = {255, 220, 0, 255};
  constexpr Color red = {255, 50, 50, 255};

  if (speed_ratio < 0.33f) return BlendColor(blue, green, speed_ratio / 0.33f);
  if (speed_ratio < 0.66f)
    return BlendColor(green, yellow, (speed_ratio - 0.33f) / 0.33f);

  return BlendColor(yellow, red, (speed_ratio - 0.66f) / 0.34f);
}

inline void RenderFluid(ECS& ecs,
                        const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity e, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      Color particle_color = VelocityToColor(vel.velocity);
      bool selected = e == entities::selected_particle;

      if (selected && entities::selection_locked) {
        DrawCircleV(pos.position, c.radius * 1.5f, WHITE);
        if (entities::render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius * 1.5f, RED, 0.5f);
      } else {
        DrawCircleV(pos.position, c.radius, particle_color);
        if (entities::render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius, RED, 0.5f);
      }
    });

  EndMode2D();
}

inline void RenderPressureField(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::CircleComponent& c) {
      Color col = PressureToColor(c.pressure);

      DrawCircleGradient(pos.position, entities::smoothing_radius * 0.8f, col,
                         Color{col.r, col.g, col.b, 0});
    });

  EndMode2D();
}

inline void RenderFluidSurface(ECS& ecs,
                               const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  constexpr int cell_size = 6;

  int grid_w = (CANVAS_W + cell_size - 1) / cell_size;
  int grid_h = (CANVAS_H + cell_size - 1) / cell_size;

  static std::vector<float> field(grid_w * grid_h);

  std::fill(field.begin(), field.end(), 0.f);

  constexpr float radius = 20.f;
  constexpr float radius_sq = radius * radius;

  auto CellIndex = [&](int x, int y) { return y * grid_w + x; };

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent&) {
      int gx = static_cast<int>(pos.position.x / cell_size);
      int gy = static_cast<int>(pos.position.y / cell_size);

      int reach = static_cast<int>(radius / cell_size) + 1;

      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          int nx = gx + ox;
          int ny = gy + oy;

          if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h) continue;

          float px = nx * cell_size + cell_size * 0.5f;
          float py = ny * cell_size + cell_size * 0.5f;

          float dx = px - pos.position.x;
          float dy = py - pos.position.y;

          float dist_sq = dx * dx + dy * dy;

          field[CellIndex(nx, ny)] += FluidFieldKernel(dist_sq, radius_sq);
        }
      }
    });

  constexpr float threshold = 0.7f;

  for (int y = 0; y < grid_h; ++y) {
    for (int x = 0; x < grid_w; ++x) {
      float v = field[CellIndex(x, y)];

      if (v < threshold) continue;

      float alpha = Clamp((v - threshold) * 255.f, 80.f, 220.f);

      DrawRectangle(x * cell_size - 1, y * cell_size, cell_size, cell_size,
                    Color{40, 140, 255, static_cast<unsigned char>(alpha)});
    }
  }

  EndMode2D();
}

inline void RenderFluidMarchingSquares(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  float line_thickness = 2.f;
  constexpr int cell_size = 5;

  int grid_w = CANVAS_W / cell_size + 2;
  int grid_h = CANVAS_H / cell_size + 1;

  static std::vector<float> field(grid_w * grid_h);
  std::fill(field.begin(), field.end(), 0.f);

  constexpr float radius = 20.f;
  constexpr float radius_sq = radius * radius;
  constexpr float threshold = 0.5f;

  auto CellIndex = [&](int x, int y) { return y * grid_w + x; };

  /**
     * Build scalar field
     */
  ecs.group_view<components::PositionComponent, components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::CircleComponent&) {
      int gx = static_cast<int>(pos.position.x / cell_size);
      int gy = static_cast<int>(pos.position.y / cell_size);

      int reach = static_cast<int>(radius / cell_size) + 1;

      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          int nx = gx + ox;
          int ny = gy + oy;

          if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h) continue;

          float px = nx * cell_size;
          float py = ny * cell_size;

          float dx = px - pos.position.x;
          float dy = py - pos.position.y;

          float dist_sq = dx * dx + dy * dy;
          float influence = FluidFieldKernel(dist_sq, radius_sq);

          field[CellIndex(nx, ny)] += influence;
        }
      }
    });

  /**
     * Marching squares contour extraction
     */
  for (int y = 0; y < grid_h - 1; ++y) {
    for (int x = 0; x < grid_w - 1; ++x) {
      float v0 = field[CellIndex(x, y)];
      float v1 = field[CellIndex(x + 1, y)];
      float v2 = field[CellIndex(x + 1, y + 1)];
      float v3 = field[CellIndex(x, y + 1)];

      int state = 0;
      if (v0 > threshold) state |= 1;
      if (v1 > threshold) state |= 2;
      if (v2 > threshold) state |= 4;
      if (v3 > threshold) state |= 8;

      if (state == 0 || state == 15) continue;

      float offset_x = 2.0f;
      Vector2 p0{float(x * cell_size) - offset_x, float(y * cell_size)};
      Vector2 p1{float((x + 1) * cell_size) - offset_x, float(y * cell_size)};
      Vector2 p2{float((x + 1) * cell_size) - offset_x,
                 float((y + 1) * cell_size)};
      Vector2 p3{float(x * cell_size) - offset_x, float((y + 1) * cell_size)};

      Vector2 a = InterpolateEdge(p0, p1, v0, v1, threshold);
      Vector2 b = InterpolateEdge(p1, p2, v1, v2, threshold);
      Vector2 c = InterpolateEdge(p2, p3, v2, v3, threshold);
      Vector2 d = InterpolateEdge(p3, p0, v3, v0, threshold);

      Color color = {200, 200, 200, 180};

      // Draw thicker lines
      switch (state) {
        case 1:
        case 14:
          DrawLineEx(d, a, line_thickness, color);
          break;
        case 2:
        case 13:
          DrawLineEx(a, b, line_thickness, color);
          break;
        case 3:
        case 12:
          DrawLineEx(d, b, line_thickness, color);
          break;
        case 4:
        case 11:
          DrawLineEx(b, c, line_thickness, color);
          break;
        case 5:
          DrawLineEx(d, c, line_thickness, color);
          DrawLineEx(a, b, line_thickness, color);
          break;
        case 6:
        case 9:
          DrawLineEx(a, c, line_thickness, color);
          break;
        case 7:
        case 8:
          DrawLineEx(d, c, line_thickness, color);
          break;
        case 10:
          DrawLineEx(a, d, line_thickness, color);
          DrawLineEx(b, c, line_thickness, color);
          break;
      }
    }
  }

  EndMode2D();
}

inline void DrawTriangleCCW(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
  // Compute signed area to enforce CCW (optional, keeps winding consistent)
  float area = (v2.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (v2.y - v1.y);
  if (area < 0.f) std::swap(v2, v3);

  // Find bounding rectangle
  float minX = std::min({v1.x, v2.x, v3.x});
  float maxX = std::max({v1.x, v2.x, v3.x});
  float minY = std::min({v1.y, v2.y, v3.y});
  float maxY = std::max({v1.y, v2.y, v3.y});

  Rectangle rec{minX, minY, maxX - minX, maxY - minY};

  // Draw as gradient rectangle using same color for all corners
  DrawRectangleGradientEx(rec, color, color, color, color);
}

inline void RenderFluidFilled(ECS& ecs,
                              const engine::components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  constexpr int cell_size = 5;

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

  /**
   * Build scalar + weighted speed field
   */
  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent&) {
      int gx = static_cast<int>(pos.position.x / cell_size);
      int gy = static_cast<int>(pos.position.y / cell_size);

      int reach = static_cast<int>(radius / cell_size) + 1;

      float speed = Vector2Length(vel.velocity);

      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          int nx = gx + ox;
          int ny = gy + oy;

          if (nx < 0 || nx >= grid_w || ny < 0 || ny >= grid_h) continue;

          float px = nx * cell_size;
          float py = ny * cell_size;

          float dx = px - pos.position.x;
          float dy = py - pos.position.y;

          float dist_sq = dx * dx + dy * dy;

          float influence = FluidFieldKernel(dist_sq, radius_sq);

          int idx = CellIndex(nx, ny);

          field[idx] += influence;
          speed_field[idx] += speed * influence;
        }
      }
    });

  /**
   * Triangle-filled marching squares
   */
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

      float offset_x = 0.f;
      Vector2 p0{float(x * cell_size) - offset_x, float(y * cell_size)};
      Vector2 p1{float((x + 1) * cell_size) - offset_x, float(y * cell_size)};
      Vector2 p2{float((x + 1) * cell_size) - offset_x,
                 float((y + 1) * cell_size)};
      Vector2 p3{float(x * cell_size) - offset_x, float((y + 1) * cell_size)};

      Vector2 a = InterpolateEdge(p0, p1, v0, v1, threshold);
      Vector2 b = InterpolateEdge(p1, p2, v1, v2, threshold);
      Vector2 c = InterpolateEdge(p2, p3, v2, v3, threshold);
      Vector2 d = InterpolateEdge(p3, p0, v3, v0, threshold);

      float avg = (v0 + v1 + v2 + v3) * 0.25f;

      float speed = avg > 0.f
                      ? (speed_field[CellIndex(x, y)] / field[CellIndex(x, y)])
                      : 0.f;

      Color col = SpeedToColor(speed);
      float cell_value = std::max({v0, v1, v2, v3});
      float influence_alpha = (cell_value - threshold) / (1.f - threshold);
      influence_alpha = Clamp(influence_alpha, 0.f, 1.f);
      col.a = static_cast<unsigned char>(influence_alpha * 200.f);

      switch (state) {
        case 1:
          DrawTriangleCCW(p0, d, a, col);
          break;

        case 2:
          DrawTriangleCCW(p1, a, b, col);
          break;

        case 3:
          DrawTriangleCCW(p0, p1, b, col);
          DrawTriangleCCW(p0, b, d, col);
          break;

        case 4:
          DrawTriangleCCW(p2, b, c, col);
          break;

        case 5:
          DrawTriangleCCW(p0, d, a, col);
          DrawTriangleCCW(p2, b, c, col);
          break;

        case 6:
          DrawTriangleCCW(a, p1, p2, col);
          DrawTriangleCCW(a, p2, c, col);
          break;

        case 7:
          DrawTriangleCCW(p0, p1, p2, col);
          DrawTriangleCCW(p0, p2, c, col);
          DrawTriangleCCW(p0, c, d, col);
          break;

        case 8:
          DrawTriangleCCW(p3, c, d, col);
          break;

        case 9:
          DrawTriangleCCW(p0, a, p3, col);
          DrawTriangleCCW(p3, a, c, col);
          break;

        case 10:
          DrawTriangleCCW(p1, a, b, col);
          DrawTriangleCCW(p3, c, d, col);
          break;

        case 11:
          DrawTriangleCCW(p0, p1, b, col);
          DrawTriangleCCW(p0, b, c, col);
          DrawTriangleCCW(p0, c, p3, col);
          break;

        case 12:
          DrawTriangleCCW(p3, p2, b, col);
          DrawTriangleCCW(p3, b, d, col);
          break;

        case 13:
          DrawTriangleCCW(p0, a, p2, col);
          DrawTriangleCCW(p0, p2, p3, col);
          break;

        case 14:
          DrawTriangleCCW(p1, p2, p3, col);
          DrawTriangleCCW(p1, p3, d, col);
          DrawTriangleCCW(p1, d, a, col);
          break;

        case 15:
          DrawTriangleCCW(p0, p1, p2, col);
          DrawTriangleCCW(p0, p2, p3, col);
          break;
      }

      /**
       * Glow edge
       */
      Color glow = {200, 200, 200, 180};
      switch (state) {
        case 1:
        case 14:
          DrawLineEx(d, a, 2.f, glow);
          break;
        case 2:
        case 13:
          DrawLineEx(a, b, 2.f, glow);
          break;
        case 3:
        case 12:
          DrawLineEx(d, b, 2.f, glow);
          break;
        case 4:
        case 11:
          DrawLineEx(b, c, 2.f, glow);
          break;
        case 5:
          DrawLineEx(d, c, 2.f, glow);
          DrawLineEx(a, b, 2.f, glow);
          break;
        case 6:
        case 9:
          DrawLineEx(a, c, 2.f, glow);
          break;
        case 7:
        case 8:
          DrawLineEx(d, c, 2.f, glow);
          break;
        case 10:
          DrawLineEx(a, d, 2.f, glow);
          DrawLineEx(b, c, 2.f, glow);
          break;
      }
    }
  }

  EndMode2D();
}

inline void RenderMouseSelectionCircle(const components::CameraComponent& cam) {
  if (!entities::selection_locked) return;

  BeginMode2D(cam.camera);

  DrawCircleLines(entities::selection_center.x, entities::selection_center.y,
                  entities::smoothing_radius, entities::selection_color);

  EndMode2D();
}

/**
 * ============================================================================
 * Collisions
 * ============================================================================
 */
inline void ResolveCollisions(ECS& ecs) {
  const float damping = entities::bounce_enabled ? 0.5f : 0.f;
  const float base_repulsion = 100.f;
  const float particle_repulsion = 0.05f;
  const float boundary_margin = 10.f;

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      Vector2 correction{0.f, 0.f};

      if (pos.position.x - c.radius < boundary_margin) {
        float penetration = boundary_margin - (pos.position.x - c.radius);
        correction.x += penetration * base_repulsion / boundary_margin;
      }
      if (pos.position.x + c.radius > CANVAS_W - boundary_margin) {
        float penetration =
          (pos.position.x + c.radius) - (CANVAS_W - boundary_margin);
        correction.x -= penetration * base_repulsion / boundary_margin;
      }
      if (pos.position.y - c.radius < boundary_margin) {
        float penetration = boundary_margin - (pos.position.y - c.radius);
        correction.y += penetration * base_repulsion / boundary_margin;
      }
      if (pos.position.y + c.radius > CANVAS_H - boundary_margin) {
        float penetration =
          (pos.position.y + c.radius) - (CANVAS_H - boundary_margin);
        correction.y -= penetration * base_repulsion / boundary_margin;
      }

      float speed = Vector2Length(vel.velocity);
      float factor = 1.f + speed * 0.05f;
      vel.velocity.x += correction.x * factor * (1.f / 120.f);
      vel.velocity.y += correction.y * factor * (1.f / 120.f);

      pos.position.x = std::clamp(pos.position.x, c.radius, CANVAS_W - c.radius);
      pos.position.y = std::clamp(pos.position.y, c.radius, CANVAS_H - c.radius);
    });

  struct ParticleRef {
    components::PositionComponent* pos;
    components::VelocityComponent* vel;
    components::CircleComponent* circ;
  };
  std::vector<ParticleRef> particles;
  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      particles.push_back({&pos, &vel, &c});
    });

  for (size_t i = 0; i < particles.size(); ++i) {
    auto& a = particles[i];
    for (size_t j = i + 1; j < particles.size(); ++j) {
      auto& b = particles[j];

      Vector2 delta = {b.pos->position.x - a.pos->position.x,
                       b.pos->position.y - a.pos->position.y};
      float dist_sq = delta.x * delta.x + delta.y * delta.y;
      if (dist_sq <= 0.f) continue;

      float dist = std::sqrt(dist_sq);
      float radius_sum = a.circ->radius + b.circ->radius;
      Vector2 dir = {delta.x / dist, delta.y / dist};

      float repulse_dist = radius_sum * 3.f;
      if (dist < repulse_dist) {
        float repulse_strength = (repulse_dist - dist) / dist;
        a.vel->velocity.x -= dir.x * particle_repulsion * repulse_strength;
        a.vel->velocity.y -= dir.y * particle_repulsion * repulse_strength;
        b.vel->velocity.x += dir.x * particle_repulsion * repulse_strength;
        b.vel->velocity.y += dir.y * particle_repulsion * repulse_strength;
      }

      if (dist < radius_sum) {
        float overlap = radius_sum - dist;
        a.pos->position.x -= dir.x * overlap * 0.5f;
        a.pos->position.y -= dir.y * overlap * 0.5f;
        b.pos->position.x += dir.x * overlap * 0.5f;
        b.pos->position.y += dir.y * overlap * 0.5f;

        float dot = (b.vel->velocity.x - a.vel->velocity.x) * dir.x +
                    (b.vel->velocity.y - a.vel->velocity.y) * dir.y;
        a.vel->velocity.x += dir.x * dot * 0.5f;
        a.vel->velocity.y += dir.y * dot * 0.5f;
        b.vel->velocity.x -= dir.x * dot * 0.5f;
        b.vel->velocity.y -= dir.y * dot * 0.5f;
      }
    }
  }
}

/**
 * ============================================================================
 * Simulation
 * ============================================================================
 */
inline void SimulateFluid(ECS& ecs, float dt) {
  if (entities::is_paused) return;

  float effective_dt = dt * entities::sim_speed;

  PredictPositions(ecs, effective_dt);
  ComputeParticleDensity(ecs);
  ComputeParticlePressure(ecs);
  ComputeParticlePressureForce(ecs, effective_dt);

  ecs.group_view<components::PositionComponent, components::VelocityComponent,
                 components::CircleComponent>(
    [&](Entity, components::PositionComponent& pos,
        components::VelocityComponent& vel, components::CircleComponent& c) {
      c.radius = entities::particle_size;
      c.particle_size = entities::particle_size;

      pos.position.x += vel.velocity.x * effective_dt;
      pos.position.y += vel.velocity.y * effective_dt;
    });

  ResolveCollisions(ecs);
}

}  // namespace motrix::engine::systems
