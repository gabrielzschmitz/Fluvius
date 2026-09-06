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
#include "simulation.h"

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
 *
 * All simulation parameters/state live in the root entity's
 * SimulationComponent (see simulation.h); only the particle entity cache
 * remains file-local here.
 * ============================================================================
 */

inline std::vector<engine::Entity> fluid_particles;

inline engine::Entity CreateParticleEntity(engine::ECS& ecs, Vector2 pos,
                                           float radius, Color color) {
  engine::Entity e = ecs.create_entity();
  ecs.add<engine::components::PositionComponent>(e, pos);
  ecs.add<engine::components::VelocityComponent>(e, Vector2{0.f, 0.f});
  ecs.add<engine::components::CircleComponent>(e, radius, color);
  return e;
}

inline void CreateFluid(engine::ECS& ecs, size_t particle_count = 10000,
                        bool centered = false) {
  fluid_particles.clear();
  fluid_particles.reserve(particle_count);

  std::vector<Vector2> placed_positions;
  placed_positions.reserve(particle_count);

  auto& sim = Simulation(ecs);
  float radius = sim.particle_size;
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

        engine::Entity e =
          CreateParticleEntity(ecs, pos, radius, Color{85, 211, 241, 191});

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

      Vector2 pos{
        static_cast<float>(GetRandomValue(radius, CANVAS_W - radius)),
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

      engine::Entity e =
        CreateParticleEntity(ecs, pos, radius, Color{85, 211, 241, 191});

      fluid_particles.push_back(e);
      placed_positions.push_back(pos);
      ++created;
    }

    logger::info("[FLUID] Created {} random spaced particles after {} attempts",
                 fluid_particles.size(), attempts);
  }
}

inline void ResetFluid(engine::ECS& ecs) {
  auto& sim = Simulation(ecs);

  sim.gravity = 1.0f;
  sim.smoothing_radius = 50.f;
  sim.is_paused = true;
  sim.sim_speed = 1.0f;
  sim.create_centered = true;

  sim.target_density = 0.000425f;
  sim.pressure_multiplier = 250.f;
  sim.velocity_damping = 0.997f;

  sim.particle_size = DefaultParticleSize(sim.particle_count);

  sim.pressure_low_color = {85, 211, 241, 130};
  sim.pressure_mid_color = {255, 255, 255, 130};
  sim.pressure_high_color = {255, 50, 50, 130};

  sim.selection_density = 0.f;
  sim.selection_active = true;
  sim.selection_locked = false;
  sim.selected_particle = {};

  size_t particle_count = static_cast<size_t>(sim.particle_count);

  for (auto e : fluid_particles)
    if (ecs.is_alive(e)) ecs.destroy_entity(e);

  CreateFluid(ecs, particle_count, sim.create_centered);

  sim.particle_cache_dirty = true;
  sim.needs_reset = false;
}

}  // namespace motrix::entities
