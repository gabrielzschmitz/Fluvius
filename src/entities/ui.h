// entities/ui.h
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../engine/components/ui.h"
#include "../engine/ecs/ecs.h"
#include "fluid.h"
#include "simulation.h"

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

/**
 * Binds a component field on the simulation-root entity through getter/setter
 * lambdas. Each access re-resolves `ecs.get<Component>(owner)` so a moved or
 * re-packed component can never dangle.
 *
 * The widget components additionally accept raw std::function<float()> /
 * void(float) bindings (e.g. proxying through Simulation(ecs)); the std::function
 * conversions handle the int<->float hops where needed.
 */
template <typename Component, typename Member>
inline std::pair<std::function<Member()>, std::function<void(Member)>>
FieldBinding(engine::ECS& ecs, engine::Entity owner, Member Component::*member) {
  return {[&ecs, owner, member]() -> Member {
            return ecs.get<Component>(owner).*member;
          },
          [&ecs, owner, member](Member value) {
            ecs.get<Component>(owner).*member = value;
          }};
}

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
                      std::function<float()> get_value,
                      std::function<void(float)> set_value, float min, float max,
                      float step, std::function<void(float)> on_change = {},
                      const std::string& tooltip = {}, float height = 30.f) {
  engine::Entity e = AddWidget(ecs, window, -1.f, height);
  ecs.add<ec::UISliderComponent>(
    e, ec::UISliderComponent{label, std::move(get_value),
                             std::move(set_value), min, max, step,
                             std::move(on_change)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddCheckbox(engine::ECS& ecs, engine::Entity window,
                        engine::Entity group, const std::string& label,
                        std::function<bool()> get_value,
                        std::function<void(bool)> set_value,
                        std::function<void(bool)> on_change = {},
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UICheckboxComponent>(
    e, ec::UICheckboxComponent{label, std::move(get_value),
                               std::move(set_value), std::move(on_change)});
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
                        std::vector<std::string> options,
                        std::function<int()> get_index,
                        std::function<void(int)> set_index,
                        std::function<void(const std::string&)> on_select,
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = 20.f) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UIDropdownComponent>(
    e, ec::UIDropdownComponent{label, std::move(options), std::move(get_index),
                               std::move(set_index), std::move(on_select)});
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

  using SIM = ec::SimulationComponent;

  auto bind = [&](auto member) {
    return FieldBinding<SIM>(ecs, simulation_root, member);
  };

  auto [gravity_get, gravity_set] = bind(&SIM::gravity);
  AddSlider(ecs, window, physics_group, "Gravity", gravity_get, gravity_set,
            0.f, 5.f, 0.1f, {}, "Adjust the downward force of the fluid.");
  auto [smoothing_get, smoothing_set] = bind(&SIM::smoothing_radius);
  AddSlider(ecs, window, physics_group, "Smoothing", smoothing_get,
            smoothing_set, 5.f, 80.f, 1.f, {},
            "Controls SPH smoothing radius used for density calculation.");
  auto [target_get, target_set] = bind(&SIM::target_density);
  AddSlider(ecs, window, physics_group, "Target Density", target_get,
            target_set, 0.00005f, 0.002f, 0.000025f, {},
            "Desired equilibrium density for SPH pressure.");
  auto [pressure_get, pressure_set] = bind(&SIM::pressure_multiplier);
  AddSlider(ecs, window, physics_group, "Pressure", pressure_get, pressure_set,
            0.1f, 250.f, 0.1f, {}, "Controls fluid stiffness.");
  auto [viscosity_get, viscosity_set] = bind(&SIM::viscosity);
  AddSlider(ecs, window, physics_group, "Viscosity", viscosity_get,
            viscosity_set, 0.f, 200.f, 1.f, {}, "Controls fluid viscosity.");
  auto [tension_get, tension_set] = bind(&SIM::surface_tension);
  AddSlider(ecs, window, physics_group, "Tension", tension_get, tension_set,
            0.f, 5.f, 0.05f, {}, "Controls fluid surface tension.");
  auto [damping_get, damping_set] = bind(&SIM::velocity_damping);
  AddSlider(ecs, window, physics_group, "Damping", damping_get, damping_set,
            0.9500f, 1.000f, 0.001f, {},
            "Velocity damping per frame. Lower values make fluid settle "
            "faster.");

  auto [count_get, count_set] = bind(&SIM::particle_count);
  AddSlider(ecs, window, physics_group, "Particle Count", count_get, count_set,
            10.f, 10000.f, 1.f, [&ecs](float) {
              Simulation(ecs).needs_reset = true;
            },
            "Adjust the number of particles. Changes will reset the "
            "simulation.");

  AddText(ecs, window, physics_group, "Density: 0",
          "Number of particles inside selection area", 0.f, 20.f);

  engine::Entity particle_group = AddGroup(ecs, window, "Particle Properties");
  auto [size_get, size_set] = bind(&SIM::particle_size);
  AddSlider(ecs, window, particle_group, "Size", size_get, size_set, 0.01f,
            20.f, 0.01f, {}, "Control the size of each particle.");

  engine::Entity simulation_group = AddGroup(ecs, window, "Simulation Actions");
  auto [speed_get, speed_set] = bind(&SIM::sim_speed);
  AddSlider(ecs, window, simulation_group, "Speed", speed_get, speed_set, 0.1f,
            10.0f, 0.1f, {}, "Adjust the temporal scale of the fluid physics.");
  auto [pause_get, pause_set] = bind(&SIM::is_paused);
  AddCheckbox(ecs, window, simulation_group, "Pause", pause_get, pause_set, {},
              {}, 0.f, 20.f);
  AddButton(ecs, window, simulation_group, "Reset", [&ecs]() {
              auto& state = Simulation(ecs);
              auto& select = UiState(ecs);
              select.low_color_index = 3;
              select.mid_color_index = 0;
              select.high_color_index = 1;
              select.particle_low_color_index = 1;
              select.particle_mid_low_color_index = 2;
              select.particle_mid_high_color_index = 4;
              select.particle_high_color_index = 5;
              state.particle_count = 1024;
              state.needs_reset = true;
            },
            "Restore all settings to factory defaults.");

  engine::Entity render_group = AddGroup(ecs, window, "Render Options");
  auto [pressure_field_get, pressure_field_set] =
    bind(&SIM::render_pressure_field);
  AddCheckbox(ecs, window, render_group, "Pressure Field", pressure_field_get,
              pressure_field_set, {}, "Toggle fluid pressure field rendering.");
  auto [particles_get, particles_set] = bind(&SIM::render_fluid_particles);
  AddCheckbox(ecs, window, render_group, "Particles", particles_get,
              particles_set, {}, "Toggle fluid particles rendering.");
  auto [velocity_get, velocity_set] = bind(&SIM::render_particle_velocity);
  AddCheckbox(ecs, window, render_group, "Velocity Vectors", velocity_get,
              velocity_set, {}, "Toggle fluid particles velocity vector.");
  auto [fill_get, fill_set] = bind(&SIM::render_fluid_filled);
  AddCheckbox(ecs, window, render_group, "Fill", fill_get, fill_set, {},
              "Toggle fluid fill rendering.");

  AddText(ecs, window, render_group, "Pressure Colors:",
          "Change the pressure field colors for low/mid/high pressures.", -1.f,
          20.f);
  AddNewLine(ecs, window, render_group);

  using UI = ec::UiStateComponent;
  auto bind_ui = [&](int UI::*member) {
    return FieldBinding<UI>(ecs, simulation_root, member);
  };

  const std::vector<std::string> pressure_options = {"White", "Red", "Green",
                                                     "Blue"};
  auto [low_get, low_set] = bind_ui(&UI::low_color_index);
  AddDropdown(ecs, window, render_group, "Low", pressure_options, low_get,
              low_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).pressure_low_color =
                  PressureColors().at(selection);
              },
              "Control the color of low pressure areas.");
  auto [mid_get, mid_set] = bind_ui(&UI::mid_color_index);
  AddDropdown(ecs, window, render_group, "Neutral", pressure_options, mid_get,
              mid_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).pressure_mid_color =
                  PressureColors().at(selection);
              },
              "Control the color of mid pressure areas.");
  auto [high_get, high_set] = bind_ui(&UI::high_color_index);
  AddDropdown(ecs, window, render_group, "High", pressure_options, high_get,
              high_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).pressure_high_color =
                  PressureColors().at(selection);
              },
              "Control the color of high pressure areas.");

  AddText(ecs, window, render_group, "Particle Colors:",
          "Change the particle colors for low/mid/high velocity.", -1.f, 20.f);
  AddNewLine(ecs, window, render_group);

  const std::vector<std::string> particle_options = {
    "Purple", "Blue", "Cyan", "Green", "Yellow", "Red", "White"};
  auto [plow_get, plow_set] = bind_ui(&UI::particle_low_color_index);
  AddDropdown(ecs, window, render_group, "Low", particle_options, plow_get,
              plow_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).particle_low_color =
                  ParticleColors().at(selection);
              },
              "Control the color of low velocity particles.");
  auto [pml_get, pml_set] = bind_ui(&UI::particle_mid_low_color_index);
  AddDropdown(ecs, window, render_group, "Mid-Low", particle_options, pml_get,
              pml_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).particle_mid_low_color =
                  ParticleColors().at(selection);
              },
              "Control the color of mid-low velocity particles.");
  auto [pmh_get, pmh_set] = bind_ui(&UI::particle_mid_high_color_index);
  AddDropdown(ecs, window, render_group, "Mid-High", particle_options, pmh_get,
              pmh_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).particle_mid_high_color =
                  ParticleColors().at(selection);
              },
              "Control the color of mid-high velocity particles.");
  auto [phigh_get, phigh_set] = bind_ui(&UI::particle_high_color_index);
  AddDropdown(ecs, window, render_group, "High", particle_options, phigh_get,
              phigh_set,
              [&ecs](const std::string& selection) {
                Simulation(ecs).particle_high_color =
                  ParticleColors().at(selection);
              },
              "Control the color of high velocity particles.");

  logger::info("[GUI] Created window '{}' (entity:{}:{})",
               ecs.get<ec::UIWindowComponent>(window).title, window.index,
               window.version);
}

}  // namespace motrix::entities