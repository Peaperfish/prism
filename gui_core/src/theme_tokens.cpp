#include "prism/gui/theme_tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::gui {

namespace {

// Darkroom: a quiet chrome around the photo, one amber accent (the Export button and the viewed
// stage). Controls sit a step off their surface so a field reads without a border.
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
    .plot_height = mock_points(132),
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
    .strip_divider_width = 1,
    .strip_hover_edge_width = 2,
    .strip_chip_margin = 8,
    .strip_pill_pad_x = 9,
    .strip_pill_pad_y = 3,
    .strip_bar_wrap = 520,
    .explain_pad = {mock_points(16), mock_points(12)},
    .explain_gap = mock_points(4),
    .prompt_wrap = 360,
    .hair_width = 1,
    .header_height = mock_points(44),
    .header_button = mock_points(28),
    .header_button_rounding = mock_points(7),
    .header_icon = mock_points(16),
    .export_height = mock_points(26),
    .export_pad_x = mock_points(12),
    .status_height = mock_points(30),
    .panel_pad = {mock_points(14), mock_points(14)},
    .panel_pad_bottom = mock_points(18),
    .section_gap = mock_points(18),
    .section_head_gap = mock_points(8),
    .control_height = mock_points(34),
    .control_rounding = mock_points(7),
    .control_pad_x = mock_points(12),
    .zoom_chip_rounding = mock_points(8),
    .zoom_chip_pad = mock_points(3),
    .zoom_chip_button_pad_x = mock_points(9),
    .zoom_chip_margin = {mock_points(16), mock_points(14)},
};

// Body text is 16 px at 100%: readable on a Retina laptop without squinting. Headers and small
// text share the mockup's 12 px (14.8 points); headers are set in the semi-bold face to stand
// apart.
constexpr TokenType darkroom_type{.body = 16.0f,
                                  .section = mock_points(12),
                                  .small = mock_points(12),
                                  .tiny = mock_points(11.5f),
                                  .explain_title = mock_points(12.5f)};

// A colour from 0xRRGGBB, with an alpha.
constexpr Rgba hex(unsigned rgb, float alpha = 1.0f) {
    return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
            static_cast<float>(rgb & 0xff) / 255.0f, alpha};
}

// The surfaces carry a slight cool tint (a blue channel a few steps above the others); the canvas
// behind the photo alone is truly neutral, as a tinted surround shifts how a photo's colour is
// judged.
constexpr TokenColours darkroom_dark{
    .text = hex(0xececf0),
    .text_muted = hex(0xa3a3ab),
    .faint = hex(0x6c6c74),
    .chrome = hex(0x121214),
    .panel = hex(0x1a1a1d),
    .well = hex(0x111113),
    .plot = hex(0x0d0d0f),
    .control = hex(0x2b2b30),
    .control_on = hex(0x3a3a41),
    .explain = hex(0x161618),
    .node = hex(0x1a1a1d),
    .control_hovered = hex(0x35353b),
    .control_active = hex(0x404048),
    .popup_bg = hex(0x222226, 0.98f),
    .title_bg = hex(0x222226),
    .title_bg_active = hex(0x222226),
    .hair = hex(0x2a2a2f),
    .control_border = grey(0.52f),
    .accent = hex(0xe3a33b),
    .accent_active = hex(0xf0b452),
    .accent_ring = hex(0xe3a33b),
    .accent_glow = hex(0xe3a33b, 0.22f),
    .text_on_accent = hex(0x1b1408),
    .track = hex(0x34343a),
    .fill = hex(0xd6d6dc),
    .thumb_border = hex(0x000000, 0.35f),
    .rail = hex(0x2f2f35),
    .view_bg = hex(0xffffff, 0.045f),
    .check_fill = hex(0xd6d6dc),
    .check_mark = hex(0x1a1a1d),
    .scrollbar_grab = hex(0x34343a),
    .knob_shadow = grey(0.0f, 0.45f),
    .plot_border = hex(0x26262b),
    .chip_shadows = {0.20f, 0.36f, 0.59f, 1},
    .chip_highlights = {0.59f, 0.28f, 0.19f, 1},
    .chip_text = grey(1.0f),
    .divider_edge = grey(0.0f, 0.35f),
    .divider_line = grey(1.0f, 0.92f),
    .grip_ring = grey(0.0f, 0.43f),
    .grip_ink = grey(0.157f),
    .modal_dim = grey(0.0f, 0.45f),
    .canvas = hex(0x262626),  // L* 15
    .strip_divider = grey(1.0f, 0.85f),
    .strip_hover_fill = grey(1.0f, 0.12f),
    .strip_hover_edge = {0.55f, 0.72f, 0.98f, 1},
    .strip_chip_bg = grey(0.05f, 0.84f),
    .strip_chip_text = grey(1.0f),
    .strip_bar_bg = hex(0x1c1c20),
    .strip_bar_text = grey(0.92f),
    .strip_bar_pill = grey(1.0f, 0.18f),
    .strip_bar_hover = grey(1.0f, 0.10f),
    .zoom_chip_bg = hex(0x121214, 0.82f),
    .zoom_chip_text = hex(0xf2f2f4),
    .zoom_chip_idle_text = hex(0xc9c9ce),
    .zoom_chip_hovered = grey(1.0f, 0.09f),
    .zoom_chip_current = grey(1.0f, 0.16f),
    .zoom_chip_current_text = grey(1.0f),
};

constexpr TokenColours darkroom_light{
    .text = hex(0x1b1b1e),
    .text_muted = hex(0x5c5c64),
    .faint = hex(0x8b8b93),
    .chrome = hex(0xe8e8eb),
    .panel = hex(0xf7f7f9),
    .well = hex(0xe6e6ea),
    .plot = hex(0x151517),  // dark in both themes
    .control = hex(0xe6e6ea),
    .control_on = hex(0xffffff),
    .explain = hex(0xefeff2),
    .node = hex(0xf7f7f9),
    .control_hovered = hex(0xdcdce1),
    .control_active = hex(0xd2d2d8),
    .popup_bg = hex(0xfcfcfd, 0.99f),
    .title_bg = hex(0xfcfcfd),
    .title_bg_active = hex(0xfcfcfd),
    .hair = hex(0xdadade),
    .control_border = grey(0.45f),
    .accent = hex(0xe3a33b),
    .accent_active = hex(0xd2922a),
    .accent_ring = hex(0x9a6200),
    .accent_glow = hex(0x9a6200, 0.18f),
    .text_on_accent = hex(0x1b1408),
    .track = hex(0xd5d5db),
    .fill = hex(0x3a3a40),
    .thumb_border = hex(0x000000, 0.25f),
    .rail = hex(0xd8d8de),
    .view_bg = hex(0x000000, 0.04f),
    .check_fill = hex(0x3a3a40),
    .check_mark = grey(1.0f),
    .scrollbar_grab = hex(0xcfcfd5),
    .knob_shadow = grey(0.0f, 0.30f),
    .plot_border = hex(0x26262b),
    .chip_shadows = {0.20f, 0.40f, 0.69f, 1},
    .chip_highlights = {0.69f, 0.34f, 0.23f, 1},
    .chip_text = grey(1.0f),
    .divider_edge = grey(0.0f, 0.35f),
    .divider_line = grey(1.0f, 0.92f),
    .grip_ring = grey(0.0f, 0.43f),
    .grip_ink = grey(0.157f),
    .modal_dim = grey(0.20f, 0.35f),
    .canvas = hex(0x838383),  // L* 55: the lightest a surround can be and still be a grey
    .strip_divider = grey(1.0f, 0.85f),
    .strip_hover_fill = grey(1.0f, 0.12f),
    .strip_hover_edge = {0.55f, 0.72f, 0.98f, 1},
    .strip_chip_bg = grey(0.05f, 0.84f),
    .strip_chip_text = grey(1.0f),
    .strip_bar_bg = hex(0xe9e9ed),
    .strip_bar_text = grey(0.12f),
    .strip_bar_pill = grey(0.0f, 0.14f),
    .strip_bar_hover = grey(0.0f, 0.08f),
    .zoom_chip_bg = hex(0x121214, 0.82f),
    .zoom_chip_text = hex(0xf2f2f4),
    .zoom_chip_idle_text = hex(0xc9c9ce),
    .zoom_chip_hovered = grey(1.0f, 0.09f),
    .zoom_chip_current = grey(1.0f, 0.16f),
    .zoom_chip_current_text = grey(1.0f),
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
    r.strip_divider_width = m.strip_divider_width * s;
    r.strip_hover_edge_width = m.strip_hover_edge_width * s;
    r.strip_chip_margin = m.strip_chip_margin * s;
    r.strip_pill_pad_x = m.strip_pill_pad_x * s;
    r.strip_pill_pad_y = m.strip_pill_pad_y * s;
    r.strip_bar_wrap = m.strip_bar_wrap * s;
    r.explain_pad = m.explain_pad * s;
    r.explain_gap = m.explain_gap * s;
    r.prompt_wrap = m.prompt_wrap * s;
    r.hair_width = m.hair_width * s;
    r.header_height = m.header_height * s;
    r.header_button = m.header_button * s;
    r.header_button_rounding = m.header_button_rounding * s;
    r.header_icon = m.header_icon * s;
    r.export_height = m.export_height * s;
    r.export_pad_x = m.export_pad_x * s;
    r.status_height = m.status_height * s;
    r.panel_pad = m.panel_pad * s;
    r.panel_pad_bottom = m.panel_pad_bottom * s;
    r.section_gap = m.section_gap * s;
    r.section_head_gap = m.section_head_gap * s;
    r.control_height = m.control_height * s;
    r.control_rounding = m.control_rounding * s;
    r.control_pad_x = m.control_pad_x * s;
    r.zoom_chip_rounding = m.zoom_chip_rounding * s;
    r.zoom_chip_pad = m.zoom_chip_pad * s;
    r.zoom_chip_button_pad_x = m.zoom_chip_button_pad_x * s;
    r.zoom_chip_margin = m.zoom_chip_margin * s;
    return r;
}

TokenType scaled(const TokenType& t, float s) {
    return {t.body * s, t.section * s, t.small * s, t.tiny * s, t.explain_title * s};
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
