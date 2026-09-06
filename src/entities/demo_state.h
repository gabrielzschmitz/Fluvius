// entities/demo_state.h
#pragma once

#include "../engine/components/demo_state.h"
#include "../engine/ecs/ecs.h"
#include "simulation.h"

namespace motrix::entities {

/**
 * Per-demo editor state accessors. Attached lazily to the simulation root so
 * slider values and per-demo scratch survive scene switches while staying
 * ECS-resident (same pattern as systems::Physics for the physics buffers).
 */
inline engine::components::DensityDemoState& DensityDemo(engine::ECS& ecs) {
  using C = engine::components::DensityDemoState;
  if (!ecs.has<C>(simulation_root)) ecs.add<C>(simulation_root);
  return ecs.get<C>(simulation_root);
}

inline engine::components::KernelDemoState& KernelDemo(engine::ECS& ecs) {
  using C = engine::components::KernelDemoState;
  if (!ecs.has<C>(simulation_root)) ecs.add<C>(simulation_root);
  return ecs.get<C>(simulation_root);
}

inline engine::components::SmoothingDemoState& SmoothingDemo(engine::ECS& ecs) {
  using C = engine::components::SmoothingDemoState;
  if (!ecs.has<C>(simulation_root)) ecs.add<C>(simulation_root);
  return ecs.get<C>(simulation_root);
}

inline engine::components::ViscosityDemoState& ViscosityDemo(
  engine::ECS& ecs) {
  using C = engine::components::ViscosityDemoState;
  if (!ecs.has<C>(simulation_root)) ecs.add<C>(simulation_root);
  return ecs.get<C>(simulation_root);
}

}  // namespace motrix::entities