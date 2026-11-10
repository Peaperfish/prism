#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "prism/gui/field_cache.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"
#include "prism/stroke_field.hpp"

// The preview's stroke field cache: whichever way it gets to the field of the strokes it is given
// (from scratch, as it was, a stroke grown, a stroke added, a stroke undone), the field and the
// rendered preview are bit-identical to a render that rasterises everything afresh.

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::Image;
using prism::PixelGrid;
using prism::StrokeField;
using prism::gui::EditSettings;
using prism::gui::FieldCache;
using prism::gui::FieldPath;
using prism::gui::PreviewFrame;
using prism::gui::PreviewOptions;
using prism::gui::StageId;
using prism::gui::Strokes;

using StrokePtr = std::shared_ptr<const FieldStroke>;

Image make_image(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            const float s = 0.02f + static_cast<float>((x * 7 + y * 13) % 101) / 110.0f;
            p[0] = s;
            p[1] = 0.6f * s + 0.01f * static_cast<float>(x % 5);
            p[2] = 0.3f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    return img;
}

StrokePtr stroke(std::vector<FieldPoint> points, float radius, float softness, float weight) {
    return std::make_shared<const FieldStroke>(
        FieldStroke{std::move(points), radius, softness, weight});
}

// The first `n` points of `s`, with its brush: the stroke as it was while being drawn.
StrokePtr first_points(const FieldStroke& s, std::size_t n) {
    return stroke({s.points.begin(), s.points.begin() + static_cast<std::ptrdiff_t>(n)}, s.radius,
                  s.softness, s.weight);
}

StrokePtr with_weight(const FieldStroke& s, float weight) {
    return stroke(s.points, s.radius, s.softness, weight);
}

// One step of the script: the strokes after it, the image they are rendered on, and the path the
// cache should take to them.
struct Step {
    std::string what;
    Strokes strokes;
    int w = 0;
    int h = 0;
    FieldPath expect = FieldPath::fresh;
};

// A drawing session on a w x h photo: a stroke grown point by point that crosses itself and runs
// off the image, a second stroke, undo, redo, re-renders of the same strokes, a weight change of
// an earlier stroke and of the last one, a third stroke undone twice, clearing them all, a new
// stroke, and then another base size.
std::vector<Step> script(int w, int h) {
    const float aspect = prism::aspect_of(w, h);
    const auto field = [aspect](std::vector<StrokePtr> strokes) {
        auto f = std::make_shared<StrokeField>();
        f->aspect = aspect;
        f->strokes = std::move(strokes);
        return Strokes(std::move(f));
    };
    std::vector<Step> steps;
    const auto add = [&](std::string what, Strokes s, FieldPath expect, int sw = 0, int sh = 0) {
        steps.push_back({std::move(what), std::move(s), sw ? sw : w, sh ? sh : h, expect});
    };

    const FieldStroke a{{{0.2f, 0.3f},
                         {0.35f, 0.25f},
                         {0.5f, 0.35f},
                         {0.55f, 0.5f},
                         {0.45f, 0.6f},
                         {0.35f, 0.5f},
                         {0.4f, 0.3f},  // from here it crosses its own first segments
                         {0.5f, 0.2f},
                         {0.65f, 0.25f},
                         {0.8f, 0.4f},
                         {0.9f, 0.6f},
                         {1.05f, 0.7f}},  // and runs off the right edge
                        0.05f,
                        0.6f,
                        0.9f};
    StrokePtr a_done;
    for (std::size_t n = 1; n <= a.points.size(); ++n) {
        a_done = first_points(a, n);
        add("A with " + std::to_string(n) + " points", field({a_done}),
            n == 1 ? FieldPath::fresh : FieldPath::grow);
    }
    const Strokes just_a = field({a_done});
    add("A again, the same handle", just_a, FieldPath::reuse);
    add("A again, the same handle", just_a, FieldPath::reuse);
    add("A again, a new handle with the same stroke", field({a_done}), FieldPath::reuse);

    const FieldStroke b{{{0.7f, 0.7f}, {0.6f, 0.8f}, {0.3f, 0.75f}, {0.25f, 0.5f}, {0.3f, 0.2f}},
                        0.08f,
                        0.8f,
                        -0.8f};
    StrokePtr b4;
    for (std::size_t n = 1; n <= 4; ++n) {
        b4 = first_points(b, n);
        add("B with " + std::to_string(n) + " points", field({a_done, b4}),
            n == 1 ? FieldPath::append : FieldPath::grow);
    }
    add("undo B", field({a_done}), FieldPath::undo);
    add("redo B", field({a_done, b4}), FieldPath::append);
    const StrokePtr b5 = first_points(b, 5);
    add("B with 5 points", field({a_done, b5}), FieldPath::grow);

    const StrokePtr a_lighter = with_weight(*a_done, 0.4f);
    add("A's weight changed", field({a_lighter, b5}), FieldPath::fresh);
    add("undo after a fresh field", field({a_lighter}), FieldPath::undo);
    const StrokePtr c = stroke({{0.1f, 0.9f}, {0.9f, 0.1f}}, 0.02f, 0.0f, 1.7f);
    add("C, a hard line", field({a_lighter, c}), FieldPath::append);
    const StrokePtr e = stroke({{0.3f, 0.4f}}, 0.1f, 0.5f, -2.5f);
    add("E, a dab", field({a_lighter, c, e}), FieldPath::append);
    add("undo E", field({a_lighter, c}), FieldPath::undo);
    add("undo C: nothing kept from before it", field({a_lighter}), FieldPath::fresh);
    add("the last stroke's weight changed", field({with_weight(*a_lighter, -1.3f)}),
        FieldPath::fresh);
    add("cleared", Strokes(), FieldPath::fresh);

    const FieldStroke d{{{0.5f, 0.5f}, {0.52f, 0.55f}, {0.6f, 0.6f}}, 0.2f, 1.0f, 2.5f};
    add("D, a big soft dodge", field({first_points(d, 1)}), FieldPath::fresh);
    add("D on another base size", field({first_points(d, 1)}), FieldPath::fresh, w - 100, h + 37);
    add("D grown on that size", field({first_points(d, 2)}), FieldPath::grow, w - 100, h + 37);
    add("D back on the first size", field({first_points(d, 3)}), FieldPath::fresh);
    return steps;
}

EditSettings settings_with(const Strokes& strokes) {
    EditSettings s;
    s.exposure_stops = 0.3f;
    s.saturation = 1.2f;
    s.contrast = 1.1f;
    s.curve = prism::gui::CurveKind::filmic;
    s.strokes = strokes;
    return s;
}

// What a step renders: the plain picture with scopes and the clip mask, the clip report while
// viewing Exposure (the stroke stage then runs on the report's sample grid only), or a test strip.
struct RenderCase {
    StageId stage;
    PreviewOptions options;
};

RenderCase render_case(std::size_t step) {
    PreviewOptions o;
    switch (step % 3) {
        case 0:
            o.scopes = true;
            o.clip_mask = true;
            return {StageId::encode, o};
        case 1:
            o.clip_report = true;
            return {StageId::exposure, o};
        default:
            o.test_strip = prism::gui::TestStrip{};
            o.clip_mask = true;
            return {StageId::encode, o};
    }
}

PreviewFrame render(const Image& base, const EditSettings& s, const RenderCase& c,
                    FieldCache* cache) {
    std::optional<PreviewFrame> frame =
        prism::gui::render_preview(base, s, 0, {}, {}, c.stage, c.options, cache);
    EXPECT_TRUE(frame.has_value());
    EXPECT_TRUE(frame && frame->error.empty()) << (frame ? frame->error : "");
    return frame ? std::move(*frame) : PreviewFrame{};
}

void expect_same_frame(const PreviewFrame& got, const PreviewFrame& want, const std::string& what) {
    EXPECT_EQ(got.width, want.width) << what;
    EXPECT_EQ(got.height, want.height) << what;
    EXPECT_TRUE(got.rgb8 == want.rgb8) << what;
    EXPECT_TRUE(got.histogram == want.histogram) << what;
    EXPECT_TRUE(got.scopes == want.scopes) << what;
    EXPECT_TRUE(got.clips == want.clips) << what;
    EXPECT_TRUE(got.clip_mask == want.clip_mask) << what;
    ASSERT_EQ(got.strip.size(), want.strip.size()) << what;
    for (std::size_t i = 0; i < got.strip.size(); ++i) {
        EXPECT_TRUE(got.strip[i].rect == want.strip[i].rect) << what << ", band " << i;
        EXPECT_TRUE(got.strip[i].histogram == want.strip[i].histogram) << what << ", band " << i;
    }
}

// Index of the first float whose bits differ, or -1 (0 if the sizes differ).
long first_bit_difference(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size()) {
        return 0;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i])) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

std::vector<float> rasterised(const Strokes& strokes, const PixelGrid& g) {
    if (strokes.empty()) {
        return {};
    }
    std::vector<float> out(static_cast<std::size_t>(g.width()) * static_cast<std::size_t>(g.height()));
    prism::rasterise_field(*strokes.field(), g, 0, g.height(), out);
    return out;
}

struct Size {
    int w;
    int h;
};

class FieldCacheScript : public ::testing::TestWithParam<Size> {};

}  // namespace

// A landscape and a portrait photo, both above the library's multi-threaded threshold.
INSTANTIATE_TEST_SUITE_P(Sizes, FieldCacheScript, ::testing::Values(Size{1501, 701}, Size{733, 1409}),
                         [](const ::testing::TestParamInfo<Size>& info) {
                             return std::to_string(info.param.w) + "x" +
                                    std::to_string(info.param.h);
                         });

// After every step the cache's field is bit-identical to rasterising the strokes afresh, and the
// preview rendered with it is bit-identical to one rendered with a cold cache and to one rendered
// without a cache (the stage rasterising, as before the cache).
TEST_P(FieldCacheScript, CacheMatchesFresh) {
    const auto [w, h] = GetParam();
    FieldCache warm;
    const std::vector<Step> steps = script(w, h);
    std::optional<Image> base;
    for (std::size_t k = 0; k < steps.size(); ++k) {
        const Step& step = steps[k];
        if (!base || base->width != step.w || base->height != step.h) {
            base = make_image(step.w, step.h);
        }
        const std::string what = std::to_string(k) + ": " + step.what;
        const EditSettings s = settings_with(step.strokes);
        const RenderCase c = render_case(k);

        const PreviewFrame with_warm = render(*base, s, c, &warm);
        EXPECT_EQ(first_bit_difference(
                      warm.field(), rasterised(step.strokes, PixelGrid::whole(step.w, step.h))),
                  -1)
            << what << " (took " << prism::gui::field_path_name(warm.last_path()) << ")";
        FieldCache cold;
        expect_same_frame(with_warm, render(*base, s, c, &cold), what + ", against a cold cache");
        expect_same_frame(with_warm, render(*base, s, c, nullptr), what + ", against no cache");
    }
}

// Each step takes the path it should, so a cache that rasterised everything every time (or never
// noticed a change) fails here even though its pictures would be right.
TEST_P(FieldCacheScript, TakesTheExpectedPath) {
    const auto [w, h] = GetParam();
    FieldCache cache;
    EXPECT_EQ(cache.last_path(), FieldPath::none);
    std::optional<Image> base;
    for (const Step& step : script(w, h)) {
        if (!base || base->width != step.w || base->height != step.h) {
            base = make_image(step.w, step.h);
        }
        PreviewOptions o;
        (void)render(*base, settings_with(step.strokes), {StageId::encode, o}, &cache);
        EXPECT_EQ(prism::gui::field_path_name(cache.last_path()),
                  prism::gui::field_path_name(step.expect))
            << step.what;
    }
}

// The cache keeps the last stroke's coverage only over a box around it: a small stroke costs far
// less than a third whole grid. Clearing lets go of everything.
TEST(FieldCache, KeepsTheLastStrokeOnlyOverItsBox) {
    constexpr int W = 1501;
    constexpr int H = 701;
    const std::size_t grid_bytes = static_cast<std::size_t>(W) * H * sizeof(float);
    auto f = std::make_shared<StrokeField>();
    f->aspect = prism::aspect_of(W, H);
    f->strokes.push_back(stroke({{0.5f, 0.5f}, {0.52f, 0.5f}}, 0.01f, 0.5f, 1.0f));
    FieldCache cache;
    cache.update(Strokes(f), PixelGrid::whole(W, H), 0);
    EXPECT_GE(cache.bytes(), 2 * grid_bytes);
    EXPECT_LT(cache.bytes(), 2 * grid_bytes + grid_bytes / 10);
    cache.update(Strokes(), PixelGrid::whole(W, H), 0);
    EXPECT_EQ(cache.bytes(), 0u);
    EXPECT_TRUE(cache.field().empty());
}

// field_at picks a part of the picture's field: a band, a strided grid; and refuses a part whose
// pixels are not pixels of the field's grid.
TEST(FieldCache, FieldAtReadsThePixelsOfAPart) {
    constexpr int W = 97;
    constexpr int H = 61;
    StrokeField f;
    f.aspect = prism::aspect_of(W, H);
    f.strokes.push_back(stroke({{0.1f, 0.2f}, {0.8f, 0.7f}}, 0.2f, 0.7f, 1.25f));
    const PixelGrid whole = PixelGrid::whole(W, H);
    std::vector<float> field(static_cast<std::size_t>(W) * H);
    prism::rasterise_field(f, whole, 0, H, field);

    for (const PixelGrid part : {PixelGrid{W, H, 10, 5, 1, 30, 20}, PixelGrid{W, H, 3, 4, 7},
                                 PixelGrid{W, H, W - 1, H - 1, 1}, whole}) {
        std::vector<float> got;
        ASSERT_TRUE(prism::gui::field_at(field, whole, part, got));
        std::vector<float> want(static_cast<std::size_t>(part.width()) * part.height());
        prism::rasterise_field(f, part, 0, part.height(), want);
        EXPECT_EQ(first_bit_difference(got, want), -1) << part.x0 << ", " << part.y0;
    }
    std::vector<float> got{1.0f};
    EXPECT_FALSE(prism::gui::field_at(field, whole, PixelGrid::whole(W + 1, H), got));
    EXPECT_TRUE(got.empty());
    const PixelGrid strided{W, H, 1, 1, 2};
    std::vector<float> on_strided(static_cast<std::size_t>(strided.width()) * strided.height());
    EXPECT_FALSE(prism::gui::field_at(on_strided, strided, PixelGrid{W, H, 2, 1, 2}, got));
    EXPECT_TRUE(prism::gui::field_at(on_strided, strided, PixelGrid{W, H, 3, 5, 4}, got));
}
