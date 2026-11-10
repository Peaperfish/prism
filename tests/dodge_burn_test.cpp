#include "prism/dodge_burn.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "prism/parallel.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/stroke_field.hpp"

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::Image;
using prism::LocalExposure;
using prism::PixelGrid;
using prism::ProcessSettings;
using prism::StrokeField;

std::shared_ptr<const FieldStroke> stroke(std::vector<FieldPoint> points, float radius,
                                          float softness, float weight) {
    return std::make_shared<const FieldStroke>(
        FieldStroke{std::move(points), radius, softness, weight});
}

// A w x h image of varied colours, from below black to well above white, so that every stage
// changes most pixels.
Image make_image(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            const float s = static_cast<float>((x * 7 + y * 13) % 101) / 40.0f;  // 0 to 2.5
            p[0] = s;
            p[1] = 0.5f * s + 0.01f * static_cast<float>(x % 5);
            p[2] = static_cast<float>(y % 11) / 10.0f - 0.05f;  // a few negatives
        }
    }
    return img;
}

std::vector<std::uint32_t> bits(const Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

// Index of the first float whose bits differ, or -1.
long first_difference(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size()) {
        return 0;
    }
    const std::vector<std::uint32_t> x = bits(a);
    const std::vector<std::uint32_t> y = bits(b);
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i]) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

// Dodges and burns that cover part of the image, with soft and hard edges.
StrokeField some_strokes(int w, int h) {
    StrokeField f;
    f.aspect = prism::aspect_of(w, h);
    f.strokes.push_back(stroke({{0.1f, 0.2f}, {0.5f, 0.5f}, {0.8f, 0.3f}}, 0.08f, 0.6f, 1.0f));
    f.strokes.push_back(stroke({{0.3f, 0.7f}, {0.6f, 0.6f}}, 0.06f, 0.0f, -0.75f));
    f.strokes.push_back(stroke({{0.5f, 0.45f}}, 0.1f, 1.0f, 0.5f));
    return f;
}

std::vector<float> field_of(const StrokeField& f, const PixelGrid& g) {
    std::vector<float> out(static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height()));
    prism::rasterise_field(f, g, 0, g.height(), out);
    return out;
}

const prism::Matrix3 warm = {1.2f, 0.0f, 0.0f,  //
                             0.0f, 1.0f, 0.0f,  //
                             0.0f, 0.0f, 0.7f};
const prism::Matrix3 grade = {1.05f,  0.03f,  -0.01f,  //
                              0.01f,  1.00f,  0.00f,   //
                              -0.02f, -0.02f, 0.94f};

// Every stage on, with dodge and burn from `field` over the whole of a w x h image.
ProcessSettings all_stages(const StrokeField& field, int w, int h) {
    ProcessSettings s;
    s.exposure_stops = 0.3f;
    s.dodge_burn = {field, PixelGrid::whole(w, h)};
    s.white_balance = warm;
    s.matrix = grade;
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = prism::parse_curve("filmic");
    return s;
}

// The y of the centre of image row `row` on an image `h` rows tall, in stroke coordinates.
float v_of_row(int row, int h) {
    return (static_cast<float>(row) + 0.5f) / static_cast<float>(h);
}

// Strokes that cross 64-row tile edges, as seen from every chunk the parallel loop makes for 1 to
// 8 threads on a 701-row image (chunk starts 0, 87, 100, 175, 200, 233, 262, 300, 350, ...; a tile
// edge is a chunk start plus a multiple of 64), and the chunk edges themselves.
StrokeField boundary_strokes(int w, int h) {
    StrokeField f;
    f.aspect = prism::aspect_of(w, h);
    // Two long diagonals, one dodge and one burn, through every row.
    f.strokes.push_back(stroke({{0.02f, 0.01f}, {0.5f, 0.6f}, {0.98f, 0.99f}}, 0.03f, 0.5f, 0.8f));
    f.strokes.push_back(stroke({{0.95f, 0.02f}, {0.4f, 0.5f}, {0.05f, 0.97f}}, 0.02f, 0.0f, -1.2f));
    // Short horizontal strokes centred on tile and chunk edges.
    float u = 0.05f;
    for (const int row : {64, 128, 164, 233, 239, 297, 350, 414, 467, 526, 600}) {
        const float v = v_of_row(row, h);
        f.strokes.push_back(stroke({{u, v}, {u + 0.12f, v}}, 0.006f, 0.7f,
                                   row % 2 == 0 ? 0.6f : -0.6f));
        u = u > 0.8f ? 0.05f : u + 0.08f;
    }
    return f;
}

// --- The tests.

TEST(DodgeBurn, ThreadCountInvariant) {
    constexpr int W = 1501;
    constexpr int H = 701;  // 1501 x 701 is just over parallel_min_pixels, so threads are used
    static_assert(static_cast<std::size_t>(W) * H >= prism::parallel_min_pixels);
    const StrokeField f = boundary_strokes(W, H);
    const ProcessSettings s = all_stages(f, W, H);

    Image one = make_image(W, H);
    ASSERT_EQ(prism::process(one, s, 1), prism::ProcessResult::completed);
    // The strokes do something: the result differs from the same pipeline without them.
    Image without = make_image(W, H);
    ProcessSettings no_strokes = s;
    no_strokes.dodge_burn = {};
    ASSERT_EQ(prism::process(without, no_strokes, 1), prism::ProcessResult::completed);
    ASSERT_NE(first_difference(one, without), -1);

    for (const int threads : {2, 3, 7, 8}) {
        Image img = make_image(W, H);
        ASSERT_EQ(prism::process(img, s, threads), prism::ProcessResult::completed);
        EXPECT_EQ(first_difference(img, one), -1) << threads << " threads";
    }
}

TEST(DodgeBurn, StageMatchesThePointwiseGain) {
    constexpr int W = 300;
    constexpr int H = 200;
    const StrokeField f = some_strokes(W, H);
    const std::vector<float> field = field_of(f, PixelGrid::whole(W, H));
    const Image in = make_image(W, H);
    Image out = in;
    prism::dodge_burn(out, {f, PixelGrid::whole(W, H)}, 1);

    // fast_exp2 is within about 2e-7 of 2^x (fast_math.hpp), and the product adds half an ulp
    // (6e-8): 4e-7 relative bounds both with room to spare.
    constexpr double tolerance = 4e-7;
    std::size_t touched = 0;
    double worst = 0;
    for (std::size_t p = 0; p < field.size(); ++p) {
        if (field[p] == 0.0f) {
            continue;
        }
        ++touched;
        const double gain = std::exp2(static_cast<double>(field[p]));
        for (std::size_t c = 0; c < 3; ++c) {
            const double expected = static_cast<double>(in.pixels[p * 3 + c]) * gain;
            const double got = out.pixels[p * 3 + c];
            const double error = std::abs(got - expected) / std::max(std::abs(expected), 1e-30);
            if (expected != 0.0) {
                worst = std::max(worst, error);
            }
            ASSERT_LE(error, tolerance) << "pixel " << p << " channel " << c;
        }
    }
    EXPECT_GT(touched, field.size() / 10);
    char worst_text[32];
    std::snprintf(worst_text, sizeof worst_text, "%.3g", worst);
    RecordProperty("worst_relative_error", worst_text);
}

TEST(DodgeBurn, FieldZeroPixelsAreBitIdentical) {
    constexpr int W = 120;
    constexpr int H = 80;
    const StrokeField f = some_strokes(W, H);
    const std::vector<float> field = field_of(f, PixelGrid::whole(W, H));
    Image in = make_image(W, H);
    // Values that a sloppy gain of 1 might not leave alone, all over the image.
    const float specials[] = {-0.0f,
                              0.0f,
                              std::numeric_limits<float>::denorm_min(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::max(),
                              -3.5f,
                              0.1f};
    for (std::size_t i = 0; i < in.pixels.size(); i += 5) {
        in.pixels[i] = specials[(i / 5) % std::size(specials)];
    }
    Image out = in;
    prism::dodge_burn(out, {f, PixelGrid::whole(W, H)}, 1);

    std::size_t zero = 0;
    std::size_t changed = 0;
    for (std::size_t p = 0; p < field.size(); ++p) {
        for (std::size_t c = 0; c < 3; ++c) {
            const auto a = std::bit_cast<std::uint32_t>(in.pixels[p * 3 + c]);
            const auto b = std::bit_cast<std::uint32_t>(out.pixels[p * 3 + c]);
            if (field[p] == 0.0f) {
                ASSERT_EQ(a, b) << "pixel " << p << " channel " << c << " has field 0";
            } else {
                changed += a != b;
            }
        }
        zero += field[p] == 0.0f;
    }
    EXPECT_GT(zero, field.size() / 4);
    EXPECT_GT(changed, field.size() / 10);
}

TEST(DodgeBurn, NoStrokesIsBitIdenticalToNoStage) {
    constexpr int W = 64;
    constexpr int H = 48;
    // No strokes: the grid is not even looked at, so a grid that fits nothing is fine.
    StrokeField empty;
    empty.aspect = prism::aspect_of(W, H);
    for (const LocalExposure& local :
         {LocalExposure{}, LocalExposure{empty, PixelGrid::whole(W, H)},
          LocalExposure{empty, PixelGrid{1, 1, 0, 0, 1}}}) {
        Image img = make_image(W, H);
        prism::dodge_burn(img, local, 0);
        EXPECT_EQ(first_difference(img, make_image(W, H)), -1);

        // The whole pipeline with this field equals the stages called by hand without one (the
        // pipeline as it was before dodge and burn), and the stage never runs.
        ProcessSettings s = all_stages(empty, W, H);
        s.dodge_burn = local;
        Image piped = make_image(W, H);
        std::vector<std::string> ran;
        ASSERT_EQ(prism::process(piped, s, 0, {}, {},
                                 [&](std::string_view name, const Image&) {
                                     ran.emplace_back(name);
                                 }),
                  prism::ProcessResult::completed);
        Image by_hand = make_image(W, H);
        prism::exposure(by_hand, *s.exposure_stops, 1);
        prism::apply_matrix(by_hand, *s.white_balance, 1);
        prism::apply_matrix(by_hand, *s.matrix, 1);
        prism::saturation(by_hand, *s.saturation, 1);
        prism::contrast(by_hand, *s.contrast, 1);
        prism::tone_curve(by_hand, *s.curve, 1);
        EXPECT_EQ(first_difference(piped, by_hand), -1);
        EXPECT_EQ(ran, (std::vector<std::string>{"exposure", "white_balance", "matrix",
                                                 "saturation", "contrast", "curve"}));
    }
}

TEST(DodgeBurn, StageOrder) {
    constexpr int W = 200;
    constexpr int H = 150;
    const StrokeField f = some_strokes(W, H);
    const ProcessSettings s = all_stages(f, W, H);

    Image piped = make_image(W, H);
    std::vector<std::string> ran;
    ASSERT_EQ(prism::process(piped, s, 1, {}, {},
                             [&](std::string_view name, const Image&) { ran.emplace_back(name); }),
              prism::ProcessResult::completed);
    EXPECT_EQ(ran, (std::vector<std::string>{"exposure", "dodge_burn", "white_balance", "matrix",
                                             "saturation", "contrast", "curve"}));

    // The gain and the two stages around it all scale linearly, so the order shows only in the
    // rounding: process() must match exposure -> dodge and burn -> white balance bit for bit, and
    // each other order must not (which shows this test can tell them apart).
    const auto rest = [&](Image& img) {
        prism::apply_matrix(img, *s.matrix, 1);
        prism::saturation(img, *s.saturation, 1);
        prism::contrast(img, *s.contrast, 1);
        prism::tone_curve(img, *s.curve, 1);
    };
    Image right = make_image(W, H);
    prism::exposure(right, *s.exposure_stops, 1);
    prism::dodge_burn(right, s.dodge_burn, 1);
    prism::apply_matrix(right, *s.white_balance, 1);
    rest(right);
    EXPECT_EQ(first_difference(piped, right), -1);

    Image before_exposure = make_image(W, H);
    prism::dodge_burn(before_exposure, s.dodge_burn, 1);
    prism::exposure(before_exposure, *s.exposure_stops, 1);
    prism::apply_matrix(before_exposure, *s.white_balance, 1);
    rest(before_exposure);
    EXPECT_NE(first_difference(piped, before_exposure), -1);

    Image after_white_balance = make_image(W, H);
    prism::exposure(after_white_balance, *s.exposure_stops, 1);
    prism::apply_matrix(after_white_balance, *s.white_balance, 1);
    prism::dodge_burn(after_white_balance, s.dodge_burn, 1);
    rest(after_white_balance);
    EXPECT_NE(first_difference(piped, after_white_balance), -1);
}

TEST(DodgeBurn, NotClampedAboveWhite) {
    constexpr int W = 40;
    constexpr int H = 30;
    // One hard dodge of 2 stops over the whole image, and a burn of half a stop.
    for (const float weight : {2.0f, -0.5f}) {
        StrokeField f;
        f.aspect = prism::aspect_of(W, H);
        f.strokes.push_back(stroke({{0.5f, 0.5f}}, 2.0f, 0.0f, weight));
        Image img;
        img.width = W;
        img.height = H;
        img.pixels.assign(static_cast<std::size_t>(W) * H * 3, 3.0f);  // above white already
        img.pixels[1] = 0.6f;
        img.pixels[2] = -0.4f;
        const Image in = img;
        prism::dodge_burn(img, {f, PixelGrid::whole(W, H)}, 1);
        const float gain = prism::stroke_gain(weight);
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            ASSERT_EQ(std::bit_cast<std::uint32_t>(img.pixels[i]),
                      std::bit_cast<std::uint32_t>(in.pixels[i] * gain))
                << "sample " << i << ", weight " << weight;
        }
        EXPECT_GT(img.pixels[0], 1.0f);
        EXPECT_GT(img.pixels[1], weight > 0 ? 1.0f : 0.0f);  // 0.6 dodged past white
        EXPECT_LT(img.pixels[2], 0.0f);                       // below black stays below black
    }
}

TEST(DodgeBurn, ThrowsWhenTheGridDoesNotFit) {
    // Taller than one tile of rows: a grid a row short must be refused before the first tile is
    // applied, not when the last one is found not to fit.
    constexpr int W = 50;
    constexpr int H = 3 * prism::dodge_burn_tile_rows + 10;
    const StrokeField f = some_strokes(W, H);
    for (const PixelGrid g : {PixelGrid::whole(W + 1, H), PixelGrid::whole(W, H - 1),
                              PixelGrid::whole(W - 1, H), PixelGrid{W, H, 1, 0, 1},
                              PixelGrid{2 * W, 2 * H, 0, 0, 3}, PixelGrid{}}) {
        Image img = make_image(W, H);
        try {
            prism::dodge_burn(img, {f, g}, 1);
            ADD_FAILURE() << "no exception";
        } catch (const std::invalid_argument& e) {
            // The stage's own message, which says what does not fit.
            EXPECT_EQ(std::string(e.what()).rfind("dodge_burn: the stroke grid is ", 0), 0u)
                << e.what();
        }
        EXPECT_EQ(first_difference(img, make_image(W, H)), -1)  // untouched
            << g.full_w << " x " << g.full_h << " from " << g.x0 << ", " << g.y0;

        ProcessSettings s;
        s.dodge_burn = {f, g};
        EXPECT_THROW((void)prism::process(img, s, 1), std::invalid_argument);
    }
    // Grids of exactly W x H pixels fit, whatever image they sample.
    for (const PixelGrid g : {PixelGrid{2 * W, 2 * H, 0, 0, 2}, PixelGrid{W + 7, H + 3, 7, 3, 1}}) {
        Image img = make_image(W, H);
        EXPECT_NO_THROW(prism::dodge_burn(img, {f, g}, 1));
    }
}

TEST(DodgeBurn, BandEqualsCrop) {
    constexpr int W = 240;
    constexpr int H = 160;
    const StrokeField f = some_strokes(W, H);
    const ProcessSettings whole_settings = all_stages(f, W, H);
    Image whole = make_image(W, H);
    ASSERT_EQ(prism::process(whole, whole_settings, 2), prism::ProcessResult::completed);

    for (const auto [x, y] : {std::pair{0, 0}, std::pair{0, 63}, std::pair{37, 0},
                              std::pair{101, 70}, std::pair{W - 1, H - 1}}) {
        // The part of the source image from (x, y) to the right and bottom edges.
        const auto crop = [&](const Image& src) {
            Image c;
            c.width = W - x;
            c.height = H - y;
            for (int row = y; row < H; ++row) {
                const float* from = src.pixels.data() + (static_cast<std::size_t>(row) * W + x) * 3;
                c.pixels.insert(c.pixels.end(), from, from + static_cast<std::size_t>(c.width) * 3);
            }
            return c;
        };
        Image band = crop(make_image(W, H));
        ProcessSettings s = whole_settings;
        s.dodge_burn.grid = PixelGrid{W, H, x, y, 1};
        ASSERT_EQ(prism::process(band, s, 2), prism::ProcessResult::completed);
        EXPECT_EQ(first_difference(band, crop(whole)), -1) << "band from (" << x << ", " << y << ")";
    }
}

}  // namespace
