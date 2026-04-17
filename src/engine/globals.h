// engine/globals.h
#pragma once

#include "raylib.h"

/*
 * Common scaled resolutions:
 *
 * Base Resolution | SCALE | Window Size
 * --------------------------------------
 *   320 x 180     |   6   | 1920 x 1080
 *   320 x 180     |   3   |  960 x 540
 *   320 x 180     |   4   | 1280 x 720
 *   640 x 360     |   2   | 1280 x 720
 *  1280 x 720     |   1   | 1280 x 720
 */
inline constexpr int SCALE = 3;
inline constexpr int CANVAS_W = 640;
inline constexpr int CANVAS_H = 360;

inline float uiScale = 2.f;

inline Font defaultFont{};

inline int PARTICLE_NUMBER = 1024;
