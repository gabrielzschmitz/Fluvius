// entities/camera.h
#pragma once

#include <vector>

#include "../engine/components/camera.h"
#include "../engine/ecs/ecs.h"
#include "../engine/globals.h"
#include "raylib.h"

namespace motrix::entities {

/**
 * ============================================================================
 * Camera Entities
 * ============================================================================
 *
 * Provides camera entities with default settings:
 *   • target at (0,0)
 *   • offset (0,0)
 *   • rotation 0
 *   • zoom 1
 * ============================================================================
 */

inline std::vector<engine::Entity> cameras;

inline engine::Entity CreateCamera(engine::ECS& ecs) {
  engine::Entity e = ecs.create_entity();

  engine::components::CameraComponent cam;
  cam.camera.target = {CANVAS_W * 0.5f, CANVAS_H * 0.5f};
  cam.camera.rotation = 0.f;

  int window_width = GetScreenWidth();
  int window_height = GetScreenHeight();
  cam.camera.zoom =
    std::min(window_width / (float)CANVAS_W, window_height / (float)CANVAS_H);
  cam.camera.offset = Vector2{window_width * 0.5f, window_height * 0.5f};

  cam.uiScale = static_cast<float>(uiScale);

  ecs.add<engine::components::CameraComponent>(e, cam);

  cameras.push_back(e);
  return e;
}

}  // namespace motrix::entities
