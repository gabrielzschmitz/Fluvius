// app/app_state.h
#pragma once

#include "app/scene.h"
#include "engine/ecs/ecs.h"

namespace motrix::app {

struct AppState {
  SceneType currentScene{SceneType::FLUID_SIM};
  motrix::engine::Entity cameraEntity{0};
  motrix::engine::Entity canvasEntity{0};
  engine::ECS ecs;
};

}  // namespace motrix::app
