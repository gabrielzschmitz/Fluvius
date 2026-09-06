// engine/components/demo_tags.h
#pragma once

#include <string_view>

namespace motrix::engine::components {

/**
 * Tag components distinguishing particles owned by each standalone demo
 * scene. The demos use them with `ECS::group_view<>` to iterate only the
 * particles they manage, keeping demo state out of the fluid simulation.
 */

struct DensityParticleTag {
  static constexpr std::string_view Name = "DensityParticle";
};

struct KernelParticleTag {
  static constexpr std::string_view Name = "KernelParticle";
};

struct SmoothingParticleTag {
  static constexpr std::string_view Name = "SmoothingParticle";
};

}  // namespace motrix::engine::components