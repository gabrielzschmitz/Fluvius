// entities/canvas.h
#pragma once

#include "../engine/components/canvas.h"
#include "../engine/ecs/ecs.h"
#include "../engine/globals.h"

namespace motrix::entities {

inline engine::Entity CreateCanvas(
  engine::ECS& ecs, Vector2 position = {CANVAS_W / 2.f, CANVAS_H / 2.f},
  Vector2 size = {CANVAS_W, CANVAS_H}, float rotation = 0.f,
  bool show_handles = true) {
  engine::Entity e = ecs.create_entity();
  ecs.add<engine::components::CanvasComponent>(e, position, size, rotation);
  ecs.get<engine::components::CanvasComponent>(e).show_handles = show_handles;
  return e;
}

inline engine::Entity CreateCanvasWithHandles(engine::ECS& ecs,
                                               bool show_handles) {
  return CreateCanvas(ecs, {CANVAS_W / 2.f, CANVAS_H / 2.f},
                     {CANVAS_W, CANVAS_H}, 0.f, show_handles);
}

}  // namespace motrix::entities
