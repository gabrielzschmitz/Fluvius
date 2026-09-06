// engine/components/ui.h
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "../ecs/ecs.h"
#include "../globals.h"
#include "raylib.h"

namespace motrix::engine::components {

struct UIWindowComponent {
  static constexpr std::string_view Name = "UIWindow";

  Vector2 position{20.f, 20.f};
  float width = 220.f;
  float height = 0.f;
  bool auto_height = true;
  float padding = 10.f;
  float gap = 8.f;
  std::string title;
  bool layout_dirty = true;
  bool dragging = false;
  Vector2 drag_offset{0.f, 0.f};
  bool close_requested = false;
  bool minimized = false;

  float scroll_y = 0.f;
  float content_height = 0.f;
  bool dragging_scrollbar = false;
  float scrollbar_drag_offset_y = 0.f;

  UIWindowComponent(Vector2 pos = {20.f, 20.f}, float w = 220.f, float h = 0.f,
                    std::string t = {})
    : position(pos), width(w), height(h), title(std::move(t)) {}
};

struct UILayoutChildComponent {
  static constexpr std::string_view Name = "UILayoutChild";

  Entity parent{0};
  float preferred_width = -1.f;  // -1 = stretch
  float preferred_height = 20.f;

  UILayoutChildComponent(Entity parent_entity = {}, float width = -1.f,
                         float height = 20.f)
    : parent(parent_entity), preferred_width(width), preferred_height(height) {}
};

struct UIResolvedRectComponent {
  static constexpr std::string_view Name = "UIResolvedRect";

  Rectangle rect{0.f, 0.f, 0.f, 0.f};
  Rectangle scaled_rect{0.f, 0.f, 0.f, 0.f};

  explicit UIResolvedRectComponent(Rectangle r = {0.f, 0.f, 0.f, 0.f})
    : rect(r) {}
};

/**
 * Slider/checkbox/dropdown values are bound via getter/setter lambdas instead
 * of raw pointers. The SparseSet can relocate components on entity removal, so
 * a long-lived raw pointer into a component field is unsafe. Bindings capture
 * the ECS + owning entity + member pointer and re-resolve on every access
 * (see `entities::FieldBinding`).
 */
struct UISliderComponent {
  static constexpr std::string_view Name = "UISlider";

  std::string label;
  std::function<float()> get_value;
  std::function<void(float)> set_value;
  float min = 0.f;
  float max = 1.f;
  float step = 0.1f;
  bool dragging = false;
  std::function<void(float)> on_change;

  UISliderComponent(std::string text = {}, std::function<float()> getter = {},
                    std::function<void(float)> setter = {},
                    float min_value = 0.f, float max_value = 1.f,
                    float step_value = 0.1f,
                    std::function<void(float)> callback = {})
    : label(std::move(text)),
      get_value(std::move(getter)),
      set_value(std::move(setter)),
      min(min_value),
      max(max_value),
      step(step_value),
      on_change(std::move(callback)) {}
};

struct UICheckboxComponent {
  static constexpr std::string_view Name = "UICheckbox";

  std::string label;
  std::function<bool()> get_value;
  std::function<void(bool)> set_value;
  std::function<void(bool)> on_change;

  UICheckboxComponent(std::string text = {}, std::function<bool()> getter = {},
                      std::function<void(bool)> setter = {},
                      std::function<void(bool)> callback = {})
    : label(std::move(text)),
      get_value(std::move(getter)),
      set_value(std::move(setter)),
      on_change(std::move(callback)) {}
};

struct UIButtonComponent {
  static constexpr std::string_view Name = "UIButton";

  std::string label;
  std::function<void()> on_click;

  UIButtonComponent(std::string text = {},
                    std::function<void()> callback = {})
    : label(std::move(text)), on_click(std::move(callback)) {}
};

struct UITextComponent {
  static constexpr std::string_view Name = "UIText";

  std::string text;

  explicit UITextComponent(std::string value = {})
    : text(std::move(value)) {}
};

struct UIDropdownComponent {
  static constexpr std::string_view Name = "UIDropdown";

  std::string label;
  std::vector<std::string> options;
  std::function<int()> get_index;
  std::function<void(int)> set_index;
  bool expanded = false;
  std::function<void(const std::string&)> on_select;

  UIDropdownComponent(std::string lbl = {}, std::vector<std::string> opts = {},
                      std::function<int()> getter = {},
                      std::function<void(int)> setter = {},
                      std::function<void(const std::string&)> callback = {})
    : label(std::move(lbl)),
      options(std::move(opts)),
      get_index(std::move(getter)),
      set_index(std::move(setter)),
      on_select(std::move(callback)) {}
};

struct UITooltipComponent {
  static constexpr std::string_view Name = "UITooltip";

  std::string text;
  float hover_timer = 0.0f;
  float delay = 1.0f;
  bool logged_visible = false;

  explicit UITooltipComponent(std::string t = "", float d = 1.0f)
    : text(std::move(t)), delay(d) {}
};

struct UIGroupComponent {
  static constexpr std::string_view Name = "UIGroup";

  std::string title;
  bool separator = true;
  float padding_top = 24.f;
  float padding_bottom = 8.f;
  float padding_left = 4.f;
  float padding_right = 4.f;
  float spacing = 8.f;

  UIGroupComponent(std::string t = {}, bool sep = true)
    : title(std::move(t)), separator(sep) {}
};

struct UIGroupChildComponent {
  static constexpr std::string_view Name = "UIGroupChild";

  Entity parent_group{};
  UIGroupChildComponent(Entity parent = {}) : parent_group(parent) {}
};

struct UINewLineComponent {
  static constexpr std::string_view Name = "UINewLine";
};

/**
 * Color selection indices for the fluid scene's dropdowns. Lives on the
 * simulation root entity so UI state is ECS-resident too.
 */
struct UiStateComponent {
  static constexpr std::string_view Name = "UiState";

  // WHITE, RED, GREEN, BLUE
  int low_color_index = 3;
  int mid_color_index = 0;
  int high_color_index = 1;

  // PURPLE, BLUE, CYAN, GREEN, YELLOW, RED, WHITE
  int particle_low_color_index = 1;
  int particle_mid_low_color_index = 2;
  int particle_mid_high_color_index = 4;
  int particle_high_color_index = 5;
};

}  // namespace motrix::engine::components
