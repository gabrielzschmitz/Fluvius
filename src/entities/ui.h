// entities/ui.h
#pragma once

#include "../engine/components/ui.h"
#include "../engine/ecs/ecs.h"
#include "../entities/fluid.h"
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

// WHITE, RED, GREEN, BLUE
inline int low_color_index = 3;
inline int mid_color_index = 0;
inline int high_color_index = 1;

inline void CreateUI(engine::ECS& ecs) {
  //
  // Window
  //
  engine::Entity window = ecs.create_entity();

  ecs.add<engine::components::UIWindowComponent>(
    window,
    engine::components::UIWindowComponent{
      {20.f, 20.f}, 250.f, CANVAS_H * uiScale / 2.0f, "Simulation Controls"});
  ecs.get<engine::components::UIWindowComponent>(window).auto_height = false;

  //
  // Physics GROUP
  //
  engine::Entity physics_group = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    physics_group,
    engine::components::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<engine::components::UIResolvedRectComponent>(physics_group);
  ecs.add<engine::components::UIGroupComponent>(
    physics_group,
    engine::components::UIGroupComponent{"Physics Parameters", true});

  //
  // Gravity slider
  //
  engine::Entity gravity_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    gravity_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(gravity_slider);

  ecs.add<engine::components::UISliderComponent>(
    gravity_slider, engine::components::UISliderComponent{
                      "Gravity", &gravity, 0.f, 5.f, 0.1f,
                      [](float value) { gravity = value; }});
  ecs.add<engine::components::UITooltipComponent>(
    gravity_slider, "Adjust the downward force of the fluid simulation.");
  ecs.add<engine::components::UIGroupChildComponent>(gravity_slider,
                                                     physics_group);

  //
  // Smoothing radius slider
  //
  engine::Entity smoothing_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    smoothing_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(smoothing_slider);

  ecs.add<engine::components::UISliderComponent>(
    smoothing_slider, engine::components::UISliderComponent{
                        "Smoothing", &smoothing_radius, 5.f, 80.f, 1.f,
                        [](float value) { smoothing_radius = value; }});

  ecs.add<engine::components::UITooltipComponent>(
    smoothing_slider,
    "Controls SPH smoothing radius used for density calculation.");

  ecs.add<engine::components::UIGroupChildComponent>(smoothing_slider,
                                                     physics_group);

  //
  // Target density slider
  //
  engine::Entity density_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    density_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(density_slider);

  ecs.add<engine::components::UISliderComponent>(
    density_slider, engine::components::UISliderComponent{
                      "Target Density", &target_density, 0.00005f, 0.002f,
                      0.000025f, [](float value) { target_density = value; }});

  ecs.add<engine::components::UITooltipComponent>(
    density_slider, "Desired equilibrium density for SPH pressure.");

  ecs.add<engine::components::UIGroupChildComponent>(density_slider,
                                                     physics_group);

  //
  // Pressure multiplier slider
  //
  engine::Entity pressure_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    pressure_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(pressure_slider);

  ecs.add<engine::components::UISliderComponent>(
    pressure_slider, engine::components::UISliderComponent{
                       "Pressure", &pressure_multiplier, 0.1f, 250.f, 0.1f,
                       [](float value) { pressure_multiplier = value; }});

  ecs.add<engine::components::UITooltipComponent>(pressure_slider,
                                                  "Controls fluid stiffness.");

  ecs.add<engine::components::UIGroupChildComponent>(pressure_slider,
                                                     physics_group);

  //
  // Viscosity slider
  //
  engine::Entity viscosity_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    viscosity_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(viscosity_slider);

  ecs.add<engine::components::UISliderComponent>(
    viscosity_slider, engine::components::UISliderComponent{
                        "Viscosity", &viscosity, 0.1f, 20.f, 0.1f,
                        [](float value) { viscosity = value; }});

  ecs.add<engine::components::UITooltipComponent>(viscosity_slider,
                                                  "Controls fluid viscosity.");

  ecs.add<engine::components::UIGroupChildComponent>(viscosity_slider,
                                                     physics_group);

  //
  // Surface tension slider
  //
  engine::Entity tension_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    tension_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(tension_slider);

  ecs.add<engine::components::UISliderComponent>(
    tension_slider, engine::components::UISliderComponent{
                      "Tension", &surface_tension, 0.f, 5.f, 0.05f,
                      [](float value) { surface_tension = value; }});

  ecs.add<engine::components::UITooltipComponent>(
    tension_slider, "Controls water surface tension.");

  ecs.add<engine::components::UIGroupChildComponent>(tension_slider,
                                                     physics_group);

  //
  // Bounce checkbox
  //
  engine::Entity checkbox = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    checkbox, engine::components::UILayoutChildComponent{window, -1.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(checkbox);

  ecs.add<engine::components::UICheckboxComponent>(
    checkbox,
    engine::components::UICheckboxComponent{
      "Bounce", &bounce_enabled, [](bool value) { bounce_enabled = value; }});
  ecs.add<engine::components::UITooltipComponent>(checkbox,
                                                  "Toggle particle bouncing.");
  ecs.add<engine::components::UIGroupChildComponent>(checkbox, physics_group);

  //
  // Number text
  //
  engine::Entity number_text = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    number_text, engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(number_text);

  ecs.add<engine::components::UITextComponent>(
    number_text, engine::components::UITextComponent(
                   "Particles: " +
                   std::to_string(motrix::entities::fluid_particles.size())));

  ecs.add<engine::components::UITooltipComponent>(
    number_text, "Displays the total number of particles in the simulation.");

  ecs.add<engine::components::UIGroupChildComponent>(number_text,
                                                     physics_group);

  // Density text
  engine::Entity density_text = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    density_text,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(density_text);

  ecs.add<engine::components::UITextComponent>(
    density_text, engine::components::UITextComponent("Density: 0"));

  ecs.add<engine::components::UITooltipComponent>(
    density_text, "Number of particles inside selection area");

  ecs.add<engine::components::UIGroupChildComponent>(density_text,
                                                     physics_group);

  //
  // Pariticle GROUP
  //
  engine::Entity particle_group = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    particle_group,
    engine::components::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<engine::components::UIResolvedRectComponent>(particle_group);
  ecs.add<engine::components::UIGroupComponent>(
    particle_group,
    engine::components::UIGroupComponent{"Particle Properties", true});

  //
  // Size slider
  //
  engine::Entity size_slider = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    size_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});

  ecs.add<engine::components::UIResolvedRectComponent>(size_slider);

  ecs.add<engine::components::UISliderComponent>(
    size_slider, engine::components::UISliderComponent{
                   "Size", &particle_size, 0.01f, 20.f, 0.01f,
                   [](float value) { particle_size = value; }});
  ecs.add<engine::components::UITooltipComponent>(
    size_slider, "Control the size of each particle.");
  ecs.add<engine::components::UIGroupChildComponent>(size_slider,
                                                     particle_group);

  //
  // Simulation GROUP
  //
  engine::Entity simulation_group = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    simulation_group,
    engine::components::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<engine::components::UIResolvedRectComponent>(simulation_group);
  ecs.add<engine::components::UIGroupComponent>(
    simulation_group,
    engine::components::UIGroupComponent{"Simulation Actions", true});

  //
  // Simulation Speed Slider
  //
  engine::Entity speed_slider = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    speed_slider,
    engine::components::UILayoutChildComponent{window, -1.f, 30.f});
  ecs.add<engine::components::UIResolvedRectComponent>(speed_slider);
  ecs.add<engine::components::UISliderComponent>(
    speed_slider, engine::components::UISliderComponent{"Speed", &sim_speed,
                                                        0.1f, 10.0f, 0.1f});
  ecs.add<engine::components::UIGroupChildComponent>(speed_slider,
                                                     simulation_group);
  ecs.add<engine::components::UITooltipComponent>(
    speed_slider, "Adjust the temporal scale of the fluid physics.");

  //
  // Pause / Resume Toggle
  //
  engine::Entity pause_toggle = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    pause_toggle,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});
  ecs.add<engine::components::UIResolvedRectComponent>(pause_toggle);
  ecs.add<engine::components::UICheckboxComponent>(
    pause_toggle, engine::components::UICheckboxComponent{"Pause", &is_paused});
  ecs.add<engine::components::UIGroupChildComponent>(pause_toggle,
                                                     simulation_group);
  ecs.add<engine::components::UITooltipComponent>(
    pause_toggle, "Pause or resume the simulation.");

  //
  // Reset button
  //
  engine::Entity reset_button = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    reset_button,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(reset_button);

  ecs.add<engine::components::UIButtonComponent>(
    reset_button,
    engine::components::UIButtonComponent{"Reset", false, [&ecs]() {
                                            low_color_index = 3;
                                            mid_color_index = 0;
                                            high_color_index = 1;

                                            motrix::entities::ResetFluid(ecs);
                                          }});
  ecs.add<engine::components::UITooltipComponent>(
    reset_button, "Restore all settings to factory defaults.");
  ecs.add<engine::components::UIGroupChildComponent>(reset_button,
                                                     simulation_group);

  //
  // Render GROUP
  //
  engine::Entity render_group = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    render_group,
    engine::components::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<engine::components::UIResolvedRectComponent>(render_group);
  ecs.add<engine::components::UIGroupComponent>(
    render_group, engine::components::UIGroupComponent{"Render Options", true});

  //
  // Toggle Fluid Pressure Field
  //
  engine::Entity toggle_fluid_pressure_field = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    toggle_fluid_pressure_field,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});
  ecs.add<engine::components::UIResolvedRectComponent>(
    toggle_fluid_pressure_field);
  ecs.add<engine::components::UICheckboxComponent>(
    toggle_fluid_pressure_field, engine::components::UICheckboxComponent{
                                   "Pressure Field", &render_pressure_field});
  ecs.add<engine::components::UIGroupChildComponent>(
    toggle_fluid_pressure_field, simulation_group);
  ecs.add<engine::components::UIGroupChildComponent>(
    toggle_fluid_pressure_field, render_group);
  ecs.add<engine::components::UITooltipComponent>(
    toggle_fluid_pressure_field, "Toggle fluid pressure field rendering.");

  //
  // Toggle Fluid Particles
  //
  engine::Entity toggle_fluid_particles = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    toggle_fluid_particles,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});
  ecs.add<engine::components::UIResolvedRectComponent>(toggle_fluid_particles);
  ecs.add<engine::components::UICheckboxComponent>(
    toggle_fluid_particles, engine::components::UICheckboxComponent{
                              "Particles", &render_fluid_particles});
  ecs.add<engine::components::UIGroupChildComponent>(toggle_fluid_particles,
                                                     render_group);
  ecs.add<engine::components::UITooltipComponent>(
    toggle_fluid_particles, "Toggle fluid particles rendering.");

  //
  // Toggle Fluid Velocity Vectors
  //
  engine::Entity toggle_fluid_velocity = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    toggle_fluid_velocity,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});
  ecs.add<engine::components::UIResolvedRectComponent>(toggle_fluid_velocity);
  ecs.add<engine::components::UICheckboxComponent>(
    toggle_fluid_velocity, engine::components::UICheckboxComponent{
                             "Velocity Vectors", &render_particle_velocity});
  ecs.add<engine::components::UIGroupChildComponent>(toggle_fluid_velocity,
                                                     render_group);
  ecs.add<engine::components::UITooltipComponent>(
    toggle_fluid_velocity, "Toggle fluid particles velocity vector rendering.");

  //
  // Toggle Fluid Fill
  //
  engine::Entity toggle_fluid_fill = ecs.create_entity();
  ecs.add<engine::components::UILayoutChildComponent>(
    toggle_fluid_fill,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});
  ecs.add<engine::components::UIResolvedRectComponent>(toggle_fluid_fill);
  ecs.add<engine::components::UICheckboxComponent>(
    toggle_fluid_fill,
    engine::components::UICheckboxComponent{"Fill", &render_fluid_filled});
  ecs.add<engine::components::UIGroupChildComponent>(toggle_fluid_fill,
                                                     render_group);
  ecs.add<engine::components::UITooltipComponent>(
    toggle_fluid_fill, "Toggle fluid fill rendering.");

  //
  // Pressure Field Text
  //
  engine::Entity pressure_text = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    pressure_text,
    engine::components::UILayoutChildComponent{window, -1.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(pressure_text);

  ecs.add<engine::components::UITextComponent>(
    pressure_text, engine::components::UITextComponent("Pressure Colors:"));

  ecs.add<engine::components::UITooltipComponent>(
    pressure_text,
    "Change the pressure field colors for low/mid/high pressures.");

  ecs.add<engine::components::UIGroupChildComponent>(pressure_text,
                                                     render_group);

  //
  // New line
  //
  engine::Entity pressure_newline = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    pressure_newline,
    engine::components::UILayoutChildComponent{window, 0.f, 0.f});

  ecs.add<engine::components::UIResolvedRectComponent>(pressure_newline);

  ecs.add<engine::components::UINewLineComponent>(pressure_newline);
  ecs.add<engine::components::UIGroupChildComponent>(pressure_newline,
                                                     render_group);

  //
  // Low color dropdown
  //
  engine::Entity low_color_dropdown = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    low_color_dropdown,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(low_color_dropdown);

  ecs.add<engine::components::UIDropdownComponent>(
    low_color_dropdown,
    engine::components::UIDropdownComponent{
      "Low",
      {"White", "Red", "Green", "Blue"},
      &low_color_index,
      [](const std::string& selection) {
        static const std::unordered_map<std::string, Color> colorMap = {
          {"White", {255, 255, 255, 130}},
          {"Red", {255, 50, 50, 130}},
          {"Blue", {0, 0, 128, 130}},
          {"Green", {0, 179, 90, 130}}};

        pressure_low_color = colorMap.at(selection);
      }});
  ecs.add<engine::components::UITooltipComponent>(
    low_color_dropdown, "Control the color of low pressure areas.");
  ecs.add<engine::components::UIGroupChildComponent>(low_color_dropdown,
                                                     render_group);

  //
  // Mid color dropdown
  //
  engine::Entity mid_color_dropdown = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    mid_color_dropdown,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(mid_color_dropdown);

  ecs.add<engine::components::UIDropdownComponent>(
    mid_color_dropdown,
    engine::components::UIDropdownComponent{
      "Neutral",
      {"White", "Red", "Green", "Blue"},
      &mid_color_index,
      [](const std::string& selection) {
        static const std::unordered_map<std::string, Color> colorMap = {
          {"White", {255, 255, 255, 130}},
          {"Red", {255, 50, 50, 130}},
          {"Blue", {0, 0, 128, 130}},
          {"Green", {0, 179, 90, 130}}};

        pressure_mid_color = colorMap.at(selection);
      }});
  ecs.add<engine::components::UITooltipComponent>(
    mid_color_dropdown, "Control the color of mid pressure areas.");
  ecs.add<engine::components::UIGroupChildComponent>(mid_color_dropdown,
                                                     render_group);

  //
  // High color dropdown
  //
  engine::Entity high_color_dropdown = ecs.create_entity();

  ecs.add<engine::components::UILayoutChildComponent>(
    high_color_dropdown,
    engine::components::UILayoutChildComponent{window, 0.f, 20.f});

  ecs.add<engine::components::UIResolvedRectComponent>(high_color_dropdown);

  ecs.add<engine::components::UIDropdownComponent>(
    high_color_dropdown,
    engine::components::UIDropdownComponent{
      "High",
      {"White", "Red", "Green", "Blue"},
      &high_color_index,
      [](const std::string& selection) {
        static const std::unordered_map<std::string, Color> colorMap = {
          {"White", {255, 255, 255, 130}},
          {"Red", {255, 50, 50, 130}},
          {"Blue", {0, 0, 128, 130}},
          {"Green", {0, 179, 90, 130}}};

        pressure_high_color = colorMap.at(selection);
      }});
  ecs.add<engine::components::UITooltipComponent>(
    high_color_dropdown, "Control the color of mid pressure areas.");
  ecs.add<engine::components::UIGroupChildComponent>(high_color_dropdown,
                                                     render_group);

  // LOG
  logger::info("[GUI] Created window '{}' (entity:{}:{})",
               ecs.get<engine::components::UIWindowComponent>(window).title,
               window.index, window.version);
}

}  // namespace motrix::entities
