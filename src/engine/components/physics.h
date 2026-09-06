// engine/components/physics.h
#pragma once

#include <string_view>
#include <vector>

#include "../ecs/ecs.h"
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
  Color color{255, 183, 222, 255};
  float density = 0.f;
  float pressure = 0.f;

  CircleComponent(float radius_value = 4.f,
                  Color color_value = {255, 183, 222, 255})
    : radius(radius_value), color(color_value) {}
};

/**
 * Simulation-wide configuration and runtime state. Exactly one entity (the
 * "simulation root") carries this component; `entities::Simulation(ecs)`
 * resolves it. This replaces the previous namespace-level globals so all
 * simulation state is ECS-resident.
 */
struct SimulationComponent {
  static constexpr std::string_view Name = "Simulation";

  // --- simulation parameters ---
  float gravity = 1.0f;
  bool create_centered = true;
  float smoothing_radius = 50.f;
  bool is_paused = true;
  float sim_speed = 1.0f;

  float target_density = 0.000425f;
  float pressure_multiplier = 250.f;
  float viscosity = 0.8f;
  float surface_tension = 0.25f;
  float velocity_damping = 0.997f;

  int particle_count = 1024;
  float particle_size = 2.0f;

  bool needs_reset = false;
  bool particle_cache_dirty = true;

  // --- selection state ---
  Color selection_color{255, 255, 255, 191};
  bool selection_active = true;
  float selection_density = 0.f;
  Vector2 selection_center{0.f, 0.f};
  bool selection_locked = false;
  Entity selected_particle{};

  // --- render flags ---
  bool render_fluid_filled = false;
  bool render_pressure_field = false;
  bool render_fluid_particles = true;
  bool render_particle_velocity = true;

  // --- color scheme ---
  Color pressure_low_color{0, 0, 128, 130};       // blue
  Color pressure_mid_color{255, 255, 255, 130};   // white
  Color pressure_high_color{255, 50, 50, 130};    // red

  Color particle_low_color{0, 120, 255, 190};       // blue
  Color particle_mid_low_color{0, 255, 255, 190};   // cyan
  Color particle_mid_high_color{255, 220, 0, 180};  // yellow
  Color particle_high_color{255, 50, 50, 160};      // red

  // --- user-drawn path ---
  std::vector<Vector2> user_path_points;
  bool is_drawing_path = false;
  float path_point_spacing = 1.f;
};

}  // namespace motrix::engine::components
