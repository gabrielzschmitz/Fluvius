// engine/components/physics.h
#pragma once

#include <string_view>

#include "raylib.h"

namespace motrix::engine::components {

struct PositionComponent {
  static constexpr std::string_view Name = "Position";
  Vector2 position{0.f, 0.f};
  explicit PositionComponent(Vector2 value = {0.f, 0.f}) : position(value) {}
};

struct VelocityComponent {
  static constexpr std::string_view Name = "Velocity";
  Vector2 velocity{0.f, 0.f};
  explicit VelocityComponent(Vector2 value = {0.f, 0.f}) : velocity(value) {}
};

struct CircleComponent {
  static constexpr std::string_view Name = "Circle";

  float radius = 4.f;
  float particle_size = 1.f;
  Color color{255, 183, 222, 255};
  float density = 0.f;
  float pressure = 0.f;

  CircleComponent(float radius_value = 4.f, float particle_size_value = 1.f,
                  Color color_value = {255, 183, 222, 255})
    : radius(radius_value),
      particle_size(particle_size_value),
      color(color_value) {}
};

}  // namespace motrix::engine::components
