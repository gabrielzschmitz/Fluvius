// app/scenes/kernel_demo.h
#pragma once

#include "engine/components/camera.h"
#include "engine/components/canvas.h"
#include "engine/components/demo_tags.h"
#include "engine/components/physics.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "entities/demo_state.h"

namespace motrix::engine::systems {

inline void CreateKernelDemo(ECS& ecs, size_t particle_count = 100) {
  srand(12345);
  for (int i = 0; i < static_cast<int>(particle_count); ++i) {
    float x = static_cast<float>(rand() % (CANVAS_W / 2));
    float y = static_cast<float>(rand() % CANVAS_H);

    Entity e = ecs.create_entity();
    ecs.add<components::PositionComponent>(e, Vector2{x, y});
    ecs.add<components::CircleComponent>(
      e, 4.f,
      Color{static_cast<unsigned char>(100 + rand() % 155),
            static_cast<unsigned char>(100 + rand() % 155),
            static_cast<unsigned char>(200 + rand() % 55), 255});
    ecs.add<components::KernelParticleTag>(e);
  }

  logger::info("[KERNEL] Created {} kernel particles", particle_count);
}

inline void RenderKernel(ECS& ecs, const components::CameraComponent& cam) {
  auto& ks = motrix::entities::KernelDemo(ecs);
  float canvasX = 0, canvasY = 0;
  ecs.group_view<components::CanvasComponent>(
    [&](Entity, components::CanvasComponent& canvasComp) {
      canvasX = canvasComp.position.x;
      canvasY = canvasComp.position.y;
    });

  float halfCanvasW = CANVAS_W / 2.f;
  float halfCanvasH = CANVAS_H / 2.f;
  float canvasTop = canvasY - halfCanvasH;

  Vector2 worldLeft = {canvasX - halfCanvasW, canvasY};
  Vector2 worldRight = {canvasX + halfCanvasW, canvasY};
  Vector2 worldTop = {canvasX, canvasY - halfCanvasH};
  Vector2 worldBottom = {canvasX, canvasY + halfCanvasH};
  Vector2 worldMiddle = {canvasX, canvasY};

  Vector2 screenCanvasLeft = GetWorldToScreen2D(worldLeft, cam.camera);
  Vector2 screenCanvasRight = GetWorldToScreen2D(worldRight, cam.camera);
  Vector2 screenCanvasTop = GetWorldToScreen2D(worldTop, cam.camera);
  Vector2 screenCanvasBottom = GetWorldToScreen2D(worldBottom, cam.camera);
  Vector2 screenMiddle = GetWorldToScreen2D(worldMiddle, cam.camera);

  float screenHalfW = screenCanvasRight.x - screenCanvasLeft.x;
  float screenHalfH = screenCanvasBottom.y - screenCanvasTop.y;

  BeginMode2D(cam.camera);

  BeginScissorMode((int)screenCanvasLeft.x, (int)screenCanvasTop.y,
                   (int)screenHalfW, (int)screenHalfH);

  ecs.group_view<components::KernelParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::KernelParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float worldX = (canvasX - halfCanvasW) + pos.position.x;
      float worldY = canvasTop + pos.position.y;
      DrawCircleV(Vector2{worldX, worldY}, circ.radius, circ.color);
    });

  EndScissorMode();

  BeginScissorMode((int)screenMiddle.x, (int)screenCanvasTop.y,
                   (int)(screenCanvasRight.x - screenMiddle.x),
                   (int)screenHalfH);

  ecs.group_view<components::KernelParticleTag, components::PositionComponent,
                 components::CircleComponent>(
    [&](Entity, components::KernelParticleTag&,
        components::PositionComponent& pos, components::CircleComponent& circ) {
      float baseX = canvasX + pos.position.x;
      float worldY = canvasTop + pos.position.y;

      float blurRadius = 50.0f * ks.blur_intensity;
      if (blurRadius > 0.5f) {
        DrawCircleGradient(Vector2{baseX, worldY}, blurRadius, circ.color,
                           Color{circ.color.r, circ.color.g, circ.color.b, 0});
      }

      DrawCircleV(Vector2{baseX, worldY}, circ.radius, circ.color);
    });

  DrawLineEx(Vector2{canvasX, canvasTop},
             Vector2{canvasX, canvasY + halfCanvasH}, 3.f, LIGHTGRAY);

  EndScissorMode();

  EndMode2D();
}

}  // namespace motrix::engine::systems

#include "engine/components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "entities/ui.h"

namespace motrix::entities {

inline void CreateKernelDemoUI(engine::ECS& ecs) {
  engine::Entity window =
    AddWindow(ecs, {20.f, CANVAS_H * uiScale / 2.f + 20.f}, 250.f, 70.f,
              "Kernel Demo Controls");
  auto& ks = KernelDemo(ecs);

  AddSlider(ecs, window, engine::INVALID_ENTITY, "Blur",
            [&ks]() { return ks.blur_intensity; },
            [&ks](float value) { ks.blur_intensity = value; },
            0.f, 1.f, 0.01f, nullptr,
            "Controls the blur intensity on the right side.");
}

}  // namespace motrix::entities

#include "app/app_state.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "entities/camera.h"
#include "entities/canvas.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline void InitKernelDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvasWithHandles(state.ecs, false);
  m_eng::systems::CreateKernelDemo(
    state.ecs, m_ett::KernelDemo(state.ecs).particles_num);
  m_ett::CreateKernelDemoUI(state.ecs);
}

inline void UpdateKernelDemo(AppState& state, float dt) {
  (void)dt;

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderKernelDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);

  m_eng::systems::RenderCanvas(state.ecs, cam);

  m_eng::systems::RenderKernel(state.ecs, cam);
}

}  // namespace motrix::app
