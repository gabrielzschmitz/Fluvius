#pragma once

#include <string_view>

struct Position {
  float x, y;
  static constexpr std::string_view Name = "Position";
};

struct Velocity {
  float vx, vy;
  static constexpr std::string_view Name = "Velocity";
};

struct Health {
  int hp;
  static constexpr std::string_view Name = "Health";
};
