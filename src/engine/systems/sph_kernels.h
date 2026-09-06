// engine/systems/sph_kernels.h
//
// Pure math primitives for the SPH (Smoothed-Particle Hydrodynamics) model.
// These functions have no ECS or application dependency and can be reused by
// the fluid simulation and the standalone demo scenes.
#pragma once

#include <cmath>
#include <functional>
#include <unordered_map>

#include "raylib.h"

namespace motrix::engine::systems {

/**
 * ============================================================================
 * Spatial Hash
 * ============================================================================
 */
struct GridCell {
  int x;
  int y;

  bool operator==(const GridCell& other) const {
    return x == other.x && y == other.y;
  }
};

struct GridCellHash {
  size_t operator()(const GridCell& c) const {
    return std::hash<int>()(c.x * 73856093) ^ std::hash<int>()(c.y * 19349663);
  }
};

inline GridCell PositionToCell(Vector2 p, float cell_size) {
  return {static_cast<int>(std::floor(p.x / cell_size)),
          static_cast<int>(std::floor(p.y / cell_size))};
}

/**
 * ============================================================================
 * SPH Kernels
 * ============================================================================
 */
// Poly6: used for density estimation.
inline float Poly6Kernel(float r2, float h) {
  float h2 = h * h;
  if (r2 >= h2) return 0.f;
  float diff = h2 - r2;
  float h9 = h * h * h * h * h * h * h * h * h;
  return 315.f / (64.f * PI * h9) * diff * diff * diff;
}

// Spiky gradient: used for pressure force.
inline float SpikyKernelGradient(float r, float h) {
  if (r <= 0.f || r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  float v = h - r;
  return -15.f / (PI * h5) * v * v;
}

// Viscosity Laplacian: used for viscosity force.
inline float ViscosityKernel(float r, float h) {
  if (r >= h) return 0.f;
  float h5 = h * h * h * h * h;
  return 15.f / (2.f * PI * h5) * (h - r);
}

// Cohesion kernel: quadratic falloff within half the smoothing radius.
inline float CohesionKernel(float r, float h) {
  if (r >= h * 0.5f) return 0.f;
  float q = r / (h * 0.5f);
  return (1.f - q) * (1.f - q);
}

// Smooth squared-distance falloff used for the filled-fluid rendering field.
inline float FluidFieldKernel(float dist_sq, float radius_sq) {
  if (dist_sq >= radius_sq) return 0.f;

  float x = 1.f - (dist_sq / radius_sq);

  return x * x;
}

// Linear interpolation of a scalar field along an edge (marching squares).
inline Vector2 InterpolateEdge(Vector2 a, Vector2 b, float va, float vb,
                               float threshold) {
  if (fabsf(vb - va) < 0.0001f) return a;

  float t = (threshold - va) / (vb - va);

  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

}  // namespace motrix::engine::systems
