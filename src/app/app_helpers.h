// app/app_helpers.h
#pragma once

#include "engine/components/camera.h"
#include "engine/globals.h"
#include "raylib.h"

namespace motrix::app {

using namespace engine::components;

inline RenderTexture2D CreateBackgroundTexture() {
  const float lineWidth = 2.f;  // width of the outer border
  const int tex_w = CANVAS_W + static_cast<int>(2 * lineWidth);
  const int tex_h = CANVAS_H + static_cast<int>(2 * lineWidth);

  RenderTexture2D target = LoadRenderTexture(tex_w, tex_h);

  BeginTextureMode(target);

  // Fill inner simulation area with a subtle background
  ClearBackground({1, 127, 127, 255});
  // ClearBackground(BLACK);

  // Outer border outside simulation area
  DrawRectangleLinesEx(
    {0.f, 0.f, static_cast<float>(tex_w), static_cast<float>(tex_h)}, lineWidth,
    LIGHTGRAY);

  EndTextureMode();
  return target;
}

inline void RenderBackground(const RenderTexture2D& bgTexture,
                             const engine::components::CameraComponent& cam) {
  ClearBackground(Color{1, 87, 87, 255});
  const float lineWidth = 2.f;  // same as used to create the texture
  BeginMode2D(cam.camera);

  // Draw the texture offset so that inner CANVAS_W x CANVAS_H aligns with (0,0)
  DrawTexture(bgTexture.texture, -lineWidth, -lineWidth, WHITE);

  EndMode2D();
}

}  // namespace motrix::app
