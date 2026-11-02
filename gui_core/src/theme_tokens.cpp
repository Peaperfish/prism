#include "prism/gui/theme_tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::gui {

namespace {

// Darkroom: a quiet, neutral chrome around the photo, one blue accent for what is chosen or
// active. Greys are neutral; frames sit a step above the window so a field reads without a border.
constexpr TokenMetrics darkroom_metrics{
    .window_padding = {14, 12},
    .frame_padding = {10, 6},
    .item_spacing = {10, 8},
    .item_inner_spacing = {8, 6},
    .window_rounding = 10,
    .popup_rounding = 8,
    .child_rounding = 8,
    .frame_rounding = 6,
    .grab_rounding = 7,
    .scrollbar_size = 10,
    .scrollbar_rounding = 8,
    .window_border = 1,
    .frame_border = 0,
    .separator_text_border = 1,
    .separator_text_padding = {0, 8},
    .slider_track = 4,
    .slider_knob = 14,
    .slider_knob_shadow = 3,
    .checkbox_size = 14,
    .checkbox_rounding = 4,
    .chevron_size = 4,
    .chevron_thickness = 1.5f,
    .overlay_padding = 6,
    .overlay_rounding = 4,
    .canvas_margin = 12,
    .drop_inset = 24,
    .drop_dash = 8,
    .drop_line = 2,
    .drop_line_active = 3,
    .divider_line = 2,
    .divider_grip = 9,
    .divider_arrow = 4,
    .divider_strip = 14,
    .divider_chip_margin = 10,
    .banner_max_width = 720,
    .badge_padding = {8, 5},
    .plot_height = 68,
    .plot_rounding = 4,
    .plot_padding = 4,
    .plot_line = 1.5f,
    .plot_luma_line = 1,
    .plot_border_width = 1,
    .chip_pad_y = 3,
    .chip_gap = 6,
    .chip_rounding = 3,
    .chip_pad_x = 7,
    .chip_text_gap = 5,
    .selector_pad = 3,
    .selector_gap = 2,
    .dot_radius = 4,
    .dot_gap = 4,
    .dot_between = 8,
    .dot_inset = 6,
    .tooltip_wrap = 384,
    .prefs_control_width = 180,
    .about_wrap = 440,
    .notices_window = {720, 520},
    .progress_width = 240,
    .scope_target_half = 4,
    .scope_label_gap = 3,
    .scope_hit = 6,
};

// Body text is 16 px at 100%: readable on a Retina laptop without squinting. Headers and small
// text share 13 px; headers are set in the semi-bold face to stand apart.
constexpr TokenType darkroom_type{.body = 16.0f, .section = 13.0f, .small = 13.0f, .tiny = 11.5f};

constexpr TokenColours darkroom_dark{
    .text = grey(0.90f),
    .text_muted = grey(0.62f),
    .window_bg = grey(0.125f),
    .menubar_bg = grey(0.105f),
    .popup_bg = grey(0.16f, 0.98f),
    .title_bg = grey(0.16f),
    .title_bg_active = grey(0.16f),
    .frame_bg = grey(0.19f),
    .frame_bg_hovered = grey(0.23f),
    .frame_bg_active = grey(0.26f),
    .button = grey(0.20f),
    .button_hovered = grey(0.26f),
    .button_active = grey(0.32f),
    .header = grey(0.20f),
    .header_hovered = grey(0.235f),
    .header_active = grey(0.27f),
    .edge = grey(0.24f),
    .separator = grey(0.22f),
    .control_border = grey(0.50f),
    .accent = {0.45f, 0.62f, 0.88f, 1},
    .accent_active = {0.55f, 0.70f, 0.95f, 1},
    .check_fill = {0.30f, 0.50f, 0.82f, 1},  // darker than the accent so a white tick reads
    .check_mark = grey(1.0f),
    .scrollbar_grab = grey(0.26f),
    .knob_shadow = grey(0.0f, 0.45f),
    .plot_bg = grey(0.095f),
    .plot_border = grey(0.21f),
    .chip_shadows = {0.20f, 0.36f, 0.59f, 1},
    .chip_highlights = {0.59f, 0.28f, 0.19f, 1},
    .chip_text = grey(1.0f),
    .divider_edge = grey(0.0f, 0.35f),
    .divider_line = grey(1.0f, 0.92f),
    .grip_ring = grey(0.0f, 0.43f),
    .grip_ink = grey(0.157f),
    .modal_dim = grey(0.0f, 0.45f),
    .canvas = grey(46.0f / 255.0f),  // about 19 on the L* scale, darker than the chrome's panels
};

constexpr TokenColours darkroom_light{
    .text = grey(0.12f),
    .text_muted = grey(0.39f),
    .window_bg = grey(0.955f),
    .menubar_bg = grey(0.92f),
    .popup_bg = grey(0.99f, 0.99f),
    .title_bg = grey(0.99f),
    .title_bg_active = grey(0.99f),
    .frame_bg = grey(0.895f),
    .frame_bg_hovered = grey(0.87f),
    .frame_bg_active = grey(0.84f),
    .button = grey(0.89f),
    .button_hovered = grey(0.85f),
    .button_active = grey(0.80f),
    .header = grey(0.895f),
    .header_hovered = grey(0.87f),
    .header_active = grey(0.84f),
    .edge = grey(0.82f),
    .separator = grey(0.85f),
    .control_border = grey(0.45f),
    .accent = {0.18f, 0.42f, 0.80f, 1},
    .accent_active = {0.10f, 0.32f, 0.66f, 1},
    .check_fill = {0.18f, 0.42f, 0.80f, 1},
    .check_mark = grey(1.0f),
    .scrollbar_grab = grey(0.80f),
    .knob_shadow = grey(0.0f, 0.30f),
    .plot_bg = grey(0.93f),
    .plot_border = grey(0.84f),
    .chip_shadows = {0.20f, 0.40f, 0.69f, 1},
    .chip_highlights = {0.69f, 0.34f, 0.23f, 1},
    .chip_text = grey(1.0f),
    .divider_edge = grey(0.0f, 0.35f),
    .divider_line = grey(1.0f, 0.92f),
    .grip_ring = grey(0.0f, 0.43f),
    .grip_ink = grey(0.157f),
    .modal_dim = grey(0.20f, 0.35f),
    .canvas = grey(118.0f / 255.0f),  // about 50 on the L* scale: mid-grey, as photo editors use
};

constexpr std::array<TokenSet, 2> sets{{
    {"Darkroom", Theme::dark, darkroom_dark, darkroom_metrics, darkroom_type},
    {"Darkroom", Theme::light, darkroom_light, darkroom_metrics, darkroom_type},
}};

float srgb_to_linear(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

Vec2 operator*(Vec2 v, float s) {
    return {v.x * s, v.y * s};
}

}  // namespace

std::span<const TokenSet> token_sets() {
    return sets;
}

const TokenSet& token_set(Theme theme) {
    for (const TokenSet& set : sets) {
        if (set.theme == theme) {
            return set;
        }
    }
    return sets[0];
}

TokenMetrics scaled(const TokenMetrics& m, float s) {
    TokenMetrics r = m;
    r.window_padding = m.window_padding * s;
    r.frame_padding = m.frame_padding * s;
    r.item_spacing = m.item_spacing * s;
    r.item_inner_spacing = m.item_inner_spacing * s;
    r.window_rounding = m.window_rounding * s;
    r.popup_rounding = m.popup_rounding * s;
    r.child_rounding = m.child_rounding * s;
    r.frame_rounding = m.frame_rounding * s;
    r.grab_rounding = m.grab_rounding * s;
    r.scrollbar_size = m.scrollbar_size * s;
    r.scrollbar_rounding = m.scrollbar_rounding * s;
    r.window_border = m.window_border * s;
    r.frame_border = m.frame_border * s;
    r.separator_text_border = m.separator_text_border * s;
    r.separator_text_padding = m.separator_text_padding * s;
    r.slider_track = m.slider_track * s;
    r.slider_knob = m.slider_knob * s;
    r.slider_knob_shadow = m.slider_knob_shadow * s;
    r.checkbox_size = m.checkbox_size * s;
    r.checkbox_rounding = m.checkbox_rounding * s;
    r.chevron_size = m.chevron_size * s;
    r.chevron_thickness = m.chevron_thickness * s;
    r.overlay_padding = m.overlay_padding * s;
    r.overlay_rounding = m.overlay_rounding * s;
    r.canvas_margin = m.canvas_margin * s;
    r.drop_inset = m.drop_inset * s;
    r.drop_dash = m.drop_dash * s;
    r.drop_line = m.drop_line * s;
    r.drop_line_active = m.drop_line_active * s;
    r.divider_line = m.divider_line * s;
    r.divider_grip = m.divider_grip * s;
    r.divider_arrow = m.divider_arrow * s;
    r.divider_strip = m.divider_strip * s;
    r.divider_chip_margin = m.divider_chip_margin * s;
    r.banner_max_width = m.banner_max_width * s;
    r.badge_padding = m.badge_padding * s;
    r.plot_height = m.plot_height * s;
    r.plot_rounding = m.plot_rounding * s;
    r.plot_padding = m.plot_padding * s;
    r.plot_line = m.plot_line * s;
    r.plot_luma_line = m.plot_luma_line * s;
    r.plot_border_width = m.plot_border_width * s;
    r.chip_pad_y = m.chip_pad_y * s;
    r.chip_gap = m.chip_gap * s;
    r.chip_rounding = m.chip_rounding * s;
    r.chip_pad_x = m.chip_pad_x * s;
    r.chip_text_gap = m.chip_text_gap * s;
    r.selector_pad = m.selector_pad * s;
    r.selector_gap = m.selector_gap * s;
    r.dot_radius = m.dot_radius * s;
    r.dot_gap = m.dot_gap * s;
    r.dot_between = m.dot_between * s;
    r.dot_inset = m.dot_inset * s;
    r.tooltip_wrap = m.tooltip_wrap * s;
    r.prefs_control_width = m.prefs_control_width * s;
    r.about_wrap = m.about_wrap * s;
    r.notices_window = m.notices_window * s;
    r.progress_width = m.progress_width * s;
    r.scope_target_half = m.scope_target_half * s;
    r.scope_label_gap = m.scope_label_gap * s;
    r.scope_hit = m.scope_hit * s;
    return r;
}

TokenType scaled(const TokenType& t, float s) {
    return {t.body * s, t.section * s, t.small * s, t.tiny * s};
}

float relative_luminance(const Rgba& c) {
    return 0.2126f * srgb_to_linear(c.r) + 0.7152f * srgb_to_linear(c.g) +
           0.0722f * srgb_to_linear(c.b);
}

float contrast_ratio(const Rgba& a, const Rgba& b) {
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

float lightness(const Rgba& c) {
    const float y = relative_luminance(c);
    const float f = y > 216.0f / 24389.0f ? std::cbrt(y) : (24389.0f / 27.0f * y + 16.0f) / 116.0f;
    return 116.0f * f - 16.0f;
}

}  // namespace prism::gui
