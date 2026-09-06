// entities/simulation.h
#pragma once

#include <cmath>

#include "../engine/components/physics.h"
#include "../engine/components/ui.h"
#include "../engine/ecs/ecs.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::entities {

/**
 * ============================================================================
 * Simulation Root
 * ============================================================================
 *
 * The app and the benchmark each register exactly ONE root entity per ECS.
 * All simulation parameters, render flags, colors and UI selection state live
 * in components on that entity (SimulationComponent / UiStateComponent)
 * instead of namespace-level globals. The root handle is a single immutable
 * routing value, set once per world at registration time.
 */

inline engine::Entity simulation_root{};

inline float DefaultParticleSize(int particle_count) {
  return Clamp(
    2.0f * std::pow(1000.0f / static_cast<float>(particle_count), 0.4f), 0.5f,
    3.0f);
}

inline engine::components::SimulationComponent& Simulation(engine::ECS& ecs) {
  return ecs.get<engine::components::SimulationComponent>(simulation_root);
}

inline engine::components::UiStateComponent& UiState(engine::ECS& ecs) {
  return ecs.get<engine::components::UiStateComponent>(simulation_root);
}

inline void RegisterSimulationRoot(engine::ECS& ecs) {
  simulation_root = ecs.create_entity();
  ecs.add<engine::components::SimulationComponent>(simulation_root);
  ecs.add<engine::components::UiStateComponent>(simulation_root);

  auto& sim = Simulation(ecs);
  sim.particle_size = DefaultParticleSize(sim.particle_count);
}

}  // namespace motrix::entities