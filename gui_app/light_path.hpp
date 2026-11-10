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

// One text that has to fit where it is drawn, as the drawing found it. `text` is what the row wants
// to say and `shown` what it drew (shortened, or only a number, when it did not fit); `width` is
// the whole text's width in the font it is drawn in, `room` the width it had.
struct TextFit {
    std::string what;   // "Exposure value", "Tone curve tab", ...
    std::string text;
    std::string shown;
    float width = 0.0f;
    float room = 0.0f;

    [[nodiscard]] bool whole() const { return shown == text && width <= room + 0.5f; }
};

struct LightPathData {
    gui::LightPathView view;
    // For the tests: when set, the panel writes down every text that has to fit (it is not for the
    // app, which passes none).
    std::vector<TextFit>* fits = nullptr;
    gui::Theme theme = gui::Theme::dark;
    bool strip_on = false;
    bool strip_available = true;  // false: the button is greyed out, and says why
    std::string strip_hint;
    bool pick_on = false;         // Pick mode is on: the White balance row's button is pressed in
    bool pick_available = true;   // false: the button is greyed out, and says why
    std::string pick_hint;
    bool brush_on = false;         // Brush mode is on: Dodge & burn's Brush button is pressed in
    bool brush_available = true;   // false: the button is greyed out, and says why
    std::string brush_hint;
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
    // One of the White balance row's two sliders: the new Kelvin or tint (gui::set_white_balance
    // snaps it), or none when the frame moved nothing (a press, a release).
    std::function<void(gui::WhiteBalanceControl, std::optional<double> value,
                       const gui::SliderFrame&)>
        white_balance_slider;
    std::function<void()> reset_white_balance;  // back to As shot
    std::function<void(gui::CurveKind)> set_curve_kind;
    std::function<void(float value, const gui::SliderFrame&)> curve_parameter;
    std::function<void()> reset_curve_parameter;
    std::function<void()> toggle_strip;
    std::function<void()> toggle_pick;  // the White balance row's Pick neutral button
    std::function<void()> toggle_brush;  // Dodge & burn's Brush button
    // Dodge & burn's Strength slider: the new strength in stops, already a step of 0.1.
    std::function<void(float)> brush_strength;
    std::function<void()> clear_strokes;  // Dodge & burn's Clear button: one undo step
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

// The narrowest panel, in pixels at the current interface size, in which every text of the Light
// path is drawn in full, measured from the fonts and style in effect (so the interface size and the
// theme are in it): the panel's padding and scrollbar, the rows' margins, and the widest of the worst-case
// texts of each row: Exposure's -10.00 and +10.00 stops with "(beyond the slider)", Contrast's
// "Softer than grade 0 · ×0.60" and an off-grid grade, White balance's As shot and Chosen with its subtitle
// and the Pick neutral button, Dodge & burn's "1998 strokes when on" and its controls line
// (Brush, a Strength track of the least width, "2.0 stops" and Clear), a clip note beside the Test strip button, each Tone curve tab with
// room around its label, Colour matrix's "Camera default", the stage switches off ("when on"), and
// the header with Compare on. A Colour matrix file's name is not one of them: it is shortened by
// design, and its whole path is the tooltip.
// The settings whose rows hold the worst-case texts light_path_min_width measures: each stage's
// value at its extremes, on and off, and the files and lights that change a row's words.
[[nodiscard]] std::vector<gui::EditSettings> light_path_worst_case_settings();

[[nodiscard]] float light_path_min_width();

// The same in the mockup's pixels (mock(1) is one), the unit the design is drawn in: it does not change with
// the interface size, up to the fonts' rounding.
[[nodiscard]] float light_path_min_mock_px();

// Draws the panel at the cursor, across the available width, and leaves the cursor under it.
std::vector<LightPathRowRect> draw_light_path(const LightPathData& data,
                                              const LightPathCallbacks& callbacks);

}  // namespace prism::gui_app
