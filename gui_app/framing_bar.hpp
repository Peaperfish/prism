#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "imgui.h"
#include "prism/frame.hpp"

namespace prism::gui_app {

// The framing bar (#58): above the canvas while framing, in the test strip's place. It is drawn
// from plain data and answers through callbacks, and holds no state of its own, so the tests can
// draw it without the app.
//
//   Aspect [Original ▾] [⇄ X]                                         Reset   Done (Return)
//   Straighten [−45° ─────────●───────── +45°]  1.2° clockwise
//   Frame 5120 × 3413 of 6000 × 4000 (73% of the photo), turned 1.2° clockwise.
//   Drag the blades to frame the print — drag inside to move them, ⌥-drag along the horizon …
//
// #58's first line is two here, so that it fits the narrowest canvas: the bar is the same four
// lines, and the same height, whatever it shows and however wide it is (#38). On a narrow canvas the
// labels "Aspect" and "Straighten" and the "(Return)" give way first, then Reset moves to the end of
// the second line and the slider shrinks to its least width; the angle's words and the two text
// lines are shortened in the middle only past that, their whole text the tooltip.

struct FramingBarData {
    Aspect aspect = Aspect::original;  // the draft's lock (FramingState::draft)
    bool portrait = false;
    float angle = 0.0f;
    std::string live_line;  // gui::framing_live_line of the draft
    std::string hint;       // gui::framing_hint
};

struct FramingBarCallbacks {
    // The Straighten slider's drag: its start, each new angle, and its end (the commit).
    std::function<void()> begin_turn;
    std::function<void(float angle)> turn_to;
    std::function<void()> end_turn;
    // An angle typed into the slider (Ctrl-click, Cmd-click on a Mac): one step.
    std::function<void(float angle)> set_angle;
    std::function<void(Aspect)> set_aspect;
    std::function<void()> turn_aspect;  // ⇄ X
    std::function<void()> reset;
    std::function<void()> done;
    // Right after a control is submitted, it being ImGui's last item, as in light_path.hpp.
    std::function<void(std::string_view id)> describe;
    // The mouse is over something that isn't a control but explains itself.
    std::function<void(std::string_view id)> hover;
};

// Where the parts were drawn, for the tests.
struct FramingBarRects {
    ImVec2 bar_min, bar_max;
    ImVec2 aspect_min, aspect_max;
    ImVec2 turn_min, turn_max;
    ImVec2 reset_min, reset_max;
    ImVec2 done_min, done_max;
    ImVec2 slider_min, slider_max;
    ImVec2 words_min, words_max;
    ImVec2 live_min, live_max;
    ImVec2 hint_min, hint_max;
    std::string done_label;  // "Done (Return)", or "Done" on a narrow canvas
    std::string live_shown;  // the live line as drawn (shortened if it had to be)
    std::string hint_shown;
};

// The bar's height at the current interface size: four lines and the padding. Nothing else has a
// say in it.
[[nodiscard]] float framing_bar_height();

// Draws the bar at the cursor, `width` wide and framing_bar_height() tall, and leaves the cursor
// under it.
void draw_framing_bar(const FramingBarData& data, const FramingBarCallbacks& callbacks, float width,
                      FramingBarRects* rects = nullptr);

}  // namespace prism::gui_app
