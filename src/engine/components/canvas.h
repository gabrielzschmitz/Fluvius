// engine/components/canvas.h
#pragma once

#include <string_view>

#include "../globals.h"
#include "raylib.h"

namespace motrix::engine::components {

struct CanvasComponent {
  static constexpr std::string_view Name = "Canvas";

  Vector2 position{CANVAS_W / 2.f, CANVAS_H / 2.f};
  Vector2 size{CANVAS_W, CANVAS_H};
  float rotation{0.f};
  Vector2 half_extents{CANVAS_W / 2.f, CANVAS_H / 2.f};

  Vector2 prev_position{CANVAS_W / 2.f, CANVAS_H / 2.f};
  float prev_rotation{0.f};

  bool rotation_dirty = true;

  enum Edge { EdgeNone = 0, Left, Right, Top, Bottom };
  enum Corner { CornerNone = 0, TL, TR, BL, BR };

  Edge dragging_edge = Edge::EdgeNone;
  Corner dragging_corner = Corner::CornerNone;

  float handle_radius = 8.f;
  float edge_tolerance = 10.f;

  CanvasComponent() = default;

  explicit CanvasComponent(Vector2 pos, Vector2 dim, float rot = 0.f)
    : position(pos),
      size(dim),
      rotation(rot),
      half_extents{dim.x / 2.f, dim.y / 2.f},
      prev_position(pos),
      prev_rotation(rot),
      rotation_dirty(true) {}
};

}  // namespace motrix::engine::components