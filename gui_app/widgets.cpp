#include "widgets.hpp"

#include <algorithm>
#include <cmath>

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
                            colour_u32(colours.accent), rounding);
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
        gui::Rgba halo = colours.accent;
        halo.a = 0.25f;
        draw->AddCircleFilled(ImVec2(x, cy), half + m.slider_knob_shadow * 1.5f, colour_u32(halo), 0);
    }
    draw->AddCircleFilled(ImVec2(x, cy), half,
                          colour_u32(active || hovered ? colours.accent_active : colours.accent), 0);
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

}  // namespace

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
