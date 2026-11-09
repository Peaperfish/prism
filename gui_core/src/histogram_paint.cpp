#include "prism/gui/histogram_paint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "prism/gui/theme_tokens.hpp"

namespace prism::gui {

namespace {

// A fill's height is kept in 1/256 of a pixel, so the top pixel of a column can be part covered.
constexpr int subpixels = 256;

constexpr int div255(int v) {
    return (v + 127) / 255;
}

// `src` at alpha `a` (0..255) over `dst`.
constexpr int over(int dst, int src, int a) {
    return div255(dst * (255 - a) + src * a);
}

// 1 - (1 - dst)(1 - src), on 0..255.
constexpr int screen(int dst, int src) {
    return 255 - div255((255 - dst) * (255 - src));
}

// A colour's light once its alpha is applied: screening this is the same as blending the screened
// colour at that alpha.
struct Light {
    int r, g, b;
};
constexpr Light light_of(const Rgba8& c) {
    return {div255(c.r * c.a), div255(c.g * c.a), div255(c.b * c.a)};
}

std::uint8_t to_byte(float v) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255));
}

// The columns' fill heights for one set of counts, in subpixels: the bins are spread over the
// columns, with straight lines between them.
void column_heights(const StageHistogram::Counts& counts, std::uint64_t full_count, int columns,
                    int rows, std::vector<int>& heights) {
    std::array<std::int64_t, StageHistogram::bins> bars{};
    const std::int64_t full = static_cast<std::int64_t>(rows) * subpixels;
    for (std::size_t b = 0; b < bars.size(); ++b) {
        bars[b] = histogram_bar_height(counts[b], full_count, full);
    }
    heights.resize(static_cast<std::size_t>(columns));
    const std::int64_t last = StageHistogram::bins - 1;
    const std::int64_t span = std::max(columns - 1, 1);
    for (int x = 0; x < columns; ++x) {
        const std::int64_t at = x * last;
        const std::int64_t bin = at / span;
        const std::int64_t part = at % span;
        const std::int64_t next = std::min(bin + 1, last);
        heights[static_cast<std::size_t>(x)] = static_cast<int>(
            (bars[static_cast<std::size_t>(bin)] * (span - part) +
             bars[static_cast<std::size_t>(next)] * part) /
            span);
    }
}

// A triangle pointing up, `width` x `height`, its corner at (left, top): each pixel's coverage comes
// from 16 sample points in it, so the slopes are smooth and still integer.
void paint_triangle(std::vector<std::uint8_t>& rgba, int stride, int left, int top, int width,
                    int height, const Rgba8& colour) {
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = rgba.data() + static_cast<std::size_t>(top + y) * stride * 4 +
                            static_cast<std::size_t>(left) * 4;
        for (int x = 0; x < width; ++x) {
            int inside = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    // In eighths of a pixel, from the box's corner. The half width at depth Y is
                    // width * Y / (2 * height).
                    const int sample_x = 8 * x + 2 * sx + 1;
                    const int sample_y = 8 * y + 2 * sy + 1;
                    inside += 2 * height * std::abs(sample_x - 4 * width) <= width * sample_y;
                }
            }
            if (inside == 0) {
                continue;
            }
            const int alpha = colour.a * inside / 16;
            std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            p[0] = static_cast<std::uint8_t>(over(p[0], colour.r, alpha));
            p[1] = static_cast<std::uint8_t>(over(p[1], colour.g, alpha));
            p[2] = static_cast<std::uint8_t>(over(p[2], colour.b, alpha));
        }
    }
}

}  // namespace

HistogramPaintColours histogram_paint_colours(Theme theme) {
    const Rgba& plot = token_set(theme).colours.plot;
    HistogramPaintColours c;
    c.plot = {to_byte(plot.r), to_byte(plot.g), to_byte(plot.b), 255};
    c.luminance = {200, 200, 206, 56};  // .22
    c.red = {255, 92, 80, 77};          // .30
    c.green = {80, 220, 110, 66};       // .26
    c.blue = {90, 140, 255, 82};        // .32
    c.grid = {255, 255, 255, 15};       // .06
    c.shadows_lit = {0x4d, 0x8d, 0xff, 255};
    c.highlights_lit = {0xff, 0x7a, 0x3d, 255};
    c.triangle_unlit = {255, 255, 255, 56};  // .22
    return c;
}

HistogramPaintSize histogram_paint_size(int width, int height, float scale) {
    const auto px = [scale](float mock_px) {
        return static_cast<int>(std::lround(mock(mock_px, scale)));
    };
    HistogramPaintSize s;
    s.width = width;
    s.height = height;
    s.inset_x = px(10.0f);
    s.inset_top = px(8.0f);
    s.triangle_width = px(14.0f);
    s.triangle_height = px(9.0f);
    s.grid_line = std::max(1, static_cast<int>(std::lround(scale)));
    return s;
}

std::uint64_t histogram_full_count(const StageHistogram& h) noexcept {
    std::uint64_t inner = 0;
    std::uint64_t all = 0;
    for (const StageHistogram::Counts* c : {&h.red, &h.green, &h.blue, &h.luminance}) {
        inner = std::max(inner, *std::max_element(c->begin() + 1, c->end() - 1));
        all = std::max(all, *std::max_element(c->begin(), c->end()));
    }
    return inner > 0 ? inner : all;
}

std::int64_t histogram_bar_height(std::uint64_t count, std::uint64_t full_count,
                                  std::int64_t full) noexcept {
    if (full_count == 0 || count == 0) {
        return 0;
    }
    if (count >= full_count) {
        return full;
    }
    const std::uint64_t f = static_cast<std::uint64_t>(full);
    // Exactly count * full / full_count, rounded down, whenever the product fits.
    if (count <= UINT64_MAX / f) {
        return static_cast<std::int64_t>(count * f / full_count);
    }
    // Only for counts beyond about 2^48 (a long double would differ between compilers): taken apart
    // so that nothing overflows. Here count > UINT64_MAX / f and count < full_count, so full_count
    // is more than f and `whole` is at least 1.
    const std::uint64_t whole = full_count / f;
    return static_cast<std::int64_t>(std::min(count / whole, f));
}

HistogramEdgePoint histogram_edge_point(int bin, std::uint64_t count, std::uint64_t full_count,
                                        const HistogramPaintSize& size) noexcept {
    const int columns = size.width - 2 * size.inset_x;
    const int rows = size.height - size.inset_top;
    HistogramEdgePoint p;
    // The painter puts bin b at column b * (columns - 1) / 255, and a column's centre is half a
    // pixel in.
    p.x = static_cast<float>(size.inset_x) +
          (static_cast<float>(bin) * static_cast<float>(std::max(columns - 1, 0)) /
               static_cast<float>(StageHistogram::bins - 1) +
           0.5f);
    const std::int64_t height =
        rows > 0 ? histogram_bar_height(count, full_count, static_cast<std::int64_t>(rows) * subpixels)
                 : 0;
    p.y = static_cast<float>(size.height) - static_cast<float>(height) / static_cast<float>(subpixels);
    return p;
}

void paint_histogram(const StageHistogram& h, const HistogramPaintSize& size,
                     const HistogramPaintColours& colours, std::vector<std::uint8_t>& rgba) {
    const int columns = size.width - 2 * size.inset_x;
    const int rows = size.height - size.inset_top;
    if (size.width <= 0 || size.height <= 0) {
        rgba.clear();
        return;
    }
    rgba.resize(static_cast<std::size_t>(size.width) * size.height * 4);

    // Every row starts as the plot colour, and those the fills reach have the grid lines in.
    std::vector<std::uint8_t> clean(static_cast<std::size_t>(size.width) * 4);
    for (std::size_t i = 0; i < clean.size(); i += 4) {
        clean[i] = colours.plot.r;
        clean[i + 1] = colours.plot.g;
        clean[i + 2] = colours.plot.b;
        clean[i + 3] = 255;
    }
    std::vector<std::uint8_t> ruled = clean;
    if (columns > 0) {
        for (int q = 1; q <= 3; ++q) {
            const int first = size.inset_x + columns * q / 4 - size.grid_line / 2;
            for (int x = std::max(first, 0); x < std::min(first + size.grid_line, size.width); ++x) {
                std::uint8_t* p = ruled.data() + static_cast<std::size_t>(x) * 4;
                p[0] = static_cast<std::uint8_t>(over(p[0], colours.grid.r, colours.grid.a));
                p[1] = static_cast<std::uint8_t>(over(p[1], colours.grid.g, colours.grid.a));
                p[2] = static_cast<std::uint8_t>(over(p[2], colours.grid.b, colours.grid.a));
            }
        }
    }
    for (int y = 0; y < size.height; ++y) {
        const std::vector<std::uint8_t>& start = y < size.inset_top ? clean : ruled;
        std::copy(start.begin(), start.end(),
                  rgba.begin() + static_cast<std::ptrdiff_t>(y) * size.width * 4);
    }

    if (columns > 0 && rows > 0) {
        const std::uint64_t full_count = histogram_full_count(h);
        std::vector<int> luma;
        std::vector<int> red;
        std::vector<int> green;
        std::vector<int> blue;
        column_heights(h.luminance, full_count, columns, rows, luma);
        column_heights(h.red, full_count, columns, rows, red);
        column_heights(h.green, full_count, columns, rows, green);
        column_heights(h.blue, full_count, columns, rows, blue);

        const Light red_light = light_of(colours.red);
        const Light green_light = light_of(colours.green);
        const Light blue_light = light_of(colours.blue);
        struct Blended {
            std::uint8_t r, g, b;
        };
        // What the fills make of a pixel of `base`, given how much of it each one covers (0..256).
        const auto blend = [&](const std::uint8_t* base, int l, int cr, int cg, int cb) {
            int r = base[0];
            int g = base[1];
            int b = base[2];
            if (l != 0) {
                const int a = colours.luminance.a * l / subpixels;
                r = over(r, colours.luminance.r, a);
                g = over(g, colours.luminance.g, a);
                b = over(b, colours.luminance.b, a);
            }
            const auto add = [&](const Light& light, int cover) {
                if (cover != 0) {
                    r = screen(r, light.r * cover / subpixels);
                    g = screen(g, light.g * cover / subpixels);
                    b = screen(b, light.b * cover / subpixels);
                }
            };
            add(red_light, cr);
            add(green_light, cg);
            add(blue_light, cb);
            return Blended{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                           static_cast<std::uint8_t>(b)};
        };
        // Nearly every pixel is off the grid lines and each fill covers it wholly or not at all, so
        // those come from a table of the 16 possible results: the same bytes, without the sums.
        std::array<Blended, 16> whole{};
        for (int i = 0; i < 16; ++i) {
            const auto cover = [i](int bit) { return (i >> bit) & 1 ? subpixels : 0; };
            whole[static_cast<std::size_t>(i)] =
                blend(clean.data(), cover(0), cover(1), cover(2), cover(3));
        }

        // Row by row, from the bottom.
        const auto covered = [](int height, int floor) {
            return std::clamp(height - floor, 0, subpixels);
        };
        for (int up = 0; up < rows; ++up) {
            const int floor = up * subpixels;
            std::uint8_t* p = rgba.data() +
                              static_cast<std::size_t>(size.height - 1 - up) * size.width * 4 +
                              static_cast<std::size_t>(size.inset_x) * 4;
            const std::uint8_t* base = ruled.data() + static_cast<std::size_t>(size.inset_x) * 4;
            const std::uint8_t* plain = clean.data() + static_cast<std::size_t>(size.inset_x) * 4;
            for (int x = 0; x < columns; ++x, p += 4, base += 4, plain += 4) {
                const auto i = static_cast<std::size_t>(x);
                const int l = covered(luma[i], floor);
                const int cr = covered(red[i], floor);
                const int cg = covered(green[i], floor);
                const int cb = covered(blue[i], floor);
                if ((l | cr | cg | cb) == 0) {
                    continue;
                }
                const bool all_or_nothing =
                    ((l | cr | cg | cb) & (subpixels - 1)) == 0;  // each is 0 or 256
                const bool on_a_line = base[0] != plain[0] || base[1] != plain[1] ||
                                       base[2] != plain[2];
                const Blended out = all_or_nothing && !on_a_line
                                        ? whole[static_cast<std::size_t>(
                                              (l >> 8) | ((cr >> 8) << 1) | ((cg >> 8) << 2) |
                                              ((cb >> 8) << 3))]
                                        : blend(base, l, cr, cg, cb);
                p[0] = out.r;
                p[1] = out.g;
                p[2] = out.b;
            }
        }
    }

    // The triangles stay inside the picture however small it is.
    const int width = std::min(size.triangle_width, size.width / 2);
    const int height = std::min(size.triangle_height, size.height - std::min(size.inset_top, size.height));
    const int left = std::clamp(size.inset_x, 0, std::max(size.width - width, 0));
    const int right = std::max(size.width - size.inset_x - width, 0);
    paint_triangle(rgba, size.width, left, size.inset_top, width, height,
                   h.any_under_black > 0 ? colours.shadows_lit : colours.triangle_unlit);
    paint_triangle(rgba, size.width, right, size.inset_top, width, height,
                   h.any_over_white > 0 ? colours.highlights_lit : colours.triangle_unlit);
}

bool HistogramPaintCache::update(const StageHistogram& h, const HistogramPaintSize& size,
                                 const HistogramPaintColours& colours) {
    if (painted_ && painted_->size == size && painted_->colours == colours &&
        painted_->histogram == h) {
        return false;
    }
    paint_histogram(h, size, colours, pixels_);
    painted_ = Painted{h, size, colours};
    return true;
}

}  // namespace prism::gui
