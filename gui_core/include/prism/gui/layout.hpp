#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace prism::gui {

// Sizes for the window's layout, as pure functions so they can be tested without a window.

// The control panel's width in pixels for a window whose content is `available` pixels wide:
// the preferred width (in points, times the interface `scale`), at least 240 points so the
// controls stay usable, and at most 40% of the window so the photo keeps most of it. When the
// window is too narrow for both (240 points are more than 40%), the minimum gives way, but only up
// to 60% of the window: the canvas never shrinks to a sliver.
//
// `text_minimum` (pixels) is the narrowest panel in which every text of the Light path is drawn in
// full, as the app measures it (gui_app/light_path.hpp, light_path_min_width). The panel is never
// narrower than that, so a width saved by an older version, or from a smaller interface size, is
// widened to it. The window's minimum width (window_minimum_width) leaves room for it, so the
// 60% rule never has to cut into it.
[[nodiscard]] float panel_width_pixels(float preferred_points, float scale, float available,
                                       float text_minimum = 0.0f);

// A saved or dragged panel width in points, widened to at least `text_minimum` pixels at `scale`
// (rounded up to a whole point), and kept within panel_width_min..panel_width_max.
[[nodiscard]] int widen_panel_width(int points, float scale, float text_minimum);

// The canvas keeps at least this many points beside the panel's minimum.
inline constexpr int canvas_width_min = 240;

// The window's minimum width, in the same pixels as `text_minimum`: room for that panel, the
// `splitter` and canvas_width_min points of canvas at `scale`, wide enough that the panel's text
// minimum is no more than the 60% of the window the panel may take, and never less than `base`.
[[nodiscard]] float window_minimum_width(float text_minimum, float splitter, float scale,
                                         float base);

// The preferred panel width, in points, that a splitter dragged to `pixels` asks for.
[[nodiscard]] int panel_points_from_pixels(float pixels, float scale);

// The panel's resize grab area: a band centred on the panel's drawn edge (`edge_x`), as wide as the
// gap between canvas and panel (`splitter`) but never under splitter_grab_min points at `scale`.
inline constexpr float splitter_grab_min = 8.0f;
struct SplitterGrab {
    float x = 0;      // its left side
    float width = 0;  // pixels

    bool operator==(const SplitterGrab&) const = default;
};
[[nodiscard]] SplitterGrab splitter_grab(float edge_x, float splitter, float scale);

// Whether a mouse at `mouse_x` is over the grab area: its left side counts, its right side not.
[[nodiscard]] bool splitter_grab_hit(const SplitterGrab& grab, float mouse_x);

// The panel's width in pixels while the grab area is dragged: `panel_pixels` is what it is now and
// `mouse_dx` how far the mouse moved since the last frame, so a press (no movement) changes nothing.
[[nodiscard]] float dragged_panel_pixels(float panel_pixels, float mouse_dx);

// --- Comparing two stages in a split ---------------------------------------------------------------

inline constexpr float split_fraction_min = 0.05f;
inline constexpr float split_fraction_max = 0.95f;
inline constexpr float split_fraction_default = 0.5f;

// `fraction` (the divider's place across the picture, 0 = left edge) kept from 5% to 95%, so
// neither half can be dragged away. NaN gives the default, 50%.
[[nodiscard]] float clamp_split_fraction(float fraction) noexcept;

// The fraction for a divider dragged to screen x `mouse_x`, over a picture at `x` and `width`
// pixels, clamped. A picture with no width gives the default.
[[nodiscard]] float split_fraction_from_x(float mouse_x, float x, float width) noexcept;

// Where the two halves of a split picture go on screen. The divider is on a whole pixel, and both
// halves use that one number (one for their edge, one for the texture coordinate), so the halves
// meet exactly: no gap or overlap, and no seam from the texture coordinate rounding differently.
// The left half draws the compare stage's picture with u in [0, u_divider]; the right half draws
// the viewed stage's picture with u in [u_divider, 1]. Both pictures have the same size and
// position, which is why they line up.
struct SplitLayout {
    float left_x0 = 0;
    float divider_x = 0;  // a whole pixel; the left half's right edge and the right half's left
    float right_x1 = 0;
    float u_divider = 0;  // (divider_x - left_x0) / width, 0 to 1
};

// The split of a picture at screen `x` and `width` pixels, with the divider at `fraction`
// (clamped to 5% to 95%) of it, rounded to the nearest whole pixel and kept inside the picture. A
// picture one pixel wide gives a divider at one of its two edges (one half is empty).
[[nodiscard]] SplitLayout split_layout(float x, float width, float fraction) noexcept;

// `text` (UTF-8) shortened to fit `max_width` by cutting out its middle and putting "…" there,
// e.g. "a very long holiday photo name.png" -> "a very lo…name.png". The end is kept because
// it holds the extension and often a number that tells files apart. `measure` gives a string's
// width (in ImGui: CalcTextSize(...).x). Never cuts a character's UTF-8 bytes apart. Returns the
// text unchanged if it fits, and just "…" (or "") if not even that much fits.
[[nodiscard]] std::string ellipsize_middle(std::string_view text, float max_width,
                                           const std::function<float(std::string_view)>& measure);

}  // namespace prism::gui
