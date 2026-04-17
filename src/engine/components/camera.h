// engine/components/camera.h
#pragma once

#include <string_view>

#include "raylib.h"

namespace motrix::engine::components {

/**
 * Camera2D Component
 * Stores the Raylib Camera2D and zoom/offset state
 */
struct CameraComponent {
  static constexpr std::string_view Name = "Camera";

  Camera2D camera{};
  float uiScale = 1.f;

  CameraComponent() {
    camera.target = {0.f, 0.f};
    camera.offset = {0.f, 0.f};
    camera.rotation = 0.f;
    camera.zoom = 1.f;
  }
};

}  // namespace motrix::engine::components
