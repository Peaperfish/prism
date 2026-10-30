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

// `text` (UTF-8) shortened to fit `max_width` by cutting out its middle and putting "..." there,
// e.g. "a very long holiday photo name.png" -> "a very lo...name.png". The end is kept because
// it holds the extension and often a number that tells files apart. `measure` gives a string's
// width (in ImGui: CalcTextSize(...).x). Never cuts a character's UTF-8 bytes apart. Returns the
// text unchanged if it fits, and just "..." (or "") if not even that much fits.
[[nodiscard]] std::string ellipsize_middle(std::string_view text, float max_width,
                                           const std::function<float(std::string_view)>& measure);

}  // namespace prism::gui
