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
[[nodiscard]] float panel_width_pixels(float preferred_points, float scale, float available);

// The preferred panel width, in points, that a splitter dragged to `pixels` asks for.
[[nodiscard]] int panel_points_from_pixels(float pixels, float scale);

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
