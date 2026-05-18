// app/scene_registry.h
#pragma once

#include "app/scene.h"
#include "app/scenes/density_demo.h"
#include "app/scenes/fluid_sim.h"
#include "app/scenes/kernel_demo.h"
#include "app/scenes/pressure_demo.h"
#include "app/scenes/smoothing_demo.h"

namespace motrix::app {

namespace m_ett = motrix::entities;

namespace Scenes {

constexpr Scene FLUID_SIM{"fluid", InitFluidSim, UpdateFluidSim, RenderFluidSim,
                          m_ett::CreateUI};
constexpr Scene KERNEL_DEMO{"kernel", InitKernelDemo, UpdateKernelDemo,
                            RenderKernelDemo, m_ett::CreateKernelDemoUI};
constexpr Scene SMOOTHING_DEMO{"smoothing", InitSmoothingDemo,
                               UpdateSmoothingDemo, RenderSmoothingDemo,
                               m_ett::CreateSmoothingDemoUI};
constexpr Scene DENSITY_DEMO{"density", InitDensityDemo, UpdateDensityDemo,
                             RenderDensityDemo, m_ett::CreateDensityDemoUI};
constexpr Scene PRESSURE_DEMO{"pressure", InitPressureDemo, UpdatePressureDemo,
                              RenderPressureDemo, CreatePressureDemoUI};

inline const Scene& Get(SceneType type) {
  switch (type) {
    case SceneType::FLUID_SIM:
      return FLUID_SIM;
    case SceneType::KERNEL_DEMO:
      return KERNEL_DEMO;
    case SceneType::SMOOTHING_DEMO:
      return SMOOTHING_DEMO;
    case SceneType::DENSITY_DEMO:
      return DENSITY_DEMO;
    case SceneType::PRESSURE_DEMO:
      return PRESSURE_DEMO;
  }
  return FLUID_SIM;
}

struct NameEntry {
  const char* name;
  SceneType type;
};

constexpr NameEntry SCENE_NAMES[] = {
  {"fluid", SceneType::FLUID_SIM},
  {"kernel", SceneType::KERNEL_DEMO},
  {"smoothing", SceneType::SMOOTHING_DEMO},
  {"density", SceneType::DENSITY_DEMO},
  {"pressure", SceneType::PRESSURE_DEMO},
};

inline SceneType FindByName(const char* name) {
  for (const auto& entry : SCENE_NAMES) {
    if (strcmp(entry.name, name) == 0) {
      return entry.type;
    }
  }
  return SceneType::FLUID_SIM;
}

}  // namespace Scenes

}  // namespace motrix::app
