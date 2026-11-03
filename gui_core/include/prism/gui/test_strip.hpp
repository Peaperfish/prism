#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// The test strip: one picture cut into bands along its long side, like a darkroom test strip,
// each band shown at a different exposure around the current one, darkest first (left or top).
// Everything here is plain arithmetic and text, so it is tested without a window.

enum class StripStep { third, half, whole };

inline constexpr int strip_default_count = 5;
inline constexpr StripStep strip_default_step = StripStep::half;

// What a render (or the window) needs to know about a strip that is on.
struct TestStrip {
    StripStep step = strip_default_step;
    int count = strip_default_count;

    bool operator==(const TestStrip&) const = default;
};

// 3, 2 or 1: a step is 1/divisor of a stop, so that offsets are exact (offset = k / divisor).
[[nodiscard]] int strip_step_divisor(StripStep step) noexcept;

// The step in stops: 1/3, 1/2 or 1.
[[nodiscard]] float strip_step_stops(StripStep step) noexcept;

// "1/3 stop", "1/2 stop", "1 stop".
[[nodiscard]] const char* strip_step_name(StripStep step) noexcept;

// A rectangle in image pixels.
struct StripRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const StripRect&) const = default;
};

// True when the bands are vertical (side by side): the image is as wide as it is tall or wider.
// A portrait image gets horizontal bands (stacked).
[[nodiscard]] bool strip_vertical(int width, int height) noexcept;

// The `count` bands of a width x height image, in order (left to right, or top to bottom). Band i
// covers [i*L/n, (i+1)*L/n) of the long side L, rounded down, so together they cover every pixel
// exactly once, whatever the size. With fewer pixels than bands some bands are empty. An empty
// image, or a count below 1, gives no bands.
[[nodiscard]] std::vector<StripRect> strip_bands(int width, int height, int count);

// The band that shows the current exposure (offset 0): count / 2.
[[nodiscard]] int strip_middle(int count) noexcept;

// Band `band`'s exposure offset in stops: (band - middle) * step, exact for each step.
[[nodiscard]] float strip_offset(StripStep step, int count, int band) noexcept;

// How much more light an offset lets in: 2^offset.
[[nodiscard]] float strip_light_factor(float offset_stops) noexcept;

// The label for band `band`: the offset and the light factor, e.g. "-1 stop · ½× light" (the first
// band spells out the units), "−½ · 0.71×", "0 · now", "+½ · 1.41×", "+1 · 2×". Minus is U+2212,
// fractions are the single glyphs ½ ⅓ ⅔ ¼ (all in the interface font). Empty for a band outside
// the strip.
[[nodiscard]] std::string strip_label(StripStep step, int count, int band);

// The band under the image pixel (x, y) of a width x height image, if the point is inside it.
[[nodiscard]] std::optional<int> strip_band_at(int width, int height, int count, float x,
                                               float y) noexcept;

// The exposure the strip is centred on: the exposure in effect, 0 if the stage is off.
[[nodiscard]] float strip_centre_stops(const EditSettings& s) noexcept;

// `s` as band `band` sees it: exposure on, at the centre plus the band's offset. This is what the
// renderer uses, and what clicking the band sets.
[[nodiscard]] EditSettings strip_band_settings(const EditSettings& s, StripStep step, int count,
                                               int band);

// Clicking a band: sets exposure to the band's (turning the stage on if it was off), keeping a
// value beyond the slider as it is. The middle band changes nothing. Returns true if `s` changed.
bool apply_strip_band(EditSettings& s, StripStep step, int count, int band);

}  // namespace prism::gui
