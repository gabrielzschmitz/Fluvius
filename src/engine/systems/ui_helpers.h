// engine/systems/ui_helpers.h
#pragma once

#include "../../entities/fluid.h"
#include "../components/ui.h"
#include "../globals.h"

namespace motrix::engine::systems {

// Scale a rectangle by uiScale
inline Rectangle ScaleRect(Rectangle rect) {
  return {rect.x * uiScale, rect.y * uiScale, rect.width * uiScale,
          rect.height * uiScale};
}

// Scale a cached resolved rect
inline Rectangle ScaleRectCached(
  components::UIResolvedRectComponent& resolved) {
  if (resolved.scaled_rect.width != resolved.rect.width ||
      resolved.scaled_rect.height != resolved.rect.height ||
      resolved.scaled_rect.x != resolved.rect.x ||
      resolved.scaled_rect.y != resolved.rect.y) {
    resolved.scaled_rect = {
      resolved.rect.x * uiScale, resolved.rect.y * uiScale,
      resolved.rect.width * uiScale, resolved.rect.height * uiScale};
  }
  return resolved.scaled_rect;
}

// Draw a Win95-style box
inline void DrawWin95Box(Rectangle rect, Color fill) {
  DrawRectangleRec(rect, fill);
  DrawLine(rect.x, rect.y, rect.x + rect.width, rect.y, WHITE);
  DrawLine(rect.x, rect.y, rect.x, rect.y + rect.height, WHITE);
  DrawLine(rect.x, rect.y + rect.height, rect.x + rect.width,
           rect.y + rect.height, DARKGRAY);
  DrawLine(rect.x + rect.width, rect.y, rect.x + rect.width,
           rect.y + rect.height, DARKGRAY);
}

inline void DrawWin95Scrollbar(engine::components::UIWindowComponent& win) {
  if (win.auto_height || win.content_height <= (win.height - 20.f)) return;

  constexpr float title_bar_h = 24.f;
  float view_h = win.height - title_bar_h;
  constexpr float scrollbar_w = 10.f;
  float max_scroll = win.content_height - view_h;

  Rectangle track_rect = {win.position.x + win.width - scrollbar_w,
                          win.position.y + title_bar_h, scrollbar_w, view_h};

  float thumb_h = std::fmax(20.f, view_h * (view_h / win.content_height));
  float scroll_ratio = win.scroll_y / max_scroll;

  Rectangle thumb_rect = {
    track_rect.x, track_rect.y + scroll_ratio * (track_rect.height - thumb_h),
    scrollbar_w, thumb_h};

  Rectangle track_scaled = ScaleRect(track_rect);
  Rectangle thumb_scaled = ScaleRect(thumb_rect);

  DrawRectangleRec(track_scaled, Color{223, 223, 223, 255});

  DrawLine(track_scaled.x, track_scaled.y, track_scaled.x,
           track_scaled.y + track_scaled.height, DARKGRAY);
  DrawLine(track_scaled.x, track_scaled.y, track_scaled.x + track_scaled.width,
           track_scaled.y, DARKGRAY);

  DrawWin95Box(thumb_scaled, LIGHTGRAY);
}

// Resolve intrinsic width for UI components
inline float ResolveIntrinsicWidth(ECS& ecs, Entity entity) {
  using namespace motrix::engine::components;

  if (ecs.has<UIButtonComponent>(entity)) {
    auto& btn = ecs.get<UIButtonComponent>(entity);
    float text_w = MeasureText(btn.label.c_str(), 10);
    return text_w + 16.f;
  }

  if (ecs.has<UICheckboxComponent>(entity)) {
    auto& cb = ecs.get<UICheckboxComponent>(entity);

    float text_w = MeasureText(cb.label.c_str(), 10);

    float box_size = 16.f;

    if (ecs.has<UILayoutChildComponent>(entity)) {
      auto& layout = ecs.get<UILayoutChildComponent>(entity);
      if (layout.preferred_height > 0.f) {
        box_size = layout.preferred_height - 4.f;
      }
    }

    return box_size + 8.f + text_w;
  }

  if (ecs.has<UITextComponent>(entity)) {
    auto& txt = ecs.get<UITextComponent>(entity);
    return MeasureText(txt.text.c_str(), 10);
  }

  if (ecs.has<UISliderComponent>(entity)) {
    return 100.f;
  }

  if (ecs.has<UIDropdownComponent>(entity)) {
    auto& dd = ecs.get<UIDropdownComponent>(entity);

    float label_w = MeasureText((dd.label + ": ").c_str(), 10);

    float max_option_w = 0.f;
    for (const auto& option : dd.options) {
      float w = MeasureText(option.c_str(), 10);
      if (w > max_option_w) max_option_w = w;
    }

    float arrow_w = 16.f;

    return label_w + max_option_w + arrow_w + 12.f;
  }

  return 0.f;
}

inline bool UIConsumesMouse(ECS& ecs, Vector2 mouse_screen) {
  using namespace motrix::engine::components;

  bool dropdown_open = false;
  bool mouse_inside_dropdown = false;

  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (!dropdown.expanded) return;

      dropdown_open = true;

      if (!ecs.has<UIWindowComponent>(layout.parent)) return;

      auto& win = ecs.get<UIWindowComponent>(layout.parent);

      Rectangle rect = ScaleRectCached(resolved);
      rect.y -= win.scroll_y * uiScale;

      float option_h = 20.f * uiScale;

      Rectangle total_rect{
        rect.x, rect.y, rect.width,
        rect.height + option_h * static_cast<float>(dropdown.options.size())};

      if (CheckCollisionPointRec(mouse_screen, total_rect))
        mouse_inside_dropdown = true;
    });

  if (dropdown_open) {
    return !mouse_inside_dropdown;
  }

  return false;
}

inline bool IsMouseOverAnyWindow(ECS& ecs, Vector2 mouse_screen) {
  using namespace motrix::engine::components;

  bool over_window = false;

  ecs.group_view<UIWindowComponent>([&](Entity, UIWindowComponent& window) {
    if (over_window) return;

    Rectangle rect{window.position.x, window.position.y, window.width,
                   window.height};

    rect = ScaleRect(rect);

    if (CheckCollisionPointRec(mouse_screen, rect)) {
      over_window = true;
    }
  });

  return over_window;
}

inline bool IsMouseOverUIRect(ECS& ecs, Rectangle target_rect) {
  using namespace motrix::engine::components;

  Vector2 mouse = GetMousePosition();

  Entity top_window{};
  float highest_y = -1e9f;

  ecs.group_view<UIWindowComponent>(
    [&](Entity entity, UIWindowComponent& window) {
      Rectangle win{window.position.x * uiScale, window.position.y * uiScale,
                    window.width * uiScale, window.height * uiScale};

      if (CheckCollisionPointRec(mouse, win)) {
        if (window.position.y > highest_y) {
          highest_y = window.position.y;
          top_window = entity;
        }
      }
    });

  if (highest_y < -1e8f) return false;

  UIWindowComponent& window = ecs.get<UIWindowComponent>(top_window);

  Rectangle top_rect{window.position.x * uiScale, window.position.y * uiScale,
                     window.width * uiScale, window.height * uiScale};

  if (!CheckCollisionPointRec(mouse, top_rect)) return false;

  return CheckCollisionPointRec(mouse, target_rect);
}

inline void UpdateDensityText(ECS& ecs) {
  using namespace motrix::engine::components;
  ecs.group_view<UITextComponent, UILayoutChildComponent>(
    [&](Entity entity, UITextComponent& text, UILayoutChildComponent& layout) {
      if (text.text.find("Density:") != std::string::npos) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Density: %.6f",
                 entities::selection_density);

        std::string new_val = buffer;

        if (text.text != new_val) {
          text.text = new_val;

          layout.preferred_width =
            static_cast<float>(MeasureText(text.text.c_str(), 10));

          Entity parent_group = layout.parent;

          if (ecs.has<UILayoutChildComponent>(parent_group)) {
            Entity window_ent =
              ecs.get<UILayoutChildComponent>(parent_group).parent;

            if (ecs.has<UIWindowComponent>(window_ent)) {
              ecs.get<UIWindowComponent>(window_ent).layout_dirty = true;
            }
          } else if (ecs.has<UIWindowComponent>(parent_group)) {
            ecs.get<UIWindowComponent>(parent_group).layout_dirty = true;
          }
        }
      }
    });
}

}  // namespace motrix::engine::systems
