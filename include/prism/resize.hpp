#pragma once

#include "prism/image.hpp"

namespace prism {

// Shrinks `img` so that its longer side is `max_long_side` pixels, keeping the aspect ratio (the
// shorter side is rounded to the nearest pixel, and is at least 1). Each output pixel is the
// average of the source area it covers (a box or "area" filter), weighting source pixels that
// only partly fall inside it by how much of them does. The averaging is done on the linear
// values, so it is physically right: it mixes light the way a smaller sensor would have.
//
// Never enlarges: an image whose longer side is already at most `max_long_side` comes back as an
// unchanged copy. `threads` as for the pipeline stages; the result is bit-identical for any
// value. Throws std::invalid_argument if max_long_side < 1 or the pixel buffer does not match
// width * height * 3.
[[nodiscard]] Image downscale(const Image& img, int max_long_side, int threads = 0);

// The longer side, in pixels, of a `width` x `height` image reduced to `percent` percent of its
// size: max(width, height) * percent / 100 rounded to the nearest pixel (halves up), and at least
// 1. Pass it to downscale() to reduce by a percentage; the rounding is the one downscale uses for
// the shorter side. Throws std::invalid_argument unless percent is 1..100 and width and height
// are at least 1.
[[nodiscard]] int long_side_for_percent(int width, int height, int percent);

}  // namespace prism
