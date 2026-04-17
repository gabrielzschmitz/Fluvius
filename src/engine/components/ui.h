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
  bool auto_width = false;
  bool auto_height = true;
  float padding = 10.f;
  float gap = 8.f;
  std::string title;
  bool layout_dirty = true;
  bool dragging = false;
  Vector2 drag_offset{0.f, 0.f};
  bool close_requested = false;

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
  float margin_top = 0.f;
  float margin_bottom = 0.f;

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

struct UISliderComponent {
  static constexpr std::string_view Name = "UISlider";

  std::string label;
  float* value = nullptr;
  float min = 0.f;
  float max = 1.f;
  float step = 0.1f;
  bool dragging = false;
  std::function<void(float)> on_change;

  UISliderComponent(std::string text = {}, float* bound_value = nullptr,
                    float min_value = 0.f, float max_value = 1.f,
                    float step_value = 0.1f,
                    std::function<void(float)> callback = {})
    : label(std::move(text)),
      value(bound_value),
      min(min_value),
      max(max_value),
      step(step_value),
      on_change(std::move(callback)) {}
};

struct UICheckboxComponent {
  static constexpr std::string_view Name = "UICheckbox";

  std::string label;
  bool* value = nullptr;
  float text_width;
  std::function<void(bool)> on_change;

  UICheckboxComponent(std::string text = {}, bool* bound = nullptr,
                      std::function<void(bool)> callback = {})
    : label(std::move(text)),
      value(bound),
      on_change(std::move(callback)),
      text_width(MeasureText(text.c_str(), 10 * uiScale) / uiScale) {}
};

struct UIButtonComponent {
  static constexpr std::string_view Name = "UIButton";

  std::string label;
  bool clicked = false;
  float text_width;
  std::function<void()> on_click;

  UIButtonComponent(std::string text = {}, bool state = false,
                    std::function<void()> callback = {})
    : label(std::move(text)),
      clicked(state),
      on_click(std::move(callback)),
      text_width(MeasureText(label.c_str(), 10 * uiScale) / uiScale) {}
};

struct UITextComponent {
  static constexpr std::string_view Name = "UIText";

  std::string text;
  float text_width;

  explicit UITextComponent(std::string value = {})
    : text(std::move(value)),
      text_width(MeasureText(text.c_str(), 10 * uiScale) / uiScale) {}
};

struct UIDropdownComponent {
  static constexpr std::string_view Name = "UIDropdown";

  std::string label;
  std::vector<std::string> options;
  std::vector<float> option_widths;
  int* selected_index = nullptr;
  bool expanded = false;
  std::function<void(const std::string&)> on_select;

  UIDropdownComponent(std::string lbl = {}, std::vector<std::string> opts = {},
                      int* bound_index = nullptr,
                      std::function<void(const std::string&)> callback = {})
    : label(std::move(lbl)),
      options(std::move(opts)),
      selected_index(bound_index),
      on_select(std::move(callback)) {
    option_widths.reserve(options.size());
    for (const auto& opt : options) {
      std::string text = label + ": " + opt;
      option_widths.push_back(MeasureText(text.c_str(), 10 * uiScale) /
                              uiScale);
    }
  }

  float SelectedWidth() const {
    if (selected_index && *selected_index >= 0 &&
        *selected_index < option_widths.size())
      return option_widths[*selected_index];
    return 0.f;
  }
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

}  // namespace motrix::engine::components
