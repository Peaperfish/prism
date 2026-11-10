#pragma once

#include "prism/image.hpp"
#include "prism/stroke_field.hpp"

namespace prism {

// Dodge and burn: brush strokes that brighten or darken parts of the image, applied as a gain.
// The strokes are a StrokeField (stroke_field.hpp); `grid` says where the image being processed
// sits on the photo the strokes were drawn on: PixelGrid::whole(W, H) for the whole photo, a band
// {W, H, x, y, 1} for a crop that runs to the right and bottom edges, a strided grid for a
// downsampled preview. Copying it copies the strokes' pointers, not their points.
struct LocalExposure {
    StrokeField field;
    PixelGrid grid;
};

// Multiplies R, G and B of each pixel by stroke_gain(field) = 2^field, where `field` is the
// stroke field at that pixel, and only where the field is not exactly 0: every other pixel is left
// bit for bit as it was. Values above 1 (brighter than white) or below 0 are scaled like any other;
// nothing is clamped.
//
// With no strokes it does nothing at all (no allocation, no pass over the pixels, `grid` is not
// looked at). Otherwise it throws std::invalid_argument if local.grid is not exactly
// img.width x img.height grid pixels, or (as the other stages) if the pixel buffer does not match
// the dimensions. `threads` as for every stage; the result is bit-identical for any value.
void dodge_burn(Image& img, const LocalExposure& local, int threads = 0);

// Rows of the stroke field one thread rasterises at a time (into a buffer of its own), then
// applies before the next. Exposed for tests that put stroke edges across tile boundaries.
inline constexpr int dodge_burn_tile_rows = 64;

}  // namespace prism
