#pragma once

#include "imgui.h"
#include "prism/gui/preferences.hpp"
#include "prism/gui/theme_tokens.hpp"

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
    // The stage histogram in the control panel and its shadow/highlight clipping indicators.
    ImU32 histogram_background;
    ImU32 histogram_border;
    ImU32 histogram_luma_fill;
    ImU32 histogram_luma_line;
    ImU32 histogram_red;
    ImU32 histogram_green;
    ImU32 histogram_blue;
    ImU32 clip_inactive_fill;
    ImU32 clip_inactive_border;
    ImU32 clip_inactive_text;
    ImU32 clip_shadows_fill;
    ImU32 clip_highlights_fill;
    ImU32 clip_gamut_fill;  // colours outside sRGB: magenta-violet, apart from orange and blue
    ImU32 clip_active_border;
    ImU32 clip_active_text;
    // The colour scopes (they share histogram_background / histogram_border and the red, green and
    // blue above). The trace is the neutral colour counts are drawn in, the guides are the light
    // lines at 0, 25, 50, 75 and 100%, and the vectorscope's targets and skin-tone line.
    ImU32 scope_trace;
    ImU32 scope_guide;
    ImU32 scope_skin;
    ImU32 scope_cyan;
    ImU32 scope_magenta;
    ImU32 scope_yellow;
    // The clipping overlay drawn over the photo: pixels above white and pixels below black. Opaque
    // enough to read on any photo; they sit on the picture, not the panel, so they are chosen to
    // be told apart from each other and from grey, and to stay visible in either theme. The
    // Light path's marks use clip_highlights_fill / clip_shadows_fill so they match the chips.
    ImU32 clip_overlay_over;
    ImU32 clip_overlay_under;
    // Colours outside sRGB, drawn as diagonal stripes so the colour underneath stays visible.
    ImU32 clip_overlay_gamut;
    // What the window is cleared to behind everything ImGui draws: the theme's window colour, so
    // the edge of a resize never flashes a different grey.
    ImVec4 window_clear;
};

[[nodiscard]] const Palette& palette(gui::Theme theme);

// The interface fonts, loaded once at start-up from the copies compiled into the executable.
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;  // section headers
};
void load_fonts(ImGuiIO& io);
[[nodiscard]] const Fonts& fonts();

// The thickness of the 1 px line between regions, whole pixels and at least one.
[[nodiscard]] float hair();

// Room above a section header: the gap between sections, unless this is the top of the window.
void space_before_section();

// A section header: the title in the semi-bold face at the theme's section size, in the muted
// text colour, with no line under it, and room above (the gap between sections) and below. With
// `action`, a plain muted text at the right edge that lights up under the cursor; returns true
// when it was clicked (it is ImGui's last item afterwards). `title_hovered` says if the mouse is on
// the title. Replaces ImGui::SeparatorText.
bool section_text(const char* text, const char* action = nullptr, bool* title_hovered = nullptr);

// The current theme's tokens as apply_style last set them: the colours, the sizes already
// multiplied by the interface scale, and the type sizes (before the scale, as ImGui wants them).
[[nodiscard]] const gui::TokenColours& token_colours();
[[nodiscard]] const gui::TokenMetrics& token_metrics();
[[nodiscard]] const gui::TokenType& token_type();

// A size from the design mockup (Inter at 13 px) in pixels at the current interface size: 16/13
// points per mockup pixel, times the scale apply_style last set. See gui::mock.
[[nodiscard]] inline float mock(float px) {
    return gui::mock(px, ImGui::GetStyle().FontScaleDpi);
}

// Rebuilds ImGui's style for `theme`, with every size multiplied by `scale` (sizes can't be
// rescaled in place, so the style is made from scratch each time). Fonts are scaled separately
// through style.FontScaleDpi, which this sets to `scale` too. Colours, spacing, rounding and the body
// font size come from the theme's tokens (prism/gui/theme_tokens.hpp).
void apply_style(gui::Theme theme, float scale);

}  // namespace prism::gui_app
