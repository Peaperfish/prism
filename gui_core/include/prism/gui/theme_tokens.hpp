#pragma once

#include <span>
#include <string_view>

#include "prism/gui/preferences.hpp"

namespace prism::gui {

// The look of the window as data: colours, sizes and type sizes, with no ImGui in sight. The app
// (gui_app/theme.cpp) reads a token set to build ImGui's style; the tests read the same sets to
// check contrast and scaling. A new style is a new entry in token_sets() (one per theme), not new
// code.

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};

struct Vec2 {
    float x = 0, y = 0;
};

[[nodiscard]] constexpr Rgba grey(float v, float alpha = 1.0f) {
    return {v, v, v, alpha};
}

struct TokenColours {
    Rgba text, text_muted;
    // Backgrounds text is drawn on.
    Rgba window_bg, menubar_bg, popup_bg, title_bg, title_bg_active;
    Rgba frame_bg, frame_bg_hovered, frame_bg_active;
    Rgba button, button_hovered, button_active;
    Rgba header, header_hovered, header_active;
    // Hairlines between areas (window and popup edges, separators): decorative, so quiet.
    Rgba edge, separator;
    // The outline of a control that has no fill of its own to stand out from its background (a
    // checkbox box, a slider track): meant to be seen, so at least 3:1 against what it sits on.
    Rgba control_border;
    Rgba accent, accent_active;
    // Checkboxes and radio buttons: the fill when ticked, and the tick drawn on it.
    Rgba check_fill, check_mark;
    Rgba scrollbar_grab;
    Rgba knob_shadow;  // under the slider's knob
    // The histogram and scope boxes in the control panel: a calm fill a step off the panel's, and
    // a hairline that doesn't outline it heavily.
    Rgba plot_bg, plot_border;
    // The Shadows / Highlights chips when lit: muted fills the white label reads on, still told
    // apart (blue and orange). The per-stage dots keep the stronger palette colours.
    Rgba chip_shadows, chip_highlights, chip_text;
    // The compare divider drawn over the photo: a dark edge, the line, and the grip's ring and
    // arrows. Theme-independent, as they sit on the photo.
    Rgba divider_edge, divider_line, grip_ring, grip_ink;
    Rgba modal_dim;
    // Behind the photo: always neutral (R = G = B), neither near white nor near black.
    Rgba canvas;
    // The test strip's bands, drawn over the photo (so the same in both themes): the thin line
    // between bands, the hovered band's wash and edge, and the label chip in a band's corner.
    Rgba strip_divider, strip_hover_fill, strip_hover_edge;
    Rgba strip_chip_bg, strip_chip_text;
    // The strip's bar above the photo follows the theme: its background, its text, the chosen
    // step's pill (a wash over the background) and the wash under a hovered pill or the close
    // button.
    Rgba strip_bar_bg, strip_bar_text, strip_bar_pill, strip_bar_hover;
};

// How many lines of Small text the Explain area shows before it scrolls inside itself. A count,
// not a size, so it does not scale with the interface size (the line height does).
inline constexpr int explain_max_lines = 5;

// Sizes in points at an interface size of 100%.
struct TokenMetrics {
    Vec2 window_padding, frame_padding, item_spacing, item_inner_spacing;
    float window_rounding, popup_rounding, child_rounding, frame_rounding, grab_rounding;
    float scrollbar_size, scrollbar_rounding;
    float window_border, frame_border;
    float separator_text_border;
    Vec2 separator_text_padding;
    // The custom slider: a thin track and a round knob (diameters), and how far the knob's soft
    // shadow reaches.
    float slider_track, slider_knob, slider_knob_shadow;
    // Checkbox and radio button: the box's side, and its corner radius.
    float checkbox_size, checkbox_rounding;
    // The thin chevrons that replace ImGui's triangles: half the width, and the stroke.
    float chevron_size, chevron_thickness;
    // Text on the photo ("Rendering...", the compare chips): padding and corner radius.
    float overlay_padding, overlay_rounding;
    // The canvas around the photo: margin for banners and badges, the empty state's drop area.
    float canvas_margin, drop_inset, drop_dash, drop_line, drop_line_active;
    // The compare divider: line half-width, grip radius, arrow size, drag strip, corner margin.
    float divider_line, divider_grip, divider_arrow, divider_strip, divider_chip_margin;
    float banner_max_width;
    Vec2 badge_padding;
    // The histogram and scope boxes.
    float plot_height, plot_rounding, plot_padding, plot_line, plot_luma_line, plot_border_width;
    // The Shadows / Highlights chips below the plot.
    float chip_pad_y, chip_gap, chip_rounding, chip_pad_x, chip_text_gap;
    // The scope selector's buttons.
    float selector_pad, selector_gap;
    // The per-stage clipping dots and percentages in a pipeline row.
    float dot_radius, dot_gap, dot_between, dot_inset;
    float tooltip_wrap;
    float prefs_control_width;
    float about_wrap;
    Vec2 notices_window;
    float progress_width;
    // The vectorscope's targets: half side, label gap, and how near the skin line shows its tip.
    float scope_target_half, scope_label_gap, scope_hit;
    // The test strip: the line between bands, the hovered band's edge, the label chip's distance
    // from the band's corner; the strip bar's step pills (padding) and the bar's widest line.
    float strip_divider_width, strip_hover_edge_width, strip_chip_margin;
    float strip_pill_pad_x, strip_pill_pad_y, strip_bar_wrap;
    // The Explain area at the foot of the control panel: its padding and the gap between its
    // summary, detail and live line.
    Vec2 explain_pad;
    float explain_gap;
    // The wrap width of the text in the "unexported edits" prompt.
    float prompt_wrap;
};

// Type sizes in points at 100%.
struct TokenType {
    float body;     // all running text, controls, menus
    float section;  // section headers (semi-bold)
    float small;    // captions, hints, small buttons and the dots' percentages
    float tiny;     // labels inside plots
};

struct TokenSet {
    std::string_view style;  // "Darkroom"
    Theme theme;
    TokenColours colours;
    TokenMetrics metrics;
    TokenType type;
};

// Every token set, for the tests to loop over.
[[nodiscard]] std::span<const TokenSet> token_sets();

// The set the app uses for `theme`.
[[nodiscard]] const TokenSet& token_set(Theme theme);

// Every size multiplied by `scale`.
[[nodiscard]] TokenMetrics scaled(const TokenMetrics& m, float scale);
[[nodiscard]] TokenType scaled(const TokenType& t, float scale);

// WCAG 2.x relative luminance of an sRGB colour (alpha ignored), and the contrast ratio (1..21)
// of two colours.
[[nodiscard]] float relative_luminance(const Rgba& c);
[[nodiscard]] float contrast_ratio(const Rgba& a, const Rgba& b);

// CIE L* (0..100) of a grey or colour, from its luminance.
[[nodiscard]] float lightness(const Rgba& c);

}  // namespace prism::gui
