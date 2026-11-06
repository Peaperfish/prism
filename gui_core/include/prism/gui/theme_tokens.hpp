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
    // Text: the body colour, the quieter one for secondary text, and the faintest (a disabled
    // icon, an unlit dot's border): meant to be seen, not read.
    Rgba text, text_muted, faint;
    // The surfaces, from the outside in. Depth comes from these shades, not from outlines.
    // chrome: the menu bar and the status bar. panel: the edit panel (and the windows that open
    // over the app). well: the container of a segmented control. plot: the scope's box (dark in
    // both themes). control: a button, a field, a combo; control_on: an active segment.
    // explain: the Explain area. node: the fill of a stage's dot on the panel.
    Rgba chrome, panel, well, plot, control, control_on, explain, node;
    // A control hovered or pressed: a step lighter than `control` in Dark, a step darker in Light.
    Rgba control_hovered, control_active;
    // Backgrounds of popups and windows' title bars.
    Rgba popup_bg, title_bg, title_bg_active;
    // The 1 px line between the big regions, and the quiet edge of a popup or a separator.
    Rgba hair;
    // The outline of a control that has no fill of its own to stand out from its background (a
    // checkbox box, a slider track): meant to be seen, so at least 3:1 against what it sits on.
    Rgba control_border;
    // The one coloured thing: the Export button and the viewed stage's dot. accent_active is its
    // hovered and pressed colour; accent_ring is the viewed dot's border (the accent itself in
    // Dark, darker in Light, where the accent is too pale for 3:1 on the panel); text_on_accent is
    // the label on the Export button.
    Rgba accent, accent_active, accent_ring, text_on_accent;
    // Sliders and switches: the empty track, the filled part (and a switch that is on), the
    // thumb's border, the rail the stage dots sit on, and the wash behind the viewed stage's row.
    Rgba track, fill, thumb_border, rail, view_bg;
    // Checkboxes and radio buttons: the fill when ticked, and the tick drawn on it.
    Rgba check_fill, check_mark;
    Rgba scrollbar_grab;
    Rgba knob_shadow;  // under the slider's knob
    // The hairline of the histogram and scope boxes: it does not outline them heavily.
    Rgba plot_border;
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
    // The zoom chip over the photo is dark in both themes: its background, the number and the
    // choice that is not current in text, a wash under a hovered choice, and the current choice
    // (the one the view already is at) as a brighter wash with white text.
    Rgba zoom_chip_bg, zoom_chip_text, zoom_chip_idle_text, zoom_chip_hovered, zoom_chip_current,
        zoom_chip_current_text;
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
    // The 1 px line between regions (at least one pixel is drawn).
    float hair_width;
    // The panel's header (Undo, Redo, Export): its height, the icon buttons' side and corner
    // radius, the icon drawn in them, the Export button's side padding.
    float header_height, header_button, header_button_rounding, header_icon, export_pad_x;
    // The status bar's height.
    float status_height;
    // The panel's padding (sides and top, then bottom), the gap between sections, and the gap
    // under a section's header.
    Vec2 panel_pad;
    float panel_pad_bottom, section_gap, section_head_gap;
    // The photo's soft shadow: how far it spreads, and how far down it is pushed.
    float photo_shadow_blur, photo_shadow_offset;
    // A dropdown button in the panel: height, corner radius and side padding.
    float control_height, control_rounding, control_pad_x;
    // The zoom chip: corner radius, padding, and its distance from the canvas's right and bottom
    // edges.
    float zoom_chip_rounding, zoom_chip_pad, zoom_chip_button_pad_x;
    Vec2 zoom_chip_margin;
};

// Type sizes in points at 100%.
struct TokenType {
    float body;     // all running text, controls, menus
    float section;  // section headers (semi-bold)
    float small;    // captions, hints, small buttons and the dots' percentages
    float tiny;     // labels inside plots
    float explain_title;  // the Explain area's heading (semi-bold)
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

// A size drawn in the design mockup (Inter at 13 px) as app points at an interface size of 100%:
// the app's body text is 16 points where the mockup's is 13, so every mockup size is multiplied by
// 16/13. `mock(px, scale)` is then multiplied by the interface scale like every other size.
[[nodiscard]] constexpr float mock_points(float px) {
    return px * 16.0f / 13.0f;
}
[[nodiscard]] constexpr float mock(float px, float scale) {
    return mock_points(px) * scale;
}

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
