// engine/systems/physics.h
#pragma once

#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/canvas.h"
#include "../components/physics.h"
#include "../ecs/ecs.h"
#include "../systems/canvas.h"
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

inline float CohesionKernel(float r, float h) {
  if (r >= h * 0.5f) return 0.f;
  float q = r / (h * 0.5f);
  return (1.f - q) * (1.f - q);
}

inline void ComputeParticlePressureForce(ECS& ecs, float dt) {
  float h = entities::smoothing_radius;
  float h2 = h * h;
  float mass = entities::particle_size;
  float viscosity = entities::viscosity;
  float surface_tension = entities::surface_tension;

  for (size_t i = 0; i < particle_entities.size(); ++i) {
    Entity e1 = particle_entities[i];

    auto& c1 = ecs.get<components::CircleComponent>(e1);
    auto& v1 = ecs.get<components::VelocityComponent>(e1);

    Vector2 p1 = predicted_positions[i];
    GridCell cell = PositionToCell(p1, h);

    Vector2 pressure_force{0.f, 0.f};
    Vector2 viscosity_force{0.f, 0.f};
    Vector2 cohesion_force{0.f, 0.f};

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

          float cohes = CohesionKernel(r, h);
          cohesion_force.x += dir.x * cohes;
          cohesion_force.y += dir.y * cohes;
        }
      }
    }

    v1.velocity.x += pressure_force.x * dt;
    v1.velocity.y += pressure_force.y * dt;

    v1.velocity.x += viscosity_force.x * viscosity;
    v1.velocity.y += viscosity_force.y * viscosity;

    v1.velocity.x += cohesion_force.x * surface_tension * mass;
    v1.velocity.y += cohesion_force.y * surface_tension * mass;
  }
}

/**
 * ============================================================================
 * Selection
 * ============================================================================
 */

inline bool IsMouseOverCanvas(ECS& ecs, Vector2 mouse_world) {
  bool over_canvas = false;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Vector2 local_mouse = WorldToCanvasLocal(mouse_world, canvas);

      bool touching_left =
        local_mouse.x >= -canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= -canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_right =
        local_mouse.x >= canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_top =
        local_mouse.y >= -canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= -canvas.half_extents.y + canvas.edge_tolerance;
      bool touching_bottom =
        local_mouse.y >= canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= canvas.half_extents.y + canvas.edge_tolerance;

      if (touching_left || touching_right || touching_top || touching_bottom) {
        over_canvas = true;
      }
    });
  return over_canvas;
}

inline void UpdateSelectionInput(
  ECS& ecs, const engine::components::CameraComponent& cam) {
  if (!entities::selection_active) return;

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  if (UIConsumesMouse(ecs, mouse_screen) ||
      IsMouseOverAnyWindow(ecs, mouse_screen))
    return;

  if (IsMouseOverCanvas(ecs, mouse_world)) {
    entities::selection_locked = false;
    entities::selection_density = 0.f;
    return;
  }

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

  if (speed_ratio < 0.33f) {
    float t = speed_ratio / 0.33f;
    return LerpColor(entities::particle_low_color,
                     entities::particle_mid_low_color, t);
  } else if (speed_ratio < 0.66f) {
    float t = (speed_ratio - 0.33f) / 0.33f;
    return LerpColor(entities::particle_mid_low_color,
                     entities::particle_mid_high_color, t);
  } else {
    float t = (speed_ratio - 0.66f) / 0.34f;
    return LerpColor(entities::particle_mid_high_color,
                     entities::particle_high_color, t);
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

  if (speed_ratio < 0.33f) {
    float t = speed_ratio / 0.33f;
    return BlendColor(entities::particle_low_color,
                      entities::particle_mid_low_color, t);
  } else if (speed_ratio < 0.66f) {
    float t = (speed_ratio - 0.33f) / 0.33f;
    return BlendColor(entities::particle_mid_low_color,
                      entities::particle_mid_high_color, t);
  } else {
    float t = (speed_ratio - 0.66f) / 0.34f;
    return BlendColor(entities::particle_mid_high_color,
                      entities::particle_high_color, t);
  }
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
          RenderArrow(pos.position, vel.velocity, c.radius * 1.5f, RED, 0.25f);
      } else {
        DrawCircleV(pos.position, c.radius, particle_color);
        if (entities::render_particle_velocity)
          RenderArrow(pos.position, vel.velocity, c.radius, particle_color,
                      0.25f);
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

  /**
   * Build scalar + weighted speed field
   */
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
      Vector2 p0{float(x * cell_size) - he_x - offset_x,
                 float(y * cell_size) - he_y};
      Vector2 p1{float((x + 1) * cell_size) - he_x - offset_x,
                 float(y * cell_size) - he_y};
      Vector2 p2{float((x + 1) * cell_size) - he_x - offset_x,
                 float((y + 1) * cell_size) - he_y};
      Vector2 p3{float(x * cell_size) - he_x - offset_x,
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

      Color col = SpeedToColor(speed);
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

      /**
       * Glow edge
       */
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
  const float base_repulsion = 250.f;
  const float particle_repulsion = 0.05f;

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      ecs
        .group_view<components::PositionComponent,
                    components::VelocityComponent, components::CircleComponent>(
          [&](Entity, components::PositionComponent& pos,
              components::VelocityComponent& vel,
              components::CircleComponent& c) {
            Vector2 local_pos = WorldToCanvasLocal(pos.position, canvas);

            float hx = canvas.half_extents.x - c.radius;
            float hy = canvas.half_extents.y - c.radius;

            float dist_from_center =
              std::sqrt(local_pos.x * local_pos.x + local_pos.y * local_pos.y);
            float corner_dist = std::sqrt(hx * hx + hy * hy);

            bool is_near_corner = dist_from_center > corner_dist * 0.8f;
            bool is_outside = local_pos.x < -hx || local_pos.x > hx ||
                              local_pos.y < -hy || local_pos.y > hy;

            if (is_outside) {
              local_pos.x = std::clamp(local_pos.x, -hx, hx);
              local_pos.y = std::clamp(local_pos.y, -hy, hy);

              Vector2 corrected_world = CanvasLocalToWorld(local_pos, canvas);
              pos.position.x = corrected_world.x;
              pos.position.y = corrected_world.y;

              float damp = is_near_corner ? 0.7f : 0.85f;
              vel.velocity.x *= damp;
              vel.velocity.y *= damp;
            }
          });
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

  float cell_size = entities::particle_size * 2.f;
  if (cell_size < 1.f) cell_size = 1.f;
  int cols = static_cast<int>(CANVAS_W / cell_size) + 3;
  int rows = static_cast<int>(CANVAS_H / cell_size) + 3;
  std::vector<std::vector<size_t>> grid(cols * rows);

  for (size_t i = 0; i < particles.size(); ++i) {
    float px = particles[i].pos->position.x;
    float py = particles[i].pos->position.y;
    if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
    int cx = static_cast<int>(px / cell_size) + 1;
    int cy = static_cast<int>(py / cell_size) + 1;
    if (cx >= 0 && cx < cols && cy >= 0 && cy < rows) {
      grid[cy * cols + cx].push_back(i);
    }
  }

  for (size_t i = 0; i < particles.size(); ++i) {
    float px = particles[i].pos->position.x;
    float py = particles[i].pos->position.y;
    if (px < 0 || px > CANVAS_W || py < 0 || py > CANVAS_H) continue;
    int cx = static_cast<int>(px / cell_size) + 1;
    int cy = static_cast<int>(py / cell_size) + 1;

    for (int dy = -1; dy <= 1; ++dy) {
      int ny = cy + dy;
      if (ny < 0 || ny >= rows) continue;
      for (int dx = -1; dx <= 1; ++dx) {
        int nx = cx + dx;
        if (nx < 0 || nx >= cols) continue;

        for (size_t j : grid[ny * cols + nx]) {
          if (j <= i) continue;

          auto& a = particles[i];
          auto& b = particles[j];

          float dx_pos = b.pos->position.x - a.pos->position.x;
          float dy_pos = b.pos->position.y - a.pos->position.y;
          float dist_sq = dx_pos * dx_pos + dy_pos * dy_pos;
          if (dist_sq <= 0.f) continue;

          float dist = std::sqrt(dist_sq);
          float radius_sum = a.circ->radius + b.circ->radius;
          float dir_x = dx_pos / dist;
          float dir_y = dy_pos / dist;

          float repulse_dist = radius_sum * 3.f;
          if (dist < repulse_dist) {
            float repulse_strength = (repulse_dist - dist) / dist;
            a.vel->velocity.x -= dir_x * particle_repulsion * repulse_strength;
            a.vel->velocity.y -= dir_y * particle_repulsion * repulse_strength;
            b.vel->velocity.x += dir_x * particle_repulsion * repulse_strength;
            b.vel->velocity.y += dir_y * particle_repulsion * repulse_strength;
          }

          if (dist < radius_sum) {
            float overlap = radius_sum - dist;
            a.pos->position.x -= dir_x * overlap * 0.5f;
            a.pos->position.y -= dir_y * overlap * 0.5f;
            b.pos->position.x += dir_x * overlap * 0.5f;
            b.pos->position.y += dir_y * overlap * 0.5f;

            float dot = (b.vel->velocity.x - a.vel->velocity.x) * dir_x +
                        (b.vel->velocity.y - a.vel->velocity.y) * dir_y;
            a.vel->velocity.x += dir_x * dot * 0.5f;
            a.vel->velocity.y += dir_y * dot * 0.5f;
            b.vel->velocity.x -= dir_x * dot * 0.5f;
            b.vel->velocity.y -= dir_y * dot * 0.5f;
          }
        }
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
