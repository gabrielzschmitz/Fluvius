// engine/systems/canvas.h
#pragma once

#include <cmath>

#include "../../entities/fluid.h"
#include "../components/camera.h"
#include "../components/canvas.h"
#include "../ecs/ecs.h"
#include "raylib.h"
#include "raymath.h"

namespace motrix::engine::systems {

inline Color canvas_background_color() { return {1, 127, 127, 255}; }
inline float canvas_border_width() { return 3.f; }
inline float rotation_snap_increment() { return 15.f * PI / 180.f; }
inline float drag_sensitivity() { return 1.f; }

inline Vector2 WorldToCanvasLocal(const Vector2& world_pos,
                                  const components::CanvasComponent& canvas) {
  float cos_r = std::cos(-canvas.rotation);
  float sin_r = std::sin(-canvas.rotation);
  Vector2 rel{world_pos.x - canvas.position.x, world_pos.y - canvas.position.y};
  return {rel.x * cos_r - rel.y * sin_r, rel.x * sin_r + rel.y * cos_r};
}

inline Vector2 CanvasLocalToWorld(const Vector2& local_pos,
                                  const components::CanvasComponent& canvas) {
  float cos_r = std::cos(canvas.rotation);
  float sin_r = std::sin(canvas.rotation);
  Vector2 rel{local_pos.x * cos_r - local_pos.y * sin_r,
              local_pos.x * sin_r + local_pos.y * cos_r};
  return {canvas.position.x + rel.x, canvas.position.y + rel.y};
}

inline void UpdateCanvasInteraction(ECS& ecs,
                                    const components::CameraComponent& cam) {
  static Vector2 last_mouse_screen{0, 0};
  static Vector2 last_mouse_world{0, 0};

  Vector2 mouse_screen = GetMousePosition();
  Vector2 mouse_world = GetScreenToWorld2D(mouse_screen, cam.camera);

  Vector2 prev_mouse_world = last_mouse_world;

  Vector2 mouse_delta{0, 0};
  if (last_mouse_screen.x != 0 || last_mouse_screen.y != 0) {
    mouse_delta.x = mouse_screen.x - last_mouse_screen.x;
    mouse_delta.y = mouse_screen.y - last_mouse_screen.y;
  }
  last_mouse_screen = mouse_screen;
  last_mouse_world = mouse_world;

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      canvas.prev_position = canvas.position;
    });

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Vector2 local_mouse = WorldToCanvasLocal(mouse_world, canvas);

      bool touching_left =
        local_mouse.x >= -canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= -canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_right =
        local_mouse.x >= canvas.half_extents.x - canvas.edge_tolerance &&
        local_mouse.x <= canvas.half_extents.x + canvas.edge_tolerance;
      bool touching_top =
        local_mouse.y >= -canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= -canvas.half_extents.y + canvas.edge_tolerance;
      bool touching_bottom =
        local_mouse.y >= canvas.half_extents.y - canvas.edge_tolerance &&
        local_mouse.y <= canvas.half_extents.y + canvas.edge_tolerance;

      bool near_corner =
        (touching_left || touching_right) && (touching_top || touching_bottom);

      if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (near_corner) {
          if (touching_left && touching_top)
            canvas.dragging_corner = components::CanvasComponent::Corner::TL;
          else if (touching_right && touching_top)
            canvas.dragging_corner = components::CanvasComponent::Corner::TR;
          else if (touching_left && touching_bottom)
            canvas.dragging_corner = components::CanvasComponent::Corner::BL;
          else if (touching_right && touching_bottom)
            canvas.dragging_corner = components::CanvasComponent::Corner::BR;

          float dx = mouse_world.x - canvas.position.x;
          float dy = mouse_world.y - canvas.position.y;
          canvas.initial_mouse_angle = std::atan2(dy, dx);
        } else if (touching_left) {
          canvas.dragging_edge = components::CanvasComponent::Edge::Left;
        } else if (touching_right) {
          canvas.dragging_edge = components::CanvasComponent::Edge::Right;
        } else if (touching_top) {
          canvas.dragging_edge = components::CanvasComponent::Edge::Top;
        } else if (touching_bottom) {
          canvas.dragging_edge = components::CanvasComponent::Edge::Bottom;
        }
      }

      if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        canvas.dragging_edge = components::CanvasComponent::Edge::EdgeNone;
        canvas.dragging_corner =
          components::CanvasComponent::Corner::CornerNone;
        canvas.prev_rotation = canvas.rotation;
      }

      if (canvas.dragging_edge != components::CanvasComponent::Edge::EdgeNone) {
        if (!entities::is_paused) {
          Vector2 world_delta = mouse_world - prev_mouse_world;

          canvas.position.x += world_delta.x;
          canvas.position.y += world_delta.y;
        }
      }

      if (canvas.dragging_corner !=
          components::CanvasComponent::Corner::CornerNone) {
        if (!entities::is_paused) {
          float dx = mouse_world.x - canvas.position.x;
          float dy = mouse_world.y - canvas.position.y;
          float angle = std::atan2(dy, dx);

          float delta_angle = angle - canvas.initial_mouse_angle;

          if (std::abs(delta_angle) > 0.001f) {
            canvas.rotation = canvas.prev_rotation + delta_angle;
            canvas.rotation_dirty = true;
          }
        }
      }

      if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        if (canvas.dragging_edge !=
              components::CanvasComponent::Edge::EdgeNone ||
            canvas.dragging_corner !=
              components::CanvasComponent::Corner::CornerNone) {
          canvas.dragging_edge = components::CanvasComponent::Edge::EdgeNone;
          canvas.dragging_corner =
            components::CanvasComponent::Corner::CornerNone;
        }
      }
    });
}

inline void RenderCanvas(ECS& ecs, const components::CameraComponent& cam) {
  BeginMode2D(cam.camera);

  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvas) {
      Color bg = canvas_background_color();
      float border_w = canvas_border_width();

      Rectangle rec{canvas.position.x, canvas.position.y, canvas.size.x,
                    canvas.size.y};
      DrawRectanglePro(rec, {canvas.size.x / 2.f, canvas.size.y / 2.f},
                       canvas.rotation * 180.f / PI, bg);

      Vector2 corners[4] = {{-canvas.half_extents.x, -canvas.half_extents.y},
                            {canvas.half_extents.x, -canvas.half_extents.y},
                            {canvas.half_extents.x, canvas.half_extents.y},
                            {-canvas.half_extents.x, canvas.half_extents.y}};

      for (int i = 0; i < 4; i++)
        corners[i] = CanvasLocalToWorld(corners[i], canvas);

      Vector2 tl = corners[0];
      Vector2 tr = corners[1];
      Vector2 br = corners[2];
      Vector2 bl = corners[3];

      DrawLineEx(tl, tr, border_w, LIGHTGRAY);
      DrawLineEx(tr, br, border_w, LIGHTGRAY);
      DrawLineEx(br, bl, border_w, LIGHTGRAY);
      DrawLineEx(bl, tl, border_w, LIGHTGRAY);

      DrawRectangleRec(
        {tl.x - border_w / 2.f, tl.y - border_w / 2.f, border_w, border_w},
        LIGHTGRAY);
      DrawRectangleRec(
        {tr.x - border_w / 2.f, tr.y - border_w / 2.f, border_w, border_w},
        LIGHTGRAY);
      DrawRectangleRec(
        {br.x - border_w / 2.f, br.y - border_w / 2.f, border_w, border_w},
        LIGHTGRAY);
      DrawRectangleRec(
        {bl.x - border_w / 2.f, bl.y - border_w / 2.f, border_w, border_w},
        LIGHTGRAY);

      if (canvas.show_handles) {
        Vector2 handle_positions[4] = {
          {-canvas.half_extents.x, -canvas.half_extents.y},
          {canvas.half_extents.x, -canvas.half_extents.y},
          {canvas.half_extents.x, canvas.half_extents.y},
          {-canvas.half_extents.x, canvas.half_extents.y}};

        for (int i = 0; i < 4; i++) {
          handle_positions[i] = CanvasLocalToWorld(handle_positions[i], canvas);
          DrawCircleV(handle_positions[i], canvas.handle_radius, LIGHTGRAY);
        }
      }
    });

  EndMode2D();
}

}  // namespace motrix::engine::systems
