// entities/ui.h
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/components/ui.h"
#include "../engine/ecs/ecs.h"
#include "fluid.h"

namespace motrix::entities {

/**
   * ============================================================================
   * GUI Entities
   * ============================================================================
   *
   * Declarative ECS GUI:
   *
   * Window
   *   ├── Slider
   *   ├── Dropdown
   *   ├── Checkbox
   *   ├── Button
   *   └── Text
   *
   * Layout is automatic.
   * Behavior lives in callbacks.
   *
   * ============================================================================
   */

namespace ec = motrix::engine::components;

inline const std::unordered_map<std::string, Color>& PressureColors() {
  static const std::unordered_map<std::string, Color> map = {
    {"White", {255, 255, 255, 130}},
    {"Red", {255, 50, 50, 130}},
    {"Blue", {0, 0, 128, 130}},
    {"Green", {0, 179, 90, 130}}};
  return map;
}

inline const std::unordered_map<std::string, Color>& ParticleColors() {
  static const std::unordered_map<std::string, Color> map = {
    {"Purple", {180, 80, 255, 190}}, {"Blue", {0, 120, 255, 190}},
    {"Cyan", {0, 255, 255, 190}},    {"Green", {0, 200, 100, 190}},
    {"Yellow", {255, 220, 0, 180}},  {"Red", {255, 50, 50, 160}},
    {"White", {255, 255, 255, 190}}};
  return map;
}

// WHITE, RED, GREEN, BLUE
inline int low_color_index = 3;
inline int mid_color_index = 0;
inline int high_color_index = 1;

// PURPLE, BLUE, CYAN, GREEN, YELLOW, RED, WHITE
inline int particle_low_color_index = 1;
inline int particle_mid_low_color_index = 2;
inline int particle_mid_high_color_index = 4;
inline int particle_high_color_index = 5;

//
// UI widget builders. Each builder creates an entity with a layout child
// slot, a resolved rect, the widget component, and (optionally) a tooltip and
// group membership.
//

inline engine::Entity AddWindow(engine::ECS& ecs, Vector2 position, float width,
                                float height, const std::string& title,
                                bool auto_height = true) {
  engine::Entity window = ecs.create_entity();
  ecs.add<ec::UIWindowComponent>(
    window, ec::UIWindowComponent{position, width, height, title});
  ecs.get<ec::UIWindowComponent>(window).auto_height = auto_height;
  return window;
}

inline engine::Entity AddGroup(engine::ECS& ecs, engine::Entity window,
                               const std::string& title) {
  engine::Entity group = ecs.create_entity();
  ecs.add<ec::UILayoutChildComponent>(
    group, ec::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<ec::UIResolvedRectComponent>(group);
  ecs.add<ec::UIGroupComponent>(group, ec::UIGroupComponent{title, true});
  return group;
}

inline engine::Entity AddWidget(engine::ECS& ecs, engine::Entity parent,
                                float width, float height) {
  engine::Entity e = ecs.create_entity();
  ecs.add<ec::UILayoutChildComponent>(
    e, ec::UILayoutChildComponent{parent, width, height});
  ecs.add<ec::UIResolvedRectComponent>(e);
  return e;
}

inline void AddText(engine::ECS& ecs, engine::Entity window,
                    engine::Entity group, const std::string& text,
                    const std::string& tooltip = {}, float width = 0.f,
                    float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UITextComponent>(e, ec::UITextComponent(text));
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddSlider(engine::ECS& ecs, engine::Entity window,
                      engine::Entity group, const std::string& label,
                      float* value, float min, float max, float step,
                      std::function<void(float)> on_change = {},
                      const std::string& tooltip = {}, float height = 30.f) {
  engine::Entity e = AddWidget(ecs, window, -1.f, height);
  ecs.add<ec::UISliderComponent>(
    e, ec::UISliderComponent{label, value, min, max, step,
                             std::move(on_change)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddCheckbox(engine::ECS& ecs, engine::Entity window,
                        engine::Entity group, const std::string& label,
                        bool* value, std::function<void(bool)> on_change = {},
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UICheckboxComponent>(
    e, ec::UICheckboxComponent{label, value, std::move(on_change)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddButton(engine::ECS& ecs, engine::Entity window,
                      engine::Entity group, const std::string& label,
                      std::function<void()> on_click,
                      const std::string& tooltip = {}, float width = 0.f,
                      float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UIButtonComponent>(
    e, ec::UIButtonComponent{label, std::move(on_click)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddDropdown(engine::ECS& ecs, engine::Entity window,
                        engine::Entity group, const std::string& label,
                        std::vector<std::string> options, int* selected,
                        std::function<void(const std::string&)> on_select,
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UIDropdownComponent>(
    e, ec::UIDropdownComponent{label, std::move(options), selected,
                               std::move(on_select)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddNewLine(engine::ECS& ecs, engine::Entity window,
                       engine::Entity group) {
  engine::Entity e = AddWidget(ecs, window, 0.f, 0.f);
  ecs.add<ec::UINewLineComponent>(e);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void CreateUI(engine::ECS& ecs) {
  engine::Entity window =
    AddWindow(ecs, {20.f, 20.f}, 250.f, CANVAS_H * uiScale / 2.0f,
              "Simulation Controls", /*auto_height=*/false);

  engine::Entity physics_group = AddGroup(ecs, window, "Physics Parameters");

  AddSlider(ecs, window, physics_group, "Gravity", &gravity, 0.f, 5.f, 0.1f,
            [](float value) { gravity = value; },
            "Adjust the downward force of the fluid simulation.");
  AddSlider(ecs, window, physics_group, "Smoothing", &smoothing_radius, 5.f,
            80.f, 1.f, [](float value) { smoothing_radius = value; },
            "Controls SPH smoothing radius used for density calculation.");
  AddSlider(ecs, window, physics_group, "Target Density", &target_density,
            0.00005f, 0.002f, 0.000025f,
            [](float value) { target_density = value; },
            "Desired equilibrium density for SPH pressure.");
  AddSlider(ecs, window, physics_group, "Pressure", &pressure_multiplier, 0.1f,
            250.f, 0.1f, [](float value) { pressure_multiplier = value; },
            "Controls fluid stiffness.");
  AddSlider(ecs, window, physics_group, "Viscosity", &viscosity, 0.f, 200.f,
            1.f, [](float value) { viscosity = value; },
            "Controls fluid viscosity.");
  AddSlider(ecs, window, physics_group, "Tension", &surface_tension, 0.f, 5.f,
            0.05f, [](float value) { surface_tension = value; },
            "Controls fluid surface tension.");
  AddSlider(ecs, window, physics_group, "Damping", &velocity_damping, 0.9500f,
            1.000f, 0.001f, [](float value) { velocity_damping = value; },
            "Velocity damping per frame. Lower values make fluid settle "
            "faster.");

  static float particle_count_value = static_cast<float>(PARTICLE_NUMBER);
  AddSlider(ecs, window, physics_group, "Particle Count", &particle_count_value,
            10.f, 10000.f, 1.f, [](float value) {
              PARTICLE_NUMBER = static_cast<int>(value);
              motrix::entities::needs_reset = true;
            },
            "Adjust the number of particles. Changes will reset the "
            "simulation.");

  AddText(ecs, window, physics_group, "Density: 0",
          "Number of particles inside selection area", 0.f, 20.f);

  engine::Entity particle_group = AddGroup(ecs, window, "Particle Properties");
  AddSlider(ecs, window, particle_group, "Size", &particle_size, 0.01f, 20.f,
            0.01f, [](float value) { particle_size = value; },
            "Control the size of each particle.");

  engine::Entity simulation_group = AddGroup(ecs, window, "Simulation Actions");
  AddSlider(ecs, window, simulation_group, "Speed", &sim_speed, 0.1f, 10.0f,
            0.1f, {}, "Adjust the temporal scale of the fluid physics.");
  AddCheckbox(ecs, window, simulation_group, "Pause", &is_paused, {}, {}, 0.f,
              20.f);
  AddButton(ecs, window, simulation_group, "Reset", [&ecs]() {
              low_color_index = 3;
              mid_color_index = 0;
              high_color_index = 1;

              PARTICLE_NUMBER = 1024;
              motrix::entities::needs_reset = true;
              particle_count_value = 1024.0f;
            },
            "Restore all settings to factory defaults.");

  engine::Entity render_group = AddGroup(ecs, window, "Render Options");
  AddCheckbox(ecs, window, render_group, "Pressure Field", &render_pressure_field,
              {}, "Toggle fluid pressure field rendering.");
  AddCheckbox(ecs, window, render_group, "Particles", &render_fluid_particles, {},
              "Toggle fluid particles rendering.");
  AddCheckbox(ecs, window, render_group, "Velocity Vectors",
              &render_particle_velocity, {},
              "Toggle fluid particles velocity vector rendering.");
  AddCheckbox(ecs, window, render_group, "Fill", &render_fluid_filled, {},
              "Toggle fluid fill rendering.");

  AddText(ecs, window, render_group, "Pressure Colors:",
          "Change the pressure field colors for low/mid/high pressures.", -1.f,
          20.f);
  AddNewLine(ecs, window, render_group);

  const std::vector<std::string> pressure_options = {"White", "Red", "Green",
                                                     "Blue"};
  AddDropdown(ecs, window, render_group, "Low", pressure_options,
              &low_color_index,
              [](const std::string& selection) {
                pressure_low_color = PressureColors().at(selection);
              },
              "Control the color of low pressure areas.");
  AddDropdown(ecs, window, render_group, "Neutral", pressure_options,
              &mid_color_index,
              [](const std::string& selection) {
                pressure_mid_color = PressureColors().at(selection);
              },
              "Control the color of mid pressure areas.");
  AddDropdown(ecs, window, render_group, "High", pressure_options,
              &high_color_index,
              [](const std::string& selection) {
                pressure_high_color = PressureColors().at(selection);
              },
              "Control the color of high pressure areas.");

  AddText(ecs, window, render_group, "Particle Colors:",
          "Change the particle colors for low/mid/high velocity.", -1.f, 20.f);
  AddNewLine(ecs, window, render_group);

  const std::vector<std::string> particle_options = {
    "Purple", "Blue", "Cyan", "Green", "Yellow", "Red", "White"};
  AddDropdown(ecs, window, render_group, "Low", particle_options,
              &particle_low_color_index,
              [](const std::string& selection) {
                particle_low_color = ParticleColors().at(selection);
              },
              "Control the color of low velocity particles.");
  AddDropdown(ecs, window, render_group, "Mid-Low", particle_options,
              &particle_mid_low_color_index,
              [](const std::string& selection) {
                particle_mid_low_color = ParticleColors().at(selection);
              },
              "Control the color of mid-low velocity particles.");
  AddDropdown(ecs, window, render_group, "Mid-High", particle_options,
              &particle_mid_high_color_index,
              [](const std::string& selection) {
                particle_mid_high_color = ParticleColors().at(selection);
              },
              "Control the color of mid-high velocity particles.");
  AddDropdown(ecs, window, render_group, "High", particle_options,
              &particle_high_color_index,
              [](const std::string& selection) {
                particle_high_color = ParticleColors().at(selection);
              },
              "Control the color of high velocity particles.");

  logger::info("[GUI] Created window '{}' (entity:{}:{})",
               ecs.get<ec::UIWindowComponent>(window).title, window.index,
               window.version);
}

}  // namespace motrix::entities