#pragma once

#include "imgui.h"
#include "prism/gui/preferences.hpp"

namespace prism::gui_app {

// The app's colours beyond ImGui's own style.
struct Palette {
    // Behind the photo. Always a neutral grey (equal R, G and B, never near white or black) in both
    // themes: the surround changes how bright and how colourful the photo looks, and a tinted or
    // extreme one would mislead every judgement made against it.
    ImU32 canvas;
    ImU32 canvas_text;        // text on the canvas (the empty state): light on its grey
    ImU32 drop_outline;       // the empty state's dashed drop area
    ImU32 drop_highlight;     // the same, and the canvas border, while a file is dragged over
    ImVec4 error_background;  // the error banner
    ImVec4 error_text;
    ImVec4 error_border;
    ImVec4 notice_background;  // the notice banner ("Saved ...")
    ImVec4 notice_text;
    ImVec4 notice_border;
    // Text drawn over the photo ("Rendering..."): light on a dark translucent box in both themes,
    // since the box sits on the photo or the grey canvas, not on the theme's background.
    ImU32 overlay_background;
    ImU32 overlay_text;
};

[[nodiscard]] const Palette& palette(gui::Theme theme);

// Rebuilds ImGui's style for `theme`, with every size multiplied by `scale` (sizes can't be
// rescaled in place, so the style is made from scratch each time). Fonts are scaled separately
// through style.FontScaleDpi, which this sets to `scale` too.
void apply_style(gui::Theme theme, float scale);

}  // namespace prism::gui_app
