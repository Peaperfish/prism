#include "widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "imgui_internal.h"
#include "theme.hpp"

namespace prism::gui_app {

namespace {

ImU32 colour_u32(const gui::Rgba& c) {
    // GetColorU32 applies the style's alpha, so a disabled control is drawn dimmed too.
    return ImGui::GetColorU32(ImVec4(c.r, c.g, c.b, c.a));
}

float snap(float v) {
    return std::round(v);
}

// Draws the track, fill, centre tick and thumb over the slider item just submitted. t and
// t_neutral are positions along the slider, 0 to 1. Every size is constant: hovering or dragging
// changes nothing about the thumb.
void draw_slider(float t, float t_neutral) {
    const gui::TokenColours& colours = token_colours();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    // The thumb travels where ImGui's own grab would: its centre half a thumb in from either end
    // of the item (GrabMinSize is the thumb's diameter and the horizontal padding is 0).
    const float diameter = std::round(mock(14.0f));
    const float half = diameter * 0.5f;
    const float left = min.x;  // the slider has no horizontal padding (HiddenSliderLook)
    const float right = max.x;
    const float cy = snap((min.y + max.y) * 0.5f);
    const auto x_at = [&](float u) {
        return snap(left + half + std::clamp(u, 0.0f, 1.0f) * std::max(0.0f, right - left - 2 * half));
    };
    const float x = x_at(t);
    const float x_neutral = x_at(t_neutral);

    const float track = std::max(1.0f, snap(mock(4.0f)));
    const float rounding = std::max(1.0f, snap(mock(2.0f)));
    const float top = cy - track * 0.5f;
    const float bottom = cy + track * 0.5f;
    draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), colour_u32(colours.track),
                        rounding);
    const float fill_from = std::min(x, x_neutral);
    const float fill_to = std::max(x, x_neutral);
    if (fill_to > fill_from) {
        draw->AddRectFilled(ImVec2(fill_from, top), ImVec2(fill_to, bottom),
                            colour_u32(colours.fill), rounding);
    }
    // A neutral point inside the range gets a centre tick (a slider that starts at its left end
    // has none).
    if (t_neutral > 0.0f && t_neutral < 1.0f) {
        const float tick_h = snap(mock(10.0f));
        const float line = hair();
        draw->AddRectFilled(ImVec2(x_neutral - std::floor(line * 0.5f), cy - tick_h * 0.5f),
                            ImVec2(x_neutral - std::floor(line * 0.5f) + line, cy + tick_h * 0.5f),
                            colour_u32(colours.faint));
    }

    // The shadow, 0 1px 3px at 35% black: a few translucent discs, growing and fading outward,
    // a pixel below the thumb.
    constexpr int layers = 4;
    const float reach_total = mock(3.0f);
    for (int i = layers; i >= 1; --i) {
        const gui::Rgba layer{0.0f, 0.0f, 0.0f, 0.35f / (layers * 2.0f)};
        draw->AddCircleFilled(ImVec2(x, cy + hair()),
                              half + reach_total * static_cast<float>(i) / layers,
                              colour_u32(layer), 0);
    }
    draw->AddCircleFilled(ImVec2(x, cy), half, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 0);
    draw->AddCircle(ImVec2(x, cy), half - hair() * 0.5f, colour_u32(colours.thumb_border), 0,
                    hair());
}

// The row is mock(16) tall: the frame is the font plus twice the vertical padding.
float slider_padding_y() {
    return std::max(0.0f, std::round((mock(16.0f) - ImGui::GetFontSize()) * 0.5f));
}

// Hides ImGui's own drawing of the slider (frame and grab) while it handles the interaction.
struct HiddenSliderLook {
    HiddenSliderLook() {
        const ImVec4 none(0, 0, 0, 0);
        for (const ImGuiCol c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
                                 ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive}) {
            ImGui::PushStyleColor(c, none);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, std::round(mock(14.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, slider_padding_y()));
    }
    ~HiddenSliderLook() {
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }
};

float position(float value, float min, float max) {
    return max > min ? (value - min) / (max - min) : 0.0f;
}

// The shared part of the checkbox and the radio button: lays out the box and label, handles the
// click, and draws the box (round or not) and the label. `on` is the state to show.
bool box_control(const char* label, bool on, bool round, bool* pressed_out) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const gui::TokenColours& colours = token_colours();
    const gui::TokenMetrics& m = token_metrics();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    const float box = std::round(m.checkbox_size);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const float width = box + (label_size.x > 0 ? style.ItemInnerSpacing.x + label_size.x : 0.0f);
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }
    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    ImDrawList* draw = window->DrawList;
    const ImVec2 box_min(pos.x, std::round(pos.y + (height - box) * 0.5f));
    const ImVec2 box_max(box_min.x + box, box_min.y + box);
    const ImVec2 centre((box_min.x + box_max.x) * 0.5f, (box_min.y + box_max.y) * 0.5f);
    const float line = std::max(1.0f, std::round(style.FontScaleDpi));
    if (on) {
        const ImU32 fill = colour_u32(colours.check_fill);
        if (round) {
            draw->AddCircleFilled(centre, box * 0.5f, fill, 0);
            draw->AddCircleFilled(centre, box * 0.2f, colour_u32(colours.check_mark), 0);
        } else {
            draw->AddRectFilled(box_min, box_max, fill, m.checkbox_rounding);
            const ImU32 tick = colour_u32(colours.check_mark);
            const auto at = [&](float u, float v) {
                return ImVec2(box_min.x + u * box, box_min.y + v * box);
            };
            const ImVec2 tick_points[] = {at(0.22f, 0.52f), at(0.43f, 0.72f), at(0.78f, 0.30f)};
            draw->AddPolyline(tick_points, 3, tick, ImDrawFlags_None, std::max(1.5f, box * 0.13f));
        }
    } else {
        const gui::Rgba& frame = held ? colours.control_active
                                      : hovered ? colours.control_hovered : colours.control;
        const ImU32 outline = colour_u32(colours.control_border);
        if (round) {
            draw->AddCircleFilled(centre, box * 0.5f, colour_u32(frame), 0);
            draw->AddCircle(centre, box * 0.5f - line * 0.5f, outline, 0, line);
        } else {
            draw->AddRectFilled(box_min, box_max, colour_u32(frame), m.checkbox_rounding);
            draw->AddRect(box_min, box_max, outline, m.checkbox_rounding, 0, line);
        }
    }
    if (label_size.x > 0) {
        draw->AddText(ImVec2(pos.x + box + style.ItemInnerSpacing.x,
                             std::round(pos.y + (height - label_size.y) * 0.5f)),
                      ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
    }
    ImGui::RenderNavCursor(bb, id);
    *pressed_out = pressed;
    return true;
}

}  // namespace

float slider_height() {
    return ImGui::GetFontSize() + 2 * slider_padding_y();
}

ImVec2 switch_size() {
    return ImVec2(std::round(mock(26.0f)), std::round(mock(15.0f)));
}

bool styled_switch(const char* id, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const gui::TokenColours& colours = token_colours();
    const ImGuiID item_id = window->GetID(id);
    const ImVec2 size = switch_size();
    const ImRect bb(window->DC.CursorPos,
                    ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, item_id)) {
        return false;
    }
    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    if (pressed) {
        *value = !*value;
        ImGui::MarkItemEdited(item_id);
    }

    ImDrawList* draw = window->DrawList;
    const bool on = *value;
    const float radius = std::round(mock(8.0f));
    draw->AddRectFilled(bb.Min, bb.Max, colour_u32(on ? colours.fill : colours.track), radius);
    if (hovered || held) {
        // A wash of the text colour over the track: the same size, so nothing moves.
        gui::Rgba wash = colours.text;
        wash.a = held ? 0.16f : 0.08f;
        draw->AddRectFilled(bb.Min, bb.Max, colour_u32(wash), radius);
    }
    const float knob = std::round(mock(11.0f));
    const float inset = std::round(mock(2.0f));
    const float knob_x = on ? bb.Max.x - inset - knob : bb.Min.x + inset;
    const float knob_y = bb.Min.y + std::round((size.y - knob) * 0.5f);
    draw->AddCircleFilled(ImVec2(knob_x + knob * 0.5f, knob_y + knob * 0.5f), knob * 0.5f,
                          colour_u32(colours.panel), 0);
    ImGui::RenderNavCursor(bb, item_id, ImGuiNavRenderCursorFlags_None, radius);
    return pressed;
}

float segmented_height() {
    return std::round(mock(26.0f)) + 2 * std::round(mock(2.0f));
}

float segmented_tab_width(float width, int count) {
    const float pad = std::round(mock(2.0f));
    const float gap = std::round(mock(2.0f));
    return std::max(1.0f, std::floor((width - 2 * pad - gap * static_cast<float>(count - 1)) /
                                     static_cast<float>(std::max(1, count))));
}

float segmented_width_for(float tab_width, int count) {
    const float pad = std::round(mock(2.0f));
    const float gap = std::round(mock(2.0f));
    return 2 * pad + gap * static_cast<float>(count - 1) + tab_width * static_cast<float>(count);
}

bool segmented(const char* id, std::span<const char* const> labels, int* current,
               const std::function<void(int)>& after_item, float wanted_width) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const int count = static_cast<int>(labels.size());
    if (window->SkipItems || count == 0) {
        return false;
    }
    const gui::TokenColours& colours = token_colours();
    const float pad = std::round(mock(2.0f));
    const float gap = std::round(mock(2.0f));
    const float tab_h = std::round(mock(26.0f));
    const float width =
        std::max(1.0f, wanted_width > 0.0f ? wanted_width : ImGui::GetContentRegionAvail().x);
    const float tab_w = segmented_tab_width(width, count);
    const ImVec2 origin = window->DC.CursorPos;
    const ImRect well(origin, ImVec2(origin.x + width, origin.y + tab_h + 2 * pad));
    ImGui::ItemSize(well);
    ImDrawList* draw = window->DrawList;
    draw->AddRectFilled(well.Min, well.Max, colour_u32(colours.well), std::round(mock(8.0f)));

    // The light theme lifts the current tab with a hairline shadow; its panel is the lighter one.
    const bool light = colours.panel.r + colours.panel.g + colours.panel.b > 1.5f;
    const float font_size = token_type().small;
    bool chosen = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        const ImVec2 tab_min(well.Min.x + pad + static_cast<float>(i) * (tab_w + gap),
                             well.Min.y + pad);
        const ImRect bb(tab_min, ImVec2(tab_min.x + tab_w, tab_min.y + tab_h));
        const ImGuiID tab_id = window->GetID(i);
        if (ImGui::ItemAdd(bb, tab_id)) {
            bool hovered = false;
            bool held = false;
            if (ImGui::ButtonBehavior(bb, tab_id, &hovered, &held) && *current != i) {
                *current = i;
                chosen = true;
                ImGui::MarkItemEdited(tab_id);
            }
            const bool active = *current == i;
            const float rounding = std::round(mock(6.0f));
            if (active) {
                if (light) {
                    draw->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + hair()),
                                        ImVec2(bb.Max.x, bb.Max.y + hair()),
                                        colour_u32(gui::Rgba{0, 0, 0, 0.12f}), rounding);
                }
                draw->AddRectFilled(bb.Min, bb.Max, colour_u32(colours.control_on), rounding);
            } else if (hovered || held) {
                gui::Rgba wash = colours.text;
                wash.a = held ? 0.14f : 0.07f;
                draw->AddRectFilled(bb.Min, bb.Max, colour_u32(wash), rounding);
            }
            // Centred by the semi-bold width in both states, so the text does not shift when the
            // tab becomes current.
            ImGui::PushFont(fonts().semibold, font_size);
            const ImVec2 wide = ImGui::CalcTextSize(labels[static_cast<std::size_t>(i)]);
            ImGui::PopFont();
            ImGui::PushFont(active ? fonts().semibold : fonts().regular, font_size);
            const ImVec2 text_size = ImGui::CalcTextSize(labels[static_cast<std::size_t>(i)]);
            draw->PushClipRect(bb.Min, bb.Max, true);
            draw->AddText(ImVec2(std::round(bb.Min.x + (tab_w - wide.x) * 0.5f),
                                 std::round(bb.Min.y + (tab_h - text_size.y) * 0.5f)),
                          ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                          labels[static_cast<std::size_t>(i)]);
            draw->PopClipRect();
            ImGui::PopFont();
            ImGui::RenderNavCursor(bb, tab_id, ImGuiNavRenderCursorFlags_None, rounding);
        }
        if (after_item) {
            after_item(i);
        }
    }
    ImGui::PopID();
    return chosen;
}

bool styled_checkbox(const char* label, bool* value) {
    bool pressed = false;
    if (!box_control(label, *value, false, &pressed)) {
        return false;
    }
    if (pressed) {
        *value = !*value;
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }
    return pressed;
}

bool styled_radio(const char* label, int* current, int value) {
    bool pressed = false;
    if (!box_control(label, *current == value, true, &pressed)) {
        return false;
    }
    if (pressed) {
        *current = value;
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }
    return pressed;
}

void draw_chevron(ImDrawList* draw, ImVec2 c, Chevron direction, ImU32 colour) {
    const float h = token_metrics().chevron_size;
    const float t = token_metrics().chevron_thickness;
    ImVec2 points[3];
    switch (direction) {
        case Chevron::down:
            points[0] = ImVec2(c.x - h, c.y - h * 0.5f);
            points[1] = ImVec2(c.x, c.y + h * 0.5f);
            points[2] = ImVec2(c.x + h, c.y - h * 0.5f);
            break;
        case Chevron::right:
            points[0] = ImVec2(c.x - h * 0.5f, c.y - h);
            points[1] = ImVec2(c.x + h * 0.5f, c.y);
            points[2] = ImVec2(c.x - h * 0.5f, c.y + h);
            break;
        case Chevron::left:
            points[0] = ImVec2(c.x + h * 0.5f, c.y - h);
            points[1] = ImVec2(c.x - h * 0.5f, c.y);
            points[2] = ImVec2(c.x + h * 0.5f, c.y + h);
            break;
    }
    draw->AddPolyline(points, 3, colour, ImDrawFlags_RoundCornersAll, t);
}

bool styled_begin_combo(const char* id, const char* preview) {
    // The frame's place, worked out before BeginCombo (which opens its popup as the current
    // window when it returns true): the cursor, the item width and the frame height.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
    const float inset = ImGui::GetStyle().FramePadding.x + token_metrics().chevron_size;
    draw_chevron(draw, ImVec2(std::round(pos.x + width - inset), std::round(pos.y + height * 0.5f)),
                 Chevron::down, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    return open;
}

bool styled_slider(const char* id, float* value, float min, float max, float neutral,
                   ImGuiSliderFlags flags, float step) {
    bool changed = false;
    const float before = *value;
    {
        const HiddenSliderLook hidden;
        changed = ImGui::SliderFloat(id, value, min, max, "", flags);
    }
    if (changed && step > 0.0f) {
        // Snapped before it is drawn, so the knob never shows between two positions. A move that
        // lands on the position it started from is no change.
        *value = std::clamp(std::round(*value / step) * step, min, max);
        changed = *value != before;
    }
    draw_slider(position(*value, min, max), position(neutral, min, max));
    return changed;
}

namespace {

// The one text field that is open in place of a slider. Typing takes the keyboard, so there is
// never more than one.
struct OpenField {
    ImGuiID id = 0;
    int last_frame = 0;      // the last frame it was drawn: an old one is stale
    int frames = 0;          // frames it has been open
    bool was_active = false; // it has had the keyboard
    std::array<char, 64> text{};
};

OpenField& open_field() {
    static OpenField field;
    return field;
}

// The frame (and field) that closed last: the slider drawn right after it still sees the field's
// deactivation as its own.
struct Closed {
    ImGuiID id = 0;
    int frame = -10;
};

Closed& last_closed() {
    static Closed closed;
    return closed;
}

}  // namespace

bool styled_slider_typed(const char* id, float* value, float min, float max, float neutral,
                         std::string_view field_text, SliderTyping* typing,
                         ImGuiSliderFlags flags, float step) {
    SliderTyping none;
    SliderTyping& out = typing != nullptr ? *typing : none;
    out = {};
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    OpenField& field = open_field();
    const ImGuiID item_id = window->GetID(id);
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    if (field.id == item_id && field.last_frame < g.FrameCount - 1) {
        field = {};  // left open on a frame it was not drawn: not any more
    }

    if (field.id != item_id) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::CalcItemWidth(), slider_height());
        const bool wanted = !window->SkipItems && !(g.CurrentItemFlags & ImGuiItemFlags_Disabled) &&
                            ImGui::GetIO().KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                            !ImGui::IsAnyItemActive() && ImGui::IsWindowHovered() &&
                            ImGui::IsMouseHoveringRect(at, ImVec2(at.x + size.x, at.y + size.y));
        if (!wanted) {
            const Closed& closed = last_closed();
            out.just_closed = closed.id == item_id && g.FrameCount <= closed.frame + 1;
            return styled_slider(id, value, min, max, neutral, flags | ImGuiSliderFlags_NoInput, step);
        }
        field = {};
        field.id = item_id;
        const std::size_t n = std::min(field_text.size(), field.text.size() - 1);
        std::copy_n(field_text.data(), n, field.text.data());
        field.text[n] = '\0';
    }

    // The field, with the slider's height (a slider's own padding), at the slider's place.
    field.last_frame = g.FrameCount;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(ImGui::GetStyle().FramePadding.x, slider_padding_y()));
    if (field.frames == 0) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool entered =
        ImGui::InputText(id, field.text.data(), field.text.size(),
                         ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleVar();
    const bool active = ImGui::IsItemActive();
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    field.was_active = field.was_active || active;
    ++field.frames;

    bool close = false;
    if (entered) {
        out.text = field.text.data();
        close = true;
    } else if (field.was_active && !active) {
        if (!escape && ImGui::IsItemDeactivatedAfterEdit()) {
            out.text = field.text.data();
        }
        close = true;
    } else if (!field.was_active && field.frames > 4) {
        close = true;  // it never got the keyboard
    }
    out.open = true;  // the last item is the field, also on the frame it closes
    if (close) {
        field = {};
        last_closed() = {item_id, g.FrameCount};
    }
    return false;
}

bool styled_slider_int(const char* id, int* value, int min, int max, int neutral,
                       ImGuiSliderFlags flags) {
    bool changed = false;
    {
        const HiddenSliderLook hidden;
        changed = ImGui::SliderInt(id, value, min, max, "", flags);
    }
    draw_slider(position(static_cast<float>(*value), static_cast<float>(min), static_cast<float>(max)),
                position(static_cast<float>(neutral), static_cast<float>(min), static_cast<float>(max)));
    return changed;
}

}  // namespace prism::gui_app
