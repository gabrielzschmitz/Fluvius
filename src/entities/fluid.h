// entities/fluid.h
#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>

#include "../engine/components/physics.h"
#include "../engine/ecs/ecs.h"
#include "../engine/globals.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::entities {

/**
 * ============================================================================
 * Fluid Entities
 * ============================================================================
 *
 * Creates multiple simulated particles used by fluid systems.
 *
 * Characteristics:
 *   • multiple ECS entities
 *   • physics + render components
 * ============================================================================
 */

inline std::vector<engine::Entity> fluid_particles;

inline Color selection_color = {255, 255, 255, 191};
inline bool selection_active = true;
inline float selection_density = 0.f;
inline Vector2 selection_center = {0.f, 0.f};
inline bool selection_locked = false;
inline engine::Entity selected_particle{};

inline bool render_fluid_surface = true;
inline bool render_fluid_filled = true;
inline bool render_marching_squares = false;
inline bool render_pressure_field = false;
inline bool render_fluid_particles = true;
inline bool render_particle_velocity = false;

inline float gravity = 1.0f;
inline bool create_centered = 1.0f;
inline float smoothing_radius = 50.f;
inline bool bounce_enabled = true;
inline bool is_paused = true;
inline float sim_speed = 1.0f;

inline float target_density = 0.000425f;
inline float pressure_multiplier = 250.f;
inline float particle_size =
  Clamp(2.0f * std::pow(1000.0f / static_cast<float>(PARTICLE_NUMBER), 0.4f),
        0.5f, 3.0f);

inline Color pressure_low_color = {0, 0, 128, 130};      // blue
inline Color pressure_mid_color = {255, 255, 255, 130};  // white
inline Color pressure_high_color = {255, 50, 50, 130};   // red

inline void CreateFluid(engine::ECS& ecs, size_t particle_count = 10000,
                        bool centered = false) {
  fluid_particles.clear();
  fluid_particles.reserve(particle_count);

  std::vector<Vector2> placed_positions;
  placed_positions.reserve(particle_count);

  float radius = particle_size;
  float min_dist = radius * 1.25f;
  float min_dist_sq = min_dist * min_dist;

  size_t created = 0;

  if (centered) {
    int cols = static_cast<int>(std::sqrt(particle_count));
    int rows = (particle_count + cols - 1) / cols;

    float gap_factor = 2.5f;
    float spacing = min_dist * gap_factor;

    float start_x = CANVAS_W / 2.f - (cols * spacing) / 2.f + radius;
    float start_y = CANVAS_H / 2.f - (rows * spacing) / 2.f + radius;

    for (int r = 0; r < rows && created < particle_count; ++r) {
      for (int c = 0; c < cols && created < particle_count; ++c) {
        Vector2 pos{start_x + c * spacing, start_y + r * spacing};

        engine::Entity e = ecs.create_entity();
        ecs.add<engine::components::PositionComponent>(e, pos);
        ecs.add<engine::components::VelocityComponent>(e, Vector2{0.f, 0.f});
        ecs.add<engine::components::CircleComponent>(e, radius, radius,
                                                     Color{85, 211, 241, 191});

        fluid_particles.push_back(e);
        placed_positions.push_back(pos);

        ++created;
      }
    }

    logger::info("[FLUID] Created {} centered grid particles",
                 fluid_particles.size());
  } else {
    size_t attempts = 0;
    size_t max_attempts = particle_count * 500;

    while (created < particle_count && attempts < max_attempts) {
      ++attempts;

      Vector2 pos{static_cast<float>(GetRandomValue(radius, CANVAS_W - radius)),
                  static_cast<float>(GetRandomValue(radius, CANVAS_H - radius))};

      bool valid = true;
      for (const auto& other : placed_positions) {
        float dx = pos.x - other.x;
        float dy = pos.y - other.y;
        if ((dx * dx + dy * dy) < min_dist_sq) {
          valid = false;
          break;
        }
      }

      if (!valid) continue;

      engine::Entity e = ecs.create_entity();
      ecs.add<engine::components::PositionComponent>(e, pos);
      ecs.add<engine::components::VelocityComponent>(e, Vector2{0.f, 0.f});
      ecs.add<engine::components::CircleComponent>(e, radius, radius,
                                                   Color{85, 211, 241, 191});

      fluid_particles.push_back(e);
      placed_positions.push_back(pos);
      ++created;
    }

    logger::info("[FLUID] Created {} random spaced particles after {} attempts",
                 fluid_particles.size(), attempts);
  }
}

inline void ResetFluid(engine::ECS& ecs) {
  gravity = 1.0f;
  smoothing_radius = 50.f;
  bounce_enabled = true;
  is_paused = true;
  sim_speed = 1.0f;
  create_centered = true;

  target_density = 0.000425f;
  pressure_multiplier = 250.f;

  particle_size =
    Clamp(2.0f * std::pow(1000.0f / static_cast<float>(PARTICLE_NUMBER), 0.4f),
          0.5f, 3.0f);

  pressure_low_color = {85, 211, 241, 130};
  pressure_mid_color = {255, 255, 255, 130};
  pressure_high_color = {255, 50, 50, 130};

  selection_density = 0.f;
  selection_active = true;
  selection_locked = false;
  selected_particle = {};

  size_t particle_count = fluid_particles.size();

  for (auto e : fluid_particles)
    if (ecs.is_alive(e)) ecs.destroy_entity(e);

  CreateFluid(ecs, particle_count, create_centered);
}

}  // namespace motrix::entities
