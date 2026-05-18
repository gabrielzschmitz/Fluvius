// app/scene.h
#pragma once

#include <cstring>

#include "engine/ecs/ecs.h"

namespace motrix::app {

struct AppState;
using ECS = motrix::engine::ECS;

struct Scene {
  const char* name;
  void (*init)(AppState&);
  void (*update)(AppState&, float dt);
  void (*render)(AppState&);
  void (*create_ui)(ECS&);
};

enum class SceneType { FLUID_SIM, KERNEL_DEMO, SMOOTHING_DEMO, DENSITY_DEMO, PRESSURE_DEMO };

namespace Scenes {

const Scene& Get(SceneType type);
SceneType FindByName(const char* name);

}  // namespace Scenes

}  // namespace motrix::app
