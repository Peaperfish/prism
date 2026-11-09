#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"
#include "prism/gui/explanations.hpp"
#include "prism/gui/light_path.hpp"
#include "prism/gui/preferences.hpp"
#include "prism/gui/slider_gesture.hpp"

namespace prism::gui_app {

// The Light path panel: a rail with a dot per stage and a row per stage, in pipeline order, under a
// "Light path" header with the Compare action. It is drawn from plain data and answers through
// callbacks, and holds no state of its own, so the tests can draw it without the app.
//
// A row's height comes from gui::light_path_row_layout, and everything in the row is placed from
// that layout, so what a row shows can never change how tall it is (#38).
//
// Whole-row clicks: the row's own button is submitted after everything else in the row. ImGui gives
// a mouse position to the first item submitted under it, so the row's controls get theirs first
// and the row only gets the presses that land on nothing else. It acts on the press, and doesn't
// hold the active id (the press is not a control being used: see active_control.hpp).

struct LightPathData {
    gui::LightPathView view;
    gui::Theme theme = gui::Theme::dark;
    bool strip_on = false;
    bool strip_available = true;  // false: the button is greyed out, and says why
    std::string strip_hint;
};

struct LightPathCallbacks {
    std::function<void(gui::StageId)> view_stage;
    std::function<void(gui::StageId, bool on)> set_stage_on;
    // One frame of a slider's gesture, called only on a frame when something happened to it.
    std::function<void(gui::SimpleControl, float position, const gui::SliderFrame&)> slider;
    // The Contrast slider, which is in paper grades and not a position for apply_simple_control:
    // the new multiplier, or none when the frame moved nothing (a press, a release).
    std::function<void(std::optional<float> contrast, const gui::SliderFrame&)> contrast_slider;
    std::function<void(gui::SimpleControl)> reset_slider;
    std::function<void(gui::CurveKind)> set_curve_kind;
    std::function<void(float value, const gui::SliderFrame&)> curve_parameter;
    std::function<void()> reset_curve_parameter;
    std::function<void()> toggle_strip;
    std::function<void(std::optional<gui::StageId>)> set_compare;  // none: Off
    std::function<void()> swap_compare;
    // Called right after a control is submitted, as the app's explain() and describe() want: the
    // control is ImGui's last item. `changed`: its value changed this frame.
    std::function<void(std::string_view id, bool changed)> explain;
    std::function<void(std::string_view id)> describe;
    // Called when the mouse is over something that isn't a control but explains itself.
    std::function<void(std::string_view id)> hover;
};

// Where a row was drawn, in screen pixels.
struct LightPathRowRect {
    gui::StageId stage = gui::StageId::decode;
    ImVec2 min;
    ImVec2 max;
};

// Draws the panel at the cursor, across the available width, and leaves the cursor under it.
std::vector<LightPathRowRect> draw_light_path(const LightPathData& data,
                                              const LightPathCallbacks& callbacks);

}  // namespace prism::gui_app
