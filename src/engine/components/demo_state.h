// engine/components/demo_state.h
#pragma once

#include <array>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../ecs/ecs.h"
#include "raylib.h"

namespace motrix::engine::components {

/**
 * Interactive demo state. Each visual demo (density grid, kernel, smoothing,
 * viscosity) keeps the handful of editor-relevant values ECS-resident on the
 * simulation root so no namespace-level globals leak out of the scene. The
 * rendering/update callbacks resolve these via entities::DensityDemo(ecs) etc.
 */
struct DensityDemoState {
  static constexpr std::string_view Name = "DensityDemo";

  int columns = 13;
  int rows = 7;
  float columns_float = 13.f;
  float rows_float = 7.f;
  float smoothing_radius = 150.f;
  float line_width = 2.f;
  float particle_mass = 300.f;
  float font_size = 13.f;
  float particle_size = 11.f;
  float arrow_radius = 6.f;
  int pending_columns = -1;
  int pending_rows = -1;
};

struct KernelDemoState {
  static constexpr std::string_view Name = "KernelDemo";

  float blur_intensity = 0.85f;
  int particles_num = 100;
};

struct SmoothingDemoState {
  static constexpr std::string_view Name = "SmoothingDemo";

  float smoothing_radius = 160.f;
  float strength = 50.f;
  float particle_size = 8.f;
};

struct ViscosityDemoState {
  static constexpr std::string_view Name = "ViscosityDemo";

  struct Cell {
    int x = 0;
    int y = 0;
    bool operator==(const Cell& other) const {
      return x == other.x && y == other.y;
    }
  };

  struct CellHash {
    size_t operator()(const Cell& c) const {
      return (static_cast<size_t>(c.x) * 73856093u) ^
             (static_cast<size_t>(c.y) * 19349663u);
    }
  };

  float particles_float = 200.f;
  float left = 0.f;
  float right = 10.f;

  std::array<Entity, 500> left_particles{};
  std::array<Entity, 500> right_particles{};
  int left_count = 0;
  int right_count = 0;

  std::vector<Entity> entities;
  std::vector<Vector2> positions;
  std::vector<Vector2> velocities;
  std::vector<float> densities;
  std::vector<float> pressures;
  std::vector<Vector2> pressure_forces;
  std::vector<Vector2> viscosity_forces;
  std::unordered_map<Cell, std::vector<size_t>, CellHash> spatial_grid;
};

}  // namespace motrix::engine::components