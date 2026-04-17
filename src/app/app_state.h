// app/app_state.h
#pragma once

#include "engine/ecs/ecs.h"
#include "raylib.h"

namespace motrix::app {

struct AppState {
  motrix::engine::Entity cameraEntity{0};
  RenderTexture2D bgTexture{};
  engine::ECS ecs;
};

}  // namespace motrix::app
