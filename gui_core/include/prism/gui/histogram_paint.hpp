#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "prism/gui/histogram.hpp"
#include "prism/gui/preferences.hpp"

namespace prism::gui {

// The histogram as a picture, painted on the CPU into RGBA bytes for the app to upload as a texture.
// No ImGui and no floating point: every step is integer maths on 0..255, so the bytes are the same
// on every compiler and machine, and the tests can name them.
//
// From the bottom up: the plot colour and three faint grid lines at the quarters; the luminance
// fill, blended over them; R, G and B, each one screened over what is there,
// 1 - (1 - dst)(1 - src), so where they overlap they add towards cyan, magenta, yellow and white;
// and a clip triangle in each top corner, lit when that end of the range has pixels. Every byte of
// alpha is 255. The 1 px edges of the fills are drawn over this as lines, from histogram_bar_height.

struct Rgba8 {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Rgba8&) const = default;
};

struct HistogramPaintColours {
    Rgba8 plot;  // its alpha is not used: the picture is opaque
    Rgba8 luminance, red, green, blue;
    Rgba8 grid;
    Rgba8 shadows_lit, highlights_lit, triangle_unlit;

    bool operator==(const HistogramPaintColours&) const = default;
};

// The design's colours. They are the same in both themes but for the plot's.
[[nodiscard]] HistogramPaintColours histogram_paint_colours(Theme theme);

// In the picture's own pixels. The fills take the picture less the side insets and the top inset,
// and stand on the bottom edge.
struct HistogramPaintSize {
    int width = 0, height = 0;
    int inset_x = 0, inset_top = 0;
    int triangle_width = 0, triangle_height = 0;
    int grid_line = 1;

    bool operator==(const HistogramPaintSize&) const = default;
};

// The design's insets and triangles for a picture of width x height pixels. `scale`: picture pixels
// per mockup pixel at 100%, that is the interface size times the display's pixel density.
[[nodiscard]] HistogramPaintSize histogram_paint_size(int width, int height, float scale);

// The count that fills the plot's height: the tallest bin of any channel, leaving out the two end
// bins so that an all-black or all-white border doesn't flatten the curve (all the bins when only
// the ends have any). Nothing counted: 0.
[[nodiscard]] std::uint64_t histogram_full_count(const StageHistogram& h) noexcept;

// How much of `full` a bin of `count` stands, 0..full; counts above full_count stop at the top.
[[nodiscard]] std::int64_t histogram_bar_height(std::uint64_t count, std::uint64_t full_count,
                                                std::int64_t full) noexcept;

// Where the 1 px edge of a fill passes through for `bin` (of `count`), in the picture's own pixels:
// x from the left edge, y from the top. The painter's fills and the edge lines both place a bin and a
// count with this, so a line sits on its fill: the column of a bin is the one whose centre is at x,
// and y is the top of the height histogram_bar_height gives, in the same 256ths of a pixel.
struct HistogramEdgePoint {
    float x = 0.0f, y = 0.0f;
};
[[nodiscard]] HistogramEdgePoint histogram_edge_point(int bin, std::uint64_t count,
                                                      std::uint64_t full_count,
                                                      const HistogramPaintSize& size) noexcept;

// Paints `h` into `rgba` (resized to width * height * 4, row by row from the top). A size with no
// room in it gives no bytes.
void paint_histogram(const StageHistogram& h, const HistogramPaintSize& size,
                     const HistogramPaintColours& colours, std::vector<std::uint8_t>& rgba);

// Keeps the last picture, and paints again only when what it shows would change.
class HistogramPaintCache {
public:
    // True if it painted: the picture differs from the last one, so it needs uploading.
    bool update(const StageHistogram& h, const HistogramPaintSize& size,
                const HistogramPaintColours& colours);
    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }

private:
    struct Painted {
        StageHistogram histogram;
        HistogramPaintSize size;
        HistogramPaintColours colours;
    };
    std::optional<Painted> painted_;
    std::vector<std::uint8_t> pixels_;
};

}  // namespace prism::gui
