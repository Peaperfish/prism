#include "widgets.hpp"

#include <algorithm>
#include <cmath>

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

// Draws the track, fill and knob over the slider item just submitted. t and t_neutral are
// positions along the slider, 0 to 1.
void draw_slider(float t, float t_neutral) {
    const gui::TokenColours& colours = token_colours();
    const gui::TokenMetrics& m = token_metrics();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    // The knob travels where ImGui's own grab would: its centre half a knob in from either end of
    // the item (GrabMinSize is the knob's diameter and the horizontal padding is 0).
    const float half = m.slider_knob * 0.5f;
    const float left = min.x;  // the slider has no horizontal padding (HiddenSliderLook)
    const float right = max.x;
    const float cy = snap((min.y + max.y) * 0.5f);
    const auto x_at = [&](float u) {
        return snap(left + half + std::clamp(u, 0.0f, 1.0f) * std::max(0.0f, right - left - 2 * half));
    };
    const float x = x_at(t);
    const float x_neutral = x_at(t_neutral);

    const float track = std::max(1.0f, snap(m.slider_track));
    const float rounding = track * 0.5f;
    draw->AddRectFilled(ImVec2(left, cy - track * 0.5f), ImVec2(right, cy + track * 0.5f),
                        colour_u32(colours.control_border), rounding);
    const float fill_from = std::min(x, x_neutral);
    const float fill_to = std::max(x, x_neutral);
    if (fill_to > fill_from) {
        draw->AddRectFilled(ImVec2(fill_from, cy - track * 0.5f), ImVec2(fill_to, cy + track * 0.5f),
                            colour_u32(colours.fill), rounding);
    }

    // A soft shadow: a few translucent discs, growing and fading outward, a touch below the knob.
    const gui::Rgba& shadow = colours.knob_shadow;
    constexpr int layers = 4;
    for (int i = layers; i >= 1; --i) {
        const float reach = m.slider_knob_shadow * static_cast<float>(i) / layers;
        gui::Rgba layer = shadow;
        layer.a = shadow.a / (layers * 2.0f);
        draw->AddCircleFilled(ImVec2(x, cy + std::max(1.0f, m.slider_knob_shadow * 0.4f)),
                              half + reach, colour_u32(layer), 0);
    }
    if (active) {
        gui::Rgba halo = colours.fill;
        halo.a = 0.25f;
        draw->AddCircleFilled(ImVec2(x, cy), half + m.slider_knob_shadow * 1.5f, colour_u32(halo), 0);
    }
    draw->AddCircleFilled(ImVec2(x, cy), half,
                          colour_u32(active || hovered ? colours.text : colours.fill), 0);
}

// Hides ImGui's own drawing of the slider (frame and grab) while it handles the interaction.
struct HiddenSliderLook {
    HiddenSliderLook() {
        const ImVec4 none(0, 0, 0, 0);
        for (const ImGuiCol c : {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
                                 ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive}) {
            ImGui::PushStyleColor(c, none);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, token_metrics().slider_knob);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
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

bool section_header(const char* title, bool* open) {
    const gui::TokenColours& colours = token_colours();
    const gui::TokenMetrics& m = token_metrics();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushFont(fonts().semibold, token_type().section);
    const ImVec2 text_size = ImGui::CalcTextSize(title);
    const float height = text_size.y + 2.0f * m.separator_text_padding.y;
    const float width = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton(title, ImVec2(width, height));
    if (clicked) {
        *open = !*open;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float cy = std::round((min.y + max.y) * 0.5f);
    if (ImGui::IsItemHovered()) {
        draw->AddRectFilled(min, max, colour_u32(colours.control_hovered),
                            ImGui::GetStyle().FrameRounding);
    }
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    float x = min.x + m.chevron_size + gap * 0.5f;
    draw_chevron(draw, ImVec2(std::round(x), cy), *open ? Chevron::down : Chevron::right, muted);
    x += m.chevron_size + gap;
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, std::round(cy - text_size.y * 0.5f)),
                  muted, title);
    x += text_size.x + gap;
    if (x < max.x) {
        draw->AddLine(ImVec2(x, cy), ImVec2(max.x, cy), colour_u32(colours.hair),
                      std::max(1.0f, m.separator_text_border));
    }
    ImGui::PopFont();
    ImGui::RenderNavCursor(ImRect(min, max), ImGui::GetItemID());
    return clicked;
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
                   ImGuiSliderFlags flags) {
    bool changed = false;
    {
        const HiddenSliderLook hidden;
        changed = ImGui::SliderFloat(id, value, min, max, "", flags);
    }
    draw_slider(position(*value, min, max), position(neutral, min, max));
    return changed;
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
