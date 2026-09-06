// app/scenes/viscosity_demo.h
#pragma once

#include "app/app_state.h"
#include "engine/components/camera.h"
#include "engine/components/physics.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/systems/camera.h"
#include "engine/systems/canvas.h"
#include "engine/systems/physics.h"
#include "entities/camera.h"
#include "entities/canvas.h"
#include "entities/ui.h"

namespace motrix::app {

namespace m_eng = motrix::engine;
namespace m_ett = motrix::entities;

inline float viscosity_particles_float = 200.0f;
inline float viscosity_left = 0.0f;
inline float viscosity_right = 10.0f;

inline m_eng::Entity leftParticles[500];
inline m_eng::Entity rightParticles[500];
inline int leftParticleCount = 0;
inline int rightParticleCount = 0;

inline std::vector<m_eng::Entity> visc_entities;
inline std::vector<Vector2> visc_positions;
inline std::vector<Vector2> visc_velocities;
inline std::vector<float> visc_densities;
inline std::vector<float> visc_pressures;
inline std::vector<Vector2> visc_pressure_forces;
inline std::vector<Vector2> visc_viscosity_forces;

inline std::unordered_map<m_eng::systems::GridCell, std::vector<size_t>,
                          m_eng::systems::GridCellHash>
  visc_spatial_grid;

inline float ViscSpikyKernelGradient(float r, float h) {
  if (r <= 0.f || r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  float v = h - r;
  return -45.f / (3.14159f * h5) * v * v;
}

inline float ViscViscosityKernel(float r, float h) {
  if (r >= h) return 0.f;
  float h3 = h * h * h;
  return 45.f / (3.14159f * h3) * (h - r);
}

inline void SimulateViscSide(m_eng::ECS& ecs, float dt, float viscosity,
                             m_eng::Entity* entities, int count, float min_x,
                             float max_x) {
  float h = 50.f;
  float h2 = h * h;
  float mass = 4.f;
  float target_density = 0.005f;
  float pressure_mult = 50.f;
  float gravity = 150.f;
  float damping = 0.97f;

  visc_entities.clear();
  visc_positions.clear();
  visc_velocities.clear();
  visc_densities.clear();
  visc_pressures.clear();
  visc_pressure_forces.clear();
  visc_viscosity_forces.clear();

  for (int i = 0; i < count; ++i) {
    auto& pos = ecs.get<m_eng::components::PositionComponent>(entities[i]);
    auto& vel = ecs.get<m_eng::components::VelocityComponent>(entities[i]);
    visc_entities.push_back(entities[i]);
    visc_positions.push_back(pos.position);
    visc_velocities.push_back(vel.velocity);
  }

  size_t n = visc_entities.size();
  if (n == 0) return;

  visc_densities.resize(n);
  visc_pressures.resize(n);
  visc_pressure_forces.resize(n, {0.f, 0.f});
  visc_viscosity_forces.resize(n, {0.f, 0.f});

  visc_spatial_grid.clear();
  for (size_t i = 0; i < n; ++i) {
    int cell_x = static_cast<int>(visc_positions[i].x / h);
    int cell_y = static_cast<int>(visc_positions[i].y / h);
    m_eng::systems::GridCell cell{cell_x, cell_y};
    visc_spatial_grid[cell].push_back(i);
  }

  for (size_t i = 0; i < n; ++i) {
    float rho = 0.f;
    int cell_x = static_cast<int>(visc_positions[i].x / h);
    int cell_y = static_cast<int>(visc_positions[i].y / h);

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        m_eng::systems::GridCell cell{cell_x + dx, cell_y + dy};
        auto it = visc_spatial_grid.find(cell);
        if (it == visc_spatial_grid.end()) continue;
        for (size_t idx : it->second) {
          float dxp = visc_positions[idx].x - visc_positions[i].x;
          float dyp = visc_positions[idx].y - visc_positions[i].y;
          float r2 = dxp * dxp + dyp * dyp;
          rho += mass * m_eng::systems::Poly6Kernel(r2, h);
        }
      }
    }
    visc_densities[i] = rho;
    visc_pressures[i] = std::max(0.f, pressure_mult * (rho - target_density));
  }

  for (size_t i = 0; i < n; ++i) {
    if (visc_densities[i] < 0.0001f) continue;

    int cell_x = static_cast<int>(visc_positions[i].x / h);
    int cell_y = static_cast<int>(visc_positions[i].y / h);

    float p_i = visc_pressures[i];
    float rho_i = visc_densities[i];
    float inv_rho_i_sq = 1.f / (rho_i * rho_i);

    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        m_eng::systems::GridCell cell{cell_x + dx, cell_y + dy};
        auto it = visc_spatial_grid.find(cell);
        if (it == visc_spatial_grid.end()) continue;

        for (size_t j : it->second) {
          if (i == j) continue;

          float dxp = visc_positions[j].x - visc_positions[i].x;
          float dyp = visc_positions[j].y - visc_positions[i].y;
          float r2 = dxp * dxp + dyp * dyp;
          if (r2 >= h2 || r2 <= 0.0001f) continue;
          float r = sqrtf(r2);
          Vector2 dir{dxp / r, dyp / r};

          float grad = ViscSpikyKernelGradient(r, h);

          float rho_j = visc_densities[j];
          if (rho_j < 0.0001f) continue;
          float inv_rho_j_sq = 1.f / (rho_j * rho_j);

          float p_j = visc_pressures[j];
          float term1 = p_i * inv_rho_i_sq;
          float term2 = p_j * inv_rho_j_sq;
          float term = term1 + term2;
          float factor = -mass * term * grad;
          visc_pressure_forces[i].x += dir.x * factor;
          visc_pressure_forces[i].y += dir.y * factor;

          float vk = ViscViscosityKernel(r, h);
          float dvx = visc_velocities[j].x - visc_velocities[i].x;
          float dvy = visc_velocities[j].y - visc_velocities[i].y;
          visc_viscosity_forces[i].x += dvx * vk;
          visc_viscosity_forces[i].y += dvy * vk;
        }
      }
    }
  }

  for (size_t i = 0; i < n; ++i) {
    float rho = visc_densities[i];
    float inv_rho = (rho > 0.01f) ? (1.f / rho) : 100.f;
    inv_rho = std::min(inv_rho, 1000.f);

    visc_velocities[i].x += visc_pressure_forces[i].x * dt * inv_rho;
    visc_velocities[i].y += visc_pressure_forces[i].y * dt * inv_rho;
    visc_velocities[i].x +=
      visc_viscosity_forces[i].x * viscosity * dt * inv_rho;
    visc_velocities[i].y +=
      visc_viscosity_forces[i].y * viscosity * dt * inv_rho;
    visc_velocities[i].y += gravity * dt;

    visc_velocities[i].x *= damping;
    visc_velocities[i].y *= damping;

    float max_speed = 200.f;
    float speed = sqrtf(visc_velocities[i].x * visc_velocities[i].x +
                        visc_velocities[i].y * visc_velocities[i].y);
    if (speed > max_speed) {
      visc_velocities[i].x = (visc_velocities[i].x / speed) * max_speed;
      visc_velocities[i].y = (visc_velocities[i].y / speed) * max_speed;
    }

    visc_positions[i].x += visc_velocities[i].x * dt;
    visc_positions[i].y += visc_velocities[i].y * dt;

    float margin = 50.f;
    float top = margin;
    float bottom = (float)CANVAS_H - margin;
    float leftBound = min_x + margin;
    float rightBound = max_x - margin;

    if (visc_positions[i].x < leftBound) {
      visc_positions[i].x = leftBound;
      visc_velocities[i].x *= -0.5f;
    }
    if (visc_positions[i].x > rightBound) {
      visc_positions[i].x = rightBound;
      visc_velocities[i].x *= -0.5f;
    }
    if (visc_positions[i].y < top) {
      visc_positions[i].y = top;
      visc_velocities[i].y *= -0.5f;
    }
    if (visc_positions[i].y > bottom) {
      visc_positions[i].y = bottom;
      visc_velocities[i].y *= -0.5f;
    }
  }

  for (size_t i = 0; i < n; ++i) {
    auto& pos = ecs.get<m_eng::components::PositionComponent>(visc_entities[i]);
    auto& vel = ecs.get<m_eng::components::VelocityComponent>(visc_entities[i]);
    auto& circ = ecs.get<m_eng::components::CircleComponent>(visc_entities[i]);

    pos.position = visc_positions[i];
    vel.velocity = visc_velocities[i];
    circ.density = visc_densities[i];
    circ.pressure = visc_pressures[i];
  }
}

inline void ResetViscosityDemo(m_eng::ECS& ecs) {
  for (int i = 0; i < leftParticleCount; ++i)
    ecs.destroy_entity(leftParticles[i]);
  for (int i = 0; i < rightParticleCount; ++i)
    ecs.destroy_entity(rightParticles[i]);
  leftParticleCount = 0;
  rightParticleCount = 0;

  float halfW = CANVAS_W / 2.f;
  srand(42);
  int count = static_cast<int>(viscosity_particles_float);

  for (int i = 0; i < count; ++i) {
    float x = 80.f + static_cast<float>(rand() % 100);
    float y = 100.f + static_cast<float>(i * 15 % (CANVAS_H - 200));

    m_eng::Entity e_left = ecs.create_entity();
    ecs.add<m_eng::components::PositionComponent>(e_left, Vector2{x, y});
    ecs.add<m_eng::components::VelocityComponent>(e_left, Vector2{0.f, 0.f});
    ecs.add<m_eng::components::CircleComponent>(e_left, 4.f,
                                                Color{85, 211, 241, 191});
    leftParticles[leftParticleCount++] = e_left;

    m_eng::Entity e_right = ecs.create_entity();
    ecs.add<m_eng::components::PositionComponent>(e_right,
                                                  Vector2{x + halfW, y});
    ecs.add<m_eng::components::VelocityComponent>(e_right, Vector2{0.f, 0.f});
    ecs.add<m_eng::components::CircleComponent>(e_right, 4.f,
                                                Color{85, 211, 241, 191});
    rightParticles[rightParticleCount++] = e_right;
  }

  logger::info("[VISCOSITY] Created {} particles on each side", count);
}

inline void CreateViscosityDemoUI(m_eng::ECS& ecs) {
  m_eng::Entity window = m_ett::AddWindow(ecs, {20.f, 20.f}, 250.f, 100.f,
                                          "Viscosity Demo Controls");
  m_ett::AddSlider(ecs, window, m_eng::INVALID_ENTITY, "Particles",
                   &viscosity_particles_float, 50.f, 500.f, 10.f, nullptr,
                   "Number of particles on each side.");
}

inline void InitViscosityDemo(AppState& state) {
  state.cameraEntity = m_ett::CreateCamera(state.ecs);
  state.canvasEntity = m_ett::CreateCanvasWithHandles(state.ecs, false);

  m_ett::is_paused = false;
  m_ett::render_particle_velocity = true;

  ResetViscosityDemo(state.ecs);
  CreateViscosityDemoUI(state.ecs);
}

inline void UpdateViscosityDemo(AppState& state, float dt) {
  static int prev_count = -1;
  int new_count = static_cast<int>(viscosity_particles_float);
  if (prev_count < 0 || new_count != prev_count) {
    prev_count = new_count;
    ResetViscosityDemo(state.ecs);
  }

  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::UpdateCanvasInteraction(state.ecs, cam);

  static bool key_p_was_down = false;
  if (IsKeyDown(KEY_P) && !key_p_was_down) m_ett::is_paused = !m_ett::is_paused;
  key_p_was_down = IsKeyDown(KEY_P);

  static bool key_r_was_down = false;
  if (IsKeyDown(KEY_R) && !key_r_was_down) ResetViscosityDemo(state.ecs);
  key_r_was_down = IsKeyDown(KEY_R);

  if (m_ett::is_paused) {
    m_eng::systems::UpdateCamera2D(state.ecs);
    return;
  }

  float sim_speed = 4.0f;
  float halfW = CANVAS_W / 2.f;
  SimulateViscSide(state.ecs, dt * sim_speed, viscosity_left, leftParticles,
                   leftParticleCount, 0.f, halfW);
  SimulateViscSide(state.ecs, dt * sim_speed, viscosity_right, rightParticles,
                   rightParticleCount, halfW, CANVAS_W);

  m_eng::systems::UpdateCamera2D(state.ecs);
}

inline void RenderViscosityDemo(AppState& state) {
  auto& cam =
    state.ecs.get<m_eng::components::CameraComponent>(state.cameraEntity);
  m_eng::systems::RenderCanvas(state.ecs, cam);

  BeginMode2D(cam.camera);

  state.ecs.group_view<m_eng::components::PositionComponent,
                       m_eng::components::VelocityComponent,
                       m_eng::components::CircleComponent>(
    [&](m_eng::Entity, m_eng::components::PositionComponent& pos,
        m_eng::components::VelocityComponent& vel,
        m_eng::components::CircleComponent& circ) {
      Color particle_color = m_eng::systems::VelocityToColor(vel.velocity);
      DrawCircleV(pos.position, circ.radius, particle_color);
      if (m_ett::render_particle_velocity) {
        m_eng::systems::RenderArrow(pos.position, vel.velocity, circ.radius,
                                    particle_color, 0.25f);
      }
    });

  float halfW = CANVAS_W / 2.f;

  DrawLineEx(Vector2{halfW, 0}, Vector2{halfW, CANVAS_H}, 3.f, LIGHTGRAY);
  EndMode2D();
}

}  // namespace motrix::app
