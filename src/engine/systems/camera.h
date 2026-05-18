// engine/systems/camera.h
#pragma once

#include "../components/camera.h"
#include "../components/ui.h"
#include "../ecs/ecs.h"
#include "../globals.h"
#include "../systems/ui_helpers.h"
#include "raylib.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Camera System
 * Centers CANVAS_W x CANVAS_H in window and allows zoom from center.
 * ============================================================================
 */
inline void UpdateCamera2D(engine::ECS& ecs) {
  bool mouse_over_ui = false;
  ecs.group_view<engine::components::UIWindowComponent>(
    [&](engine::Entity, engine::components::UIWindowComponent& win) {
      Rectangle r = {win.position.x, win.position.y, win.width, win.height};
      if (CheckCollisionPointRec(GetMousePosition(), ScaleRect(r)))
        mouse_over_ui = true;
    });

  ecs.group_view<engine::components::CameraComponent>(
    [&](engine::Entity, engine::components::CameraComponent& cam) {
      int window_width = GetScreenWidth();
      int window_height = GetScreenHeight();

      if (IsKeyPressed(KEY_EQUAL)) {  // '=' key
        cam.camera.target = {CANVAS_W * 0.5f, CANVAS_H * 0.5f};
        cam.camera.rotation = 0.f;
        cam.camera.zoom =
          std::min(window_width / (float)CANVAS_W, window_height / (float)CANVAS_H);
        cam.camera.offset = {window_width * 0.5f, window_height * 0.5f};

        ecs.group_view<components::CanvasComponent>(
          [&](Entity, components::CanvasComponent& canvas) {
            canvas.position = {CANVAS_W / 2.f, CANVAS_H / 2.f};
            canvas.size = {CANVAS_W, CANVAS_H};
            canvas.rotation = 0.f;
            canvas.half_extents = {CANVAS_W / 2.f, CANVAS_H / 2.f};
            canvas.rotation_dirty = true;
          });
      }

      cam.camera.offset = {window_width * 0.5f, window_height * 0.5f};

      if (mouse_over_ui) return;

      // Zoom at mouse position
      float wheel = GetMouseWheelMove();
      if (wheel != 0.f) {
        Vector2 mouseScreen = GetMousePosition();

        // Convert mouse screen position to world position
        Vector2 mouseWorldBefore = GetScreenToWorld2D(mouseScreen, cam.camera);

        // Apply zoom
        cam.camera.zoom *= (1.f + wheel * 0.1f);
        cam.camera.zoom = std::clamp(cam.camera.zoom, 0.1f, 10.f);

        // Convert mouse screen position to world position after zoom
        Vector2 mouseWorldAfter = GetScreenToWorld2D(mouseScreen, cam.camera);

        // Offset the camera to keep the world under the mouse stable
        cam.camera.target.x += mouseWorldBefore.x - mouseWorldAfter.x;
        cam.camera.target.y += mouseWorldBefore.y - mouseWorldAfter.y;
      }

      // Middle mouse drag to pan
      static Vector2 lastMousePos = {0.f, 0.f};
      if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 mouseDelta = {GetMouseX() - lastMousePos.x,
                              GetMouseY() - lastMousePos.y};

        // Adjust camera target based on delta and current zoom
        cam.camera.target.x -= mouseDelta.x / cam.camera.zoom;
        cam.camera.target.y -= mouseDelta.y / cam.camera.zoom;
      }
      lastMousePos = GetMousePosition();
    });
}

}  // namespace motrix::engine::systems
