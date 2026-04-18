// entities/canvas.h
#pragma once

#include "../engine/components/canvas.h"
#include "../engine/ecs/ecs.h"
#include "../engine/globals.h"

namespace motrix::entities {

inline engine::Entity CreateCanvas(engine::ECS& ecs,
                              Vector2 position = {CANVAS_W / 2.f, CANVAS_H / 2.f},
                              Vector2 size = {CANVAS_W, CANVAS_H},
                              float rotation = 0.f) {
  engine::Entity e = ecs.create_entity();
  ecs.add<engine::components::CanvasComponent>(e, position, size, rotation);
  return e;
}

}  // namespace motrix::entities