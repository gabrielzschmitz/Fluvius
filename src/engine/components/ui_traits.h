// engine/components/ui_traits.h
#pragma once

#include "ui.h"

namespace motrix::engine::components {

template <typename T>
struct UIComponentTraits;

template <>
struct UIComponentTraits<UIButtonComponent> {
  static float Width(const UIButtonComponent& c) { return c.text_width + 12.f; }
  static float Height(const UIButtonComponent&) { return 20.f; }
};

template <>
struct UIComponentTraits<UICheckboxComponent> {
  static float Width(const UICheckboxComponent& c) {
    return c.text_width + 24.f;
  }
  static float Height(const UICheckboxComponent&) { return 20.f; }
};

template <>
struct UIComponentTraits<UITextComponent> {
  static float Width(const UITextComponent& c) { return c.text_width; }
  static float Height(const UITextComponent&) { return 20.f; }
};

template <>
struct UIComponentTraits<UISliderComponent> {
  static float Width(const UISliderComponent&) { return 160.f; }
  static float Height(const UISliderComponent&) { return 30.f; }
};

inline float MeasureUITextWidth(const std::string& text) {
  return MeasureText(text.c_str(), 10 * uiScale) / uiScale;
}

}  // namespace motrix::engine::components
