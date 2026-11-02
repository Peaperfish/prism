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
    Rgba accent, accent_active, check_mark;
    Rgba scrollbar_grab;
    Rgba knob_shadow;  // under the slider's knob
    Rgba modal_dim;
    // Behind the photo: always neutral (R = G = B), neither near white nor near black.
    Rgba canvas;
};

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
};

// Type sizes in points at 100%.
struct TokenType {
    float body;     // all running text, controls, menus
    float section;  // section headers (semi-bold)
    float small;    // captions, hints, histogram labels
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
