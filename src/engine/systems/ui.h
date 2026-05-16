// engine/systems/ui.h
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "../../app/scene.h"
#include "../components/ui.h"
#include "../ecs/ecs.h"
#include "raymath.h"
#include "ui_helpers.h"

namespace motrix::engine::systems {

using namespace motrix::engine::components;

//
// Layout Helpers
//
struct RowItem {
  UILayoutChildComponent* layout;
  UIResolvedRectComponent* rect;
  float width;
};

inline void FlushRow(std::vector<RowItem>& row, float content_width,
                     float content_left, float& cursor_y, float gap_spacing,
                     bool add_gap = true, bool left_align = false) {
  if (row.empty()) return;

  float row_height = 0.f;
  float total_width = 0.f;

  for (auto& item : row) {
    row_height = std::max(row_height, item.layout->preferred_height);
    total_width += item.width;
  }

  float total_gap =
    (row.size() > 1) ? gap_spacing * float(row.size() - 1) : 0.f;

  float x = left_align ? content_left
                       : content_left +
                           (content_width - (total_width + total_gap)) * 0.5f;

  for (auto& item : row) {
    item.rect->rect = {x, cursor_y, item.width, item.layout->preferred_height};
    x += item.width + gap_spacing;
  }

  cursor_y += row_height + (add_gap ? gap_spacing : 0.f);
  row.clear();
}

//
// Layout
//
inline void LayoutGroup(ECS& ecs, Entity group, UIGroupComponent& g,
                        UIResolvedRectComponent& group_rect, float content_left,
                        float content_width, float& cursor_y) {
  float group_start_y = cursor_y + g.padding_top;
  float group_cursor_y = group_start_y;

  float inner_content_left = content_left + g.padding_left;
  float inner_content_width = content_width - g.padding_left - g.padding_right;

  std::vector<RowItem> group_row;

  ecs.group_view<UIGroupChildComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity child, UIGroupChildComponent& gc,
        UIResolvedRectComponent& child_rect, UILayoutChildComponent& layout) {
      if (gc.parent_group != group) return;

      if (ecs.has<UINewLineComponent>(child)) {
        FlushRow(group_row, inner_content_width, inner_content_left,
                 group_cursor_y, g.spacing);
        group_cursor_y += 0.f;
        return;
      }

      float width = layout.preferred_width;

      bool full_row = ecs.has<UISliderComponent>(child) || width == -1.f;

      if (full_row) {
        width = inner_content_width;
      } else {
        if (width <= 0.f) width = ResolveIntrinsicWidth(ecs, child);
        if (width <= 0.f) width = inner_content_width * 0.5f;
      }

      float row_width = 0.f;
      for (auto& item : group_row) row_width += item.width;
      if (!group_row.empty()) row_width += (group_row.size() - 1) * g.spacing;

      float next_row_width =
        row_width + width + (group_row.empty() ? 0.f : g.spacing);

      if (full_row || next_row_width > inner_content_width)
        FlushRow(group_row, inner_content_width, inner_content_left,
                 group_cursor_y, g.spacing);

      group_row.push_back(RowItem{&layout, &child_rect, width});
    });

  FlushRow(group_row, inner_content_width, inner_content_left, group_cursor_y,
           g.spacing, false);

  float group_height = group_cursor_y - group_start_y;

  group_rect.rect = {content_left - 4.f, group_start_y - g.padding_top,
                     content_width + 8.f,
                     group_height + g.padding_top + g.padding_bottom};

  cursor_y = group_rect.rect.y + group_rect.rect.height;
}

inline void LayoutStandaloneChildren(ECS& ecs, Entity window_entity,
                                     float content_left, float content_width,
                                     float& cursor_y,
                                     UIWindowComponent& window) {
  std::vector<RowItem> row;

  ecs.group_view<UILayoutChildComponent, UIResolvedRectComponent>(
    [&](Entity child, UILayoutChildComponent& layout,
        UIResolvedRectComponent& child_rect) {
      if (layout.parent != window_entity) return;
      if (ecs.has<UIGroupChildComponent>(child)) return;
      if (ecs.has<UIGroupComponent>(child)) return;

      if (ecs.has<UINewLineComponent>(child)) {
        FlushRow(row, content_width, content_left, cursor_y, window.gap);
        cursor_y += 0.f;
        return;
      }

      float width = layout.preferred_width;
      bool full_row = ecs.has<UISliderComponent>(child) || width == -1.f;

      if (full_row) {
        width = content_width;
      } else {
        if (width <= 0.f) width = ResolveIntrinsicWidth(ecs, child);
        if (width <= 0.f) width = content_width * 0.5f;
      }

      float row_width = 0.f;
      for (auto& item : row) row_width += item.width;
      if (!row.empty()) row_width += (row.size() - 1) * window.gap;

      float next_row_width =
        row_width + width + (row.empty() ? 0.f : window.gap);

      if (full_row || next_row_width > content_width)
        FlushRow(row, content_width, content_left, cursor_y, window.gap);

      row.push_back(RowItem{&layout, &child_rect, width});
    });

  FlushRow(row, content_width, content_left, cursor_y, window.gap, false, true);
}

inline void LayoutUI(ECS& ecs) {
  ecs.group_view<UIWindowComponent>([&](Entity window_entity,
                                        UIWindowComponent& window) {
    if (!window.layout_dirty) return;

    constexpr float title_h = 24.f;
    constexpr float scrollbar_w = 10.f;

    bool had_scrollbar = !window.auto_height &&
                         (window.content_height > (window.height - title_h));

    float start_y = window.position.y + window.padding / 2 + title_h;
    float cursor_y = start_y;
    float content_left = window.position.x + window.padding;
    float content_width = window.width - 2.f * window.padding;

    if (!window.auto_height && had_scrollbar) content_width -= scrollbar_w;

    size_t group_count = 0;

    ecs.group_view<UIGroupComponent>(
      [&](Entity, UIGroupComponent&) { group_count++; });

    bool has_standalone_children = false;

    ecs.group_view<UILayoutChildComponent>(
      [&](Entity child, UILayoutChildComponent& layout) {
        if (layout.parent == window_entity &&
            !ecs.has<UIGroupChildComponent>(child) &&
            !ecs.has<UIGroupComponent>(child)) {
          has_standalone_children = true;
        }
      });

    size_t current_group_idx = 0;

    ecs.group_view<UIGroupComponent, UIResolvedRectComponent>(
      [&](Entity group, UIGroupComponent& g, UIResolvedRectComponent& rect) {
        LayoutGroup(ecs, group, g, rect, content_left, content_width, cursor_y);

        current_group_idx++;

        if (current_group_idx < group_count || has_standalone_children) {
          cursor_y += window.gap;
        }
      });

    LayoutStandaloneChildren(ecs, window_entity, content_left, content_width,
                             cursor_y, window);
    window.content_height = cursor_y - start_y + window.padding;

    if (window.auto_height)
      window.height = cursor_y - window.position.y + window.padding;

    bool needs_scrollbar = !window.auto_height &&
                           (window.content_height > (window.height - title_h));

    if (had_scrollbar != needs_scrollbar)
      window.layout_dirty = true;
    else
      window.layout_dirty = false;
  });
}

inline void HandleWindowScrolling(ECS& ecs,
                                  engine::components::UIWindowComponent& win) {
  if (win.auto_height) return;

  constexpr float title_bar_h = 24.f;
  float view_h = win.height - title_bar_h;

  if (win.content_height <= view_h) {
    win.scroll_y = 0.f;
    return;
  }

  constexpr float scrollbar_w = 10.f;
  float max_scroll = win.content_height - view_h;

  Rectangle track_rect = {win.position.x + win.width - scrollbar_w,
                          win.position.y + title_bar_h, scrollbar_w, view_h};

  float thumb_h = std::fmax(10.f, view_h * (view_h / win.content_height));
  float scroll_ratio = win.scroll_y / max_scroll;

  Rectangle thumb_rect = {
    track_rect.x, track_rect.y + scroll_ratio * (track_rect.height - thumb_h),
    scrollbar_w, thumb_h};

  Rectangle track_scaled = ScaleRect(track_rect);
  Rectangle thumb_scaled = ScaleRect(thumb_rect);
  Vector2 mouse_screen = GetMousePosition();

  Rectangle content_rect =
    ScaleRect({win.position.x, win.position.y + title_bar_h,
               win.width - scrollbar_w, win.height - title_bar_h});

  Vector2 mouse = GetMousePosition();

  if (CheckCollisionPointRec(mouse, content_rect) &&
      !UIConsumesMouse(ecs, mouse)) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0.f) win.scroll_y -= wheel * 30.f;
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
      CheckCollisionPointRec(mouse_screen, thumb_scaled)) {
    win.dragging_scrollbar = true;
    win.scrollbar_drag_offset_y = mouse_screen.y - thumb_scaled.y;
  }

  if (win.dragging_scrollbar) {
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
      win.dragging_scrollbar = false;
    } else {
      float local_y_scaled =
        mouse_screen.y - win.scrollbar_drag_offset_y - track_scaled.y;
      float max_local_y_scaled = track_scaled.height - thumb_scaled.height;

      float new_ratio = Clamp(local_y_scaled / max_local_y_scaled, 0.f, 1.f);
      win.scroll_y = new_ratio * max_scroll;
    }
  }

  win.scroll_y = Clamp(win.scroll_y, 0.f, max_scroll);
}

//
// Render
//
inline void RenderGroups(ECS& ecs, Entity window_entity, float scroll_y) {
  ecs.group_view<UIGroupComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UIGroupComponent& g,
                                             UIResolvedRectComponent& rect,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle r = ScaleRectCached(rect);
    r.y -= (scroll_y * uiScale);

    DrawRectangleLinesEx(r, 1.f, DARKGRAY);

    if (g.separator)
      DrawLine(r.x, r.y + 18 * uiScale, r.x + r.width, r.y + 18 * uiScale,
               GRAY);

    if (!g.title.empty())
      DrawText(g.title.c_str(), r.x + 4 * uiScale, r.y + 2 * uiScale,
               12 * uiScale, BLACK);
  });
}

inline void RenderSliders(ECS& ecs, Entity window_entity, float scroll_y,
                          bool& input_consumed) {
  ecs.group_view<UISliderComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UISliderComponent& slider,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;
    if (!slider.value) return;

    float current_val = *slider.value;
    Rectangle rect = ScaleRectCached(resolved);
    rect.y -= (scroll_y * uiScale);

    Rectangle bar{rect.x, rect.y + 14 * uiScale, rect.width, 16 * uiScale};

    int decimal_places = (slider.step >= 0.1f)     ? 1
                         : (slider.step >= 0.01f)  ? 2
                         : (slider.step >= 0.001f) ? 3
                                                   : 4;
    const char* fmt = TextFormat("%%s: %%.%df", decimal_places);
    DrawText(TextFormat(fmt, slider.label.c_str(), current_val), rect.x, rect.y,
             10 * uiScale, BLACK);

    DrawRectangleRec(bar, {223, 223, 223, 255});
    DrawRectangleLinesEx(bar, 1 * uiScale, BLACK);

    float t = (current_val - slider.min) / (slider.max - slider.min);
    float knob_x = bar.x + t * bar.width;
    Rectangle knob{knob_x - 4 * uiScale, bar.y - 2 * uiScale, 8 * uiScale,
                   bar.height + 4 * uiScale};

    DrawWin95Box(knob, LIGHTGRAY);

    bool hit = CheckCollisionPointRec(GetMousePosition(), bar) ||
               CheckCollisionPointRec(GetMousePosition(), knob);

    if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hit) {
      slider.dragging = true;
      input_consumed = true;
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) slider.dragging = false;

    if (slider.dragging) {
      float mouse_t = (GetMousePosition().x - bar.x) / bar.width;
      mouse_t = Clamp(mouse_t, 0.f, 1.f);
      float raw = slider.min + mouse_t * (slider.max - slider.min);
      float snapped = std::round(raw / slider.step) * slider.step;
      *slider.value = Clamp(snapped, slider.min, slider.max);
      if (slider.on_change) slider.on_change(*slider.value);
    }
  });
}

inline void RenderCheckboxes(ECS& ecs, Entity window_entity, float scroll_y,
                             bool input_consumed) {
  ecs.group_view<UICheckboxComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity e, UICheckboxComponent& checkbox,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (layout.parent != window_entity) return;

      Rectangle rect = ScaleRectCached(resolved);
      rect.y -= (scroll_y * uiScale);

      bool is_full_width = (layout.preferred_width == -1.f);
      float fontSize = 10 * uiScale;
      float box_size = rect.height - 4.f * uiScale;
      float text_w = (float)MeasureText(checkbox.label.c_str(), (int)fontSize);
      float gap = 6.f * uiScale;
      float box_y = rect.y + (rect.height - box_size) * 0.5f;

      float check_x = rect.x;
      float text_x = rect.x + box_size + gap;

      if (is_full_width) {
        DrawWin95Box(rect, LIGHTGRAY);

        float total_w = box_size + gap + text_w;
        check_x = rect.x + (rect.width - total_w) * 0.5f;
        text_x = check_x + box_size + gap;
      } else {
        DrawWin95Box({check_x, box_y, box_size, box_size}, LIGHTGRAY);
      }

      if (*checkbox.value) {
        DrawLine(check_x + box_size * 0.2f, box_y + box_size * 0.5f,
                 check_x + box_size * 0.45f, box_y + box_size * 0.75f, BLACK);

        DrawLine(check_x + box_size * 0.45f, box_y + box_size * 0.75f,
                 check_x + box_size * 0.8f, box_y + box_size * 0.2f, BLACK);
      }

      DrawText(checkbox.label.c_str(), text_x,
               rect.y + (rect.height - fontSize) * 0.5f, fontSize, BLACK);

      if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
          CheckCollisionPointRec(GetMousePosition(), rect)) {
        *checkbox.value = !(*checkbox.value);
        if (checkbox.on_change) checkbox.on_change(*checkbox.value);
      }
    });
}

inline void RenderButtons(ECS& ecs, Entity window_entity, float scroll_y,
                          bool input_consumed) {
  ecs.group_view<UIButtonComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UIButtonComponent& button,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle rect = ScaleRectCached(resolved);
    rect.y -= (scroll_y * uiScale);

    DrawWin95Box(rect, LIGHTGRAY);
    float fontSize = 10 * uiScale;
    float text_w = button.text_width * uiScale;
    DrawText(button.label.c_str(), rect.x + (rect.width - text_w) * 0.5f,
             rect.y + (rect.height - fontSize) * 0.5f, fontSize, BLACK);

    if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
        CheckCollisionPointRec(GetMousePosition(), rect))
      if (button.on_click) button.on_click();
  });
}

inline void RenderText(ECS& ecs, Entity window_entity, float scroll_y) {
  ecs.group_view<UITextComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UITextComponent& text,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle rect = ScaleRectCached(resolved);
    rect.y -= (scroll_y * uiScale);

    DrawText(text.text.c_str(), rect.x,
             rect.y + (rect.height - 10.f * uiScale) * 0.5f, 10 * uiScale,
             BLACK);
  });
}

inline void RenderDropdowns(ECS& ecs, Entity window_entity, float scroll_y,
                            bool& input_consumed) {
  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity e, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (layout.parent != window_entity) return;

      Rectangle rect = ScaleRectCached(resolved);
      rect.y -= (scroll_y * uiScale);

      DrawWin95Box(rect, LIGHTGRAY);

      float arrow_w = 16.f * uiScale;
      Rectangle arrow_rect = {rect.x + rect.width - arrow_w, rect.y, arrow_w,
                              rect.height};
      DrawWin95Box(arrow_rect, LIGHTGRAY);
      float arrow_text_w = MeasureText("v", 10 * uiScale);

      DrawText("v", arrow_rect.x + (arrow_rect.width - arrow_text_w) * 0.5f,
               arrow_rect.y + (arrow_rect.height - 10.f * uiScale) * 0.5f,
               10 * uiScale, BLACK);

      std::string display =
        dropdown.label + ": " + dropdown.options[*dropdown.selected_index];
      float text_x = rect.x + 4.f * uiScale;
      float text_y = rect.y + (rect.height - 10.f * uiScale) * 0.5f;

      DrawText(display.c_str(), text_x, text_y, 10 * uiScale, BLACK);

      if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
          CheckCollisionPointRec(GetMousePosition(), rect)) {
        dropdown.expanded = !dropdown.expanded;
        input_consumed = true;
      }
    });
}

inline void RenderDropdownLists(ECS& ecs, Entity window_entity, float scroll_y,
                                bool& input_consumed) {
  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity e,
                                             UIDropdownComponent& dropdown,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity || !dropdown.expanded) return;

    Rectangle rect = ScaleRectCached(resolved);
    rect.y -= (scroll_y * uiScale);
    float option_h = 20.f * uiScale;

    for (size_t i = 0; i < dropdown.options.size(); ++i) {
      Rectangle option_rect{rect.x, rect.y + rect.height + i * option_h,
                            rect.width, option_h};

      bool hover = CheckCollisionPointRec(GetMousePosition(), option_rect);

      DrawWin95Box(option_rect, hover ? Color{225, 225, 225, 255} : LIGHTGRAY);

      DrawText(dropdown.options[i].c_str(), option_rect.x + 4 * uiScale,
               option_rect.y + (option_rect.height - 10.f * uiScale) * 0.5f,
               10 * uiScale, BLACK);
    }
  });
}

inline void PrepassDropdownInput(ECS& ecs, bool& input_consumed) {
  if (!IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) return;

  Vector2 mouse = GetMousePosition();

  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (!dropdown.expanded) return;

      if (!ecs.has<UIWindowComponent>(layout.parent)) return;

      auto& window = ecs.get<UIWindowComponent>(layout.parent);

      Rectangle rect = ScaleRectCached(resolved);
      rect.y -= (window.scroll_y * uiScale);

      float option_h = 20.f * uiScale;

      for (size_t i = 0; i < dropdown.options.size(); ++i) {
        Rectangle option_rect{rect.x, rect.y + rect.height + i * option_h,
                              rect.width, option_h};

        if (CheckCollisionPointRec(mouse, option_rect)) {
          *dropdown.selected_index = (int)i;
          dropdown.expanded = false;
          input_consumed = true;

          if (dropdown.on_select) dropdown.on_select(dropdown.options[i]);
          return;
        }
      }

      Rectangle total_list_rect{rect.x, rect.y + rect.height, rect.width,
                                option_h * dropdown.options.size()};

      bool over_button = CheckCollisionPointRec(mouse, rect);
      bool over_list = CheckCollisionPointRec(mouse, total_list_rect);

      if (!over_button && !over_list) dropdown.expanded = false;
    });
}

inline void RenderTooltips(ECS& ecs, float scroll_y, bool input_consumed) {
  Vector2 mouse = GetMousePosition();
  float dt = GetFrameTime();
  std::string tooltip_to_draw = "";

  ecs.view<components::UITooltipComponent, components::UIResolvedRectComponent>(
    [&](Entity e, components::UITooltipComponent& tooltip,
        components::UIResolvedRectComponent& resolved) {
      Rectangle rect = ScaleRectCached(resolved);
      rect.y -= (scroll_y * uiScale);
      bool is_hovering = !input_consumed && IsMouseOverUIRect(ecs, rect);
      if (is_hovering) {
        tooltip.hover_timer += dt;
        if (tooltip.hover_timer >= tooltip.delay) {
          tooltip_to_draw = tooltip.text;
          if (!tooltip.logged_visible) {
            logger::debug("[GUI] Tooltip appeared: '{}' (entity:{}:{})",
                          tooltip.text, e.index, e.version);
            tooltip.logged_visible = true;
          }
        }
      } else if (tooltip.hover_timer > 0.0f) {
        tooltip.hover_timer = 0.0f;
        tooltip.logged_visible = false;
      }
    });

  if (!tooltip_to_draw.empty()) {
    float fontSize = 10 * uiScale;
    int textWidth = MeasureText(tooltip_to_draw.c_str(), fontSize);
    float tipWidth = (float)textWidth + 10 * uiScale;
    float tipHeight = fontSize + 8 * uiScale;

    float tipX = mouse.x + 12;
    if (tipX + tipWidth > CANVAS_W * SCALE) tipX = mouse.x - tipWidth - 12;

    float tipY = mouse.y + 12;
    if (tipY + tipHeight > CANVAS_H * SCALE) tipY = mouse.y - tipHeight - 12;

    Rectangle tipRect = {tipX, tipY, tipWidth, tipHeight};

    DrawRectangleRec(tipRect, {255, 255, 225, 255});
    DrawRectangleLinesEx(tipRect, 1, BLACK);
    DrawText(tooltip_to_draw.c_str(), tipRect.x + 5 * uiScale,
             tipRect.y + 4 * uiScale, fontSize, BLACK);
  }
}

inline void RenderWindow(ECS& ecs, bool& input_consumed,
                         motrix::app::SceneType sceneType) {
  Vector2 mouse = GetMousePosition();

  if (IsKeyPressed(KEY_F10) || IsKeyPressed('u')) {
    bool has_window = false;
    ecs.group_view<UIWindowComponent>([&](Entity, UIWindowComponent& window) {
      has_window = true;
      window.minimized = !window.minimized;
      window.layout_dirty = true;
    });

    if (!has_window) {
      const auto& scene = motrix::app::Scenes::Get(sceneType);
      if (scene.create_ui) scene.create_ui(ecs);
      return;
    }
  }

  ecs.group_view<UIWindowComponent>([&](Entity e, UIWindowComponent& window) {
    if (window.minimized) {
      float btn_size = 24.f * uiScale;
      float btn_x = GetScreenWidth() - btn_size - (10.f * uiScale);
      float btn_y = 10.f * uiScale;
      Rectangle restore_btn{btn_x, btn_y, btn_size, btn_size};

      DrawWin95Box(restore_btn, LIGHTGRAY);

      float margin = 6.f * uiScale;
      Rectangle inner_rect{restore_btn.x + margin, restore_btn.y + margin,
                           restore_btn.width - (margin * 2),
                           restore_btn.height - (margin * 2)};
      DrawRectangleLinesEx(inner_rect, 1.f * uiScale, BLACK);

      if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (CheckCollisionPointRec(mouse, restore_btn)) {
          window.minimized = false;
          window.layout_dirty = true;
          input_consumed = true;
        }
      }

      return;
    }

    constexpr float title_h_logical = 24.f;
    const float title_h_scaled = title_h_logical * uiScale;
    const float border = 2.f * uiScale;

    Rectangle rect = ScaleRect(
      {window.position.x, window.position.y, window.width, window.height});
    bool mouse_over_window = CheckCollisionPointRec(mouse, rect);

    const float title_h = 24.f * uiScale;
    Rectangle close_button{rect.x + rect.width - title_h, rect.y, title_h,
                           title_h};
    Rectangle minimize_button{rect.x + rect.width - (title_h * 2), rect.y,
                              title_h, title_h};
    Rectangle title_bar = {rect.x + border, rect.y + border,
                           rect.width - (border * 2), title_h_scaled - border};

    if (!input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
      if (CheckCollisionPointRec(mouse, close_button)) {
        window.close_requested = true;
        input_consumed = true;
      } else if (CheckCollisionPointRec(mouse, minimize_button)) {
        window.minimized = !window.minimized;
        window.layout_dirty = true;
        input_consumed = true;
      } else if (CheckCollisionPointRec(mouse, title_bar)) {
        window.dragging = true;
        window.layout_dirty = true;
        window.drag_offset = {(mouse.x / uiScale) - window.position.x,
                              (mouse.y / uiScale) - window.position.y};
        input_consumed = true;
      }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) window.dragging = false;
    if (window.dragging) {
      window.position.x = (mouse.x / uiScale) - window.drag_offset.x;
      window.position.y = (mouse.y / uiScale) - window.drag_offset.y;
      window.layout_dirty = true;
    }

    DrawWin95Box(rect, LIGHTGRAY);
    DrawRectangleRec(title_bar, {0, 0, 128, 255});
    DrawText(window.title.c_str(), rect.x + 6 * uiScale, rect.y + 4 * uiScale,
             13 * uiScale, WHITE);

    DrawWin95Box(minimize_button, LIGHTGRAY);
    float fontSize = 10.f * uiScale;

    int minTextWidth = MeasureText("_", (int)fontSize);
    float min_x_offset = (minimize_button.width - (float)minTextWidth) / 2.f;
    float min_y_offset = (minimize_button.height - fontSize) / 2.f;
    DrawText("_", minimize_button.x + min_x_offset,
             minimize_button.y + min_y_offset, (int)fontSize, BLACK);

    DrawWin95Box(close_button, LIGHTGRAY);

    int textWidth = MeasureText("X", (int)fontSize);

    float x_offset = (close_button.width - (float)textWidth) / 2.f;
    float y_offset = (close_button.height - fontSize) / 2.f;

    DrawText("X", close_button.x + x_offset, close_button.y + y_offset,
             (int)fontSize, BLACK);

    if (window.close_requested) {
      std::vector<Entity> to_destroy;

      std::function<void(Entity)> collect_descendants = [&](Entity parent) {
        ecs.group_view<UILayoutChildComponent>(
          [&](Entity child, UILayoutChildComponent& layout) {
            if (layout.parent.index == parent.index &&
                layout.parent.version == parent.version) {
              collect_descendants(child);
              to_destroy.push_back(child);
            }
          });
      };

      collect_descendants(e);

      for (Entity child : to_destroy) ecs.destroy_entity(child);

      logger::info(
        "[GUI] Closed window '{}' and cleaned up {} sub-entities "
        "(entity:{}:{})",
        window.title, to_destroy.size(), e.index, e.version);
      ecs.destroy_entity(e);
      return;
    }

    if (mouse_over_window) HandleWindowScrolling(ecs, window);
    DrawWin95Scrollbar(window);

    bool has_scrollbar =
      (window.content_height > (window.height - title_h_logical));
    float scrollbar_w_scaled = has_scrollbar ? (10.f * uiScale) : 0.f;

    Rectangle content_area = {rect.x + border, rect.y + title_h_scaled,
                              rect.width - (border * 2) - scrollbar_w_scaled,
                              rect.height - title_h_scaled - border};

    BeginScissorMode((int)content_area.x, (int)content_area.y,
                     (int)content_area.width, (int)content_area.height + 4);

    RenderGroups(ecs, e, window.scroll_y);
    RenderSliders(ecs, e, window.scroll_y, input_consumed);
    RenderCheckboxes(ecs, e, window.scroll_y, input_consumed);
    RenderButtons(ecs, e, window.scroll_y, input_consumed);
    RenderText(ecs, e, window.scroll_y);
    RenderDropdowns(ecs, e, window.scroll_y, input_consumed);

    EndScissorMode();

    RenderDropdownLists(ecs, e, window.scroll_y, input_consumed);
    RenderTooltips(ecs, window.scroll_y, input_consumed);

    if (mouse_over_window && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
      input_consumed = true;
  });
}

inline void RenderUI(ECS& ecs, motrix::app::SceneType sceneType =
                                 motrix::app::SceneType::FLUID_SIM) {
  LayoutUI(ecs);

  bool input_consumed = false;

  PrepassDropdownInput(ecs, input_consumed);

  RenderWindow(ecs, input_consumed, sceneType);
}

}  // namespace motrix::engine::systems
