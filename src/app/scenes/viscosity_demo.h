// app/scenes/viscosity_demo.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/components/physics.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/fluid_render.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/demo_state.h"
#include "entities/simulation.h"
#include "entities/ui.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

// Demo-specific SPH kernels. These deliberately use different tuning than the
// canonical kernels in engine/systems/sph_kernels.h (3x factor and h3/h5
// normalization) to make the viscosity contrast visually obvious.
inline float DemoSpikyKernelGradient(float r, float h) {
  if (r <= 0.f || r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  float v = h - r;
  return -45.f / (3.14159f * h5) * v * v;
}

inline float DemoViscosityKernel(float r, float h) {
  if (r >= h) return 0.f;
  float h3 = h * h * h;
  return 45.f / (3.14159f * h3) * (h - r);
}

inline void SimulateViscSide(m_eng::ECS& ecs, float dt, float viscosity,
                             m_eng::Entity* entities, int count, float min_x,
                             float max_x) {
  auto& vs = m_ett::ViscosityDemo(ecs);
  float h = 50.f;
  float h2 = h * h;
  float mass = 4.f;
  float target_density = 0.005f;
  float pressure_mult = 50.f;
  float gravity = 150.f;
  float damping = 0.97f;

  vs.entities.clear();
  vs.positions.clear();
  vs.velocities.clear();
  vs.densities.clear();
  vs.pressures.clear();
  vs.pressure_forces.clear();
  vs.viscosity_forces.clear();

  for (int i = 0; i < count; ++i) {
    auto& pos = ecs.get<m_eng::components::PositionComponent>(entities[i]);
    auto& vel = ecs.get<m_eng::components::VelocityComponent>(entities[i]);
    vs.entities.push_back(entities[i]);
    vs.positions.push_back(pos.position);
    vs.velocities.push_back(vel.velocity);
  }

  size_t n = vs.entities.size();
  if (n == 0) return;

  vs.densities.resize(n);
  vs.pressures.resize(n);
  vs.pressure_forces.resize(n, {0.f, 0.f});
  vs.viscosity_forces.resize(n, {0.f, 0.f});

  vs.spatial_grid.clear();
  for (size_t i = 0; i < n; ++i) {
    int cell_x = static_cast<int>(vs.positions[i].x / h);
    int cell_y = static_cast<int>(vs.positions[i].y / h);
    m_eng::components::ViscosityDemoState::Cell cell{cell_x, cell_y};
    vs.spatial_grid[cell].push_back(i);
  }

  for (size_t i = 0; i < n; ++i) {
    float rho = 0.f;
    int cell_x = static_cast<int>(vs.positions[i].x / h);
    int cell_y = static_cast<int>(vs.positions[i].y / h);

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        m_eng::components::ViscosityDemoState::Cell cell{cell_x + dx,
                                                         cell_y + dy};
        auto it = vs.spatial_grid.find(cell);
        if (it == vs.spatial_grid.end()) continue;
        for (size_t idx : it->second) {
          float dxp = vs.positions[idx].x - vs.positions[i].x;
          float dyp = vs.positions[idx].y - vs.positions[i].y;
          float r2 = dxp * dxp + dyp * dyp;
          rho += mass * m_eng::systems::Poly6Kernel(r2, h);
        }
      }
    }
    vs.densities[i] = rho;
    vs.pressures[i] = std::max(0.f, pressure_mult * (rho - target_density));
  }

  for (size_t i = 0; i < n; ++i) {
    if (vs.densities[i] < 0.0001f) continue;

    int cell_x = static_cast<int>(vs.positions[i].x / h);
    int cell_y = static_cast<int>(vs.positions[i].y / h);

    float p_i = vs.pressures[i];
    float rho_i = vs.densities[i];
    float inv_rho_i_sq = 1.f / (rho_i * rho_i);

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        m_eng::components::ViscosityDemoState::Cell cell{cell_x + dx,
                                                         cell_y + dy};
        auto it = vs.spatial_grid.find(cell);
        if (it == vs.spatial_grid.end()) continue;

        for (size_t j : it->second) {
          if (i == j) continue;

          float dxp = vs.positions[j].x - vs.positions[i].x;
          float dyp = vs.positions[j].y - vs.positions[i].y;
          float r2 = dxp * dxp + dyp * dyp;
          if (r2 >= h2 || r2 <= 0.0001f) continue;
          float r = sqrtf(r2);
          Vector2 dir{dxp / r, dyp / r};

          float grad = DemoSpikyKernelGradient(r, h);

          float rho_j = vs.densities[j];
          if (rho_j < 0.0001f) continue;
          float inv_rho_j_sq = 1.f / (rho_j * rho_j);

          float p_j = vs.pressures[j];
          float term1 = p_i * inv_rho_i_sq;
          float term2 = p_j * inv_rho_j_sq;
          float term = term1 + term2;
          float factor = -mass * term * grad;
          vs.pressure_forces[i].x += dir.x * factor;
          vs.pressure_forces[i].y += dir.y * factor;

          float vk = DemoViscosityKernel(r, h);
          float dvx = vs.velocities[j].x - vs.velocities[i].x;
          float dvy = vs.velocities[j].y - vs.velocities[i].y;
          vs.viscosity_forces[i].x += dvx * vk;
          vs.viscosity_forces[i].y += dvy * vk;
        }
      }
    }
  }

  for (size_t i = 0; i < n; ++i) {
    float rho = vs.densities[i];
    float inv_rho = (rho > 0.01f) ? (1.f / rho) : 100.f;
    inv_rho = std::min(inv_rho, 1000.f);

    vs.velocities[i].x += vs.pressure_forces[i].x * dt * inv_rho;
    vs.velocities[i].y += vs.pressure_forces[i].y * dt * inv_rho;
    vs.velocities[i].x +=
      vs.viscosity_forces[i].x * viscosity * dt * inv_rho;
    vs.velocities[i].y +=
      vs.viscosity_forces[i].y * viscosity * dt * inv_rho;
    vs.velocities[i].y += gravity * dt;

    vs.velocities[i].x *= damping;
    vs.velocities[i].y *= damping;

    float max_speed = 200.f;
    float speed = sqrtf(vs.velocities[i].x * vs.velocities[i].x +
                        vs.velocities[i].y * vs.velocities[i].y);
    if (speed > max_speed) {
      vs.velocities[i].x = (vs.velocities[i].x / speed) * max_speed;
      vs.velocities[i].y = (vs.velocities[i].y / speed) * max_speed;
    }

    vs.positions[i].x += vs.velocities[i].x * dt;
    vs.positions[i].y += vs.velocities[i].y * dt;

    float margin = 50.f;
    float top = margin;
    float bottom = (float)CANVAS_H - margin;
    float leftBound = min_x + margin;
    float rightBound = max_x - margin;

    if (vs.positions[i].x < leftBound) {
      vs.positions[i].x = leftBound;
      vs.velocities[i].x *= -0.5f;
    }
    if (vs.positions[i].x > rightBound) {
      vs.positions[i].x = rightBound;
      vs.velocities[i].x *= -0.5f;
    }
    if (vs.positions[i].y < top) {
      vs.positions[i].y = top;
      vs.velocities[i].y *= -0.5f;
    }
    if (vs.positions[i].y > bottom) {
      vs.positions[i].y = bottom;
      vs.velocities[i].y *= -0.5f;
    }
  }

  for (size_t i = 0; i < n; ++i) {
    auto& pos = ecs.get<m_eng::components::PositionComponent>(vs.entities[i]);
    auto& vel = ecs.get<m_eng::components::VelocityComponent>(vs.entities[i]);
    auto& circ = ecs.get<m_eng::components::CircleComponent>(vs.entities[i]);

    pos.position = vs.positions[i];
    vel.velocity = vs.velocities[i];
    circ.density = vs.densities[i];
    circ.pressure = vs.pressures[i];
  }
}

inline void ResetViscosityDemo(m_eng::ECS& ecs) {
  auto& vs = m_ett::ViscosityDemo(ecs);
  for (int i = 0; i < vs.left_count; ++i)
    ecs.destroy_entity(vs.left_particles[i]);
  for (int i = 0; i < vs.right_count; ++i)
    ecs.destroy_entity(vs.right_particles[i]);
  vs.left_count = 0;
  vs.right_count = 0;

  float halfW = CANVAS_W / 2.f;
  srand(42);
  int count = static_cast<int>(vs.particles_float);

  for (int i = 0; i < count; ++i) {
    float x = 80.f + static_cast<float>(rand() % 100);
    float y = 100.f + static_cast<float>(i * 15 % (CANVAS_H - 200));

    m_eng::Entity e_left = ecs.create_entity();
    ecs.add<m_eng::components::PositionComponent>(e_left, Vector2{x, y});
    ecs.add<m_eng::components::VelocityComponent>(e_left, Vector2{0.f, 0.f});
    ecs.add<m_eng::components::CircleComponent>(e_left, 4.f,
                                                Color{85, 211, 241, 191});
    vs.left_particles[vs.left_count++] = e_left;

    m_eng::Entity e_right = ecs.create_entity();
    ecs.add<m_eng::components::PositionComponent>(e_right,
                                                  Vector2{x + halfW, y});
    ecs.add<m_eng::components::VelocityComponent>(e_right, Vector2{0.f, 0.f});
    ecs.add<m_eng::components::CircleComponent>(e_right, 4.f,
                                                Color{85, 211, 241, 191});
    vs.right_particles[vs.right_count++] = e_right;
  }

  logger::info("[VISCOSITY] Created {} particles on each side", count);
}

inline void CreateViscosityDemoUI(m_eng::ECS& ecs) {
  auto& vs = m_ett::ViscosityDemo(ecs);
  m_eng::Entity window = m_ett::AddWindow(ecs, {20.f, 20.f}, 250.f, 100.f,
                                          "Viscosity Demo Controls");
  m_ett::AddSlider(ecs, window, m_eng::INVALID_ENTITY, "Particles",
                   [&vs]() { return vs.particles_float; },
                   [&vs](float value) { vs.particles_float = value; },
                   50.f, 500.f, 10.f, nullptr,
                   "Number of particles on each side.");
}

inline void InitViscosityDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvasWithHandles(state.ecs, false);

  m_ett::Simulation(state.ecs).is_paused = false;
  m_ett::Simulation(state.ecs).render_particle_velocity = true;

  ResetViscosityDemo(state.ecs);
  CreateViscosityDemoUI(state.ecs);
}

inline void UpdateViscosityDemo(AppState& state, float dt) {
  auto& vs = m_ett::ViscosityDemo(state.ecs);
  static int prev_count = -1;
  int new_count = static_cast<int>(vs.particles_float);
  if (prev_count < 0 || new_count != prev_count) {
    prev_count = new_count;
    ResetViscosityDemo(state.ecs);
  }

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  static bool key_p_was_down = false;
  if (IsKeyDown(KEY_P) && !key_p_was_down)
    m_ett::Simulation(state.ecs).is_paused =
      !m_ett::Simulation(state.ecs).is_paused;
  key_p_was_down = IsKeyDown(KEY_P);

  static bool key_r_was_down = false;
  if (IsKeyDown(KEY_R) && !key_r_was_down) ResetViscosityDemo(state.ecs);
  key_r_was_down = IsKeyDown(KEY_R);

  if (m_ett::Simulation(state.ecs).is_paused) {
    m_eng::systems::UpdateCamera2D(state.ecs);
    return;
  }

  float sim_speed = 4.0f;
  float halfW = CANVAS_W / 2.f;
  SimulateViscSide(state.ecs, dt * sim_speed, vs.left, vs.left_particles.data(),
                   vs.left_count, 0.f, halfW);
  SimulateViscSide(state.ecs, dt * sim_speed, vs.right,
                   vs.right_particles.data(), vs.right_count, halfW, CANVAS_W);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderViscosityDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  auto& sim = m_ett::Simulation(state.ecs);
  m_eng::systems::RenderCanvas(state.ecs, cam);

  BeginMode2D(cam.camera);

  state.ecs.group_view<m_eng::components::PositionComponent,
                       m_eng::components::VelocityComponent,
                       m_eng::components::CircleComponent>(
    [&](m_eng::Entity, m_eng::components::PositionComponent& pos,
        m_eng::components::VelocityComponent& vel,
        m_eng::components::CircleComponent& circ) {
      Color particle_color = m_eng::systems::VelocityToColor(vel.velocity, sim);
      DrawCircleV(pos.position, circ.radius, particle_color);
      if (sim.render_particle_velocity) {
        m_eng::systems::RenderArrow(pos.position, vel.velocity, circ.radius,
                                    particle_color, 0.25f);
      }
    });

  float halfW = CANVAS_W / 2.f;

  DrawLineEx(Vector2{halfW, 0}, Vector2{halfW, CANVAS_H}, 3.f, LIGHTGRAY);
  EndMode2D();
}

}  // namespace motrix::app
