#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/frame.hpp"
#include "prism/gui/brush.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/close_guard.hpp"
#include "prism/gui/field_cache.hpp"
#include "prism/gui/neutral_pick.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/process.hpp"
#include "prism/raw.hpp"
#include "prism/recipe.hpp"
#include "prism/resize.hpp"
#include "prism/stroke_field.hpp"
#include "synthetic_dng.hpp"

// The easel in the app's model (#58): the frame in the edit settings, the framed preview base, the
// framed grids every stage after Decode is given, the brush's mapping from the framed canvas to the
// whole photo, the framed export, and the close prompt. Without a frame, everything is as it was.

namespace fs = std::filesystem;

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::Frame;
using prism::Image;
using prism::PixelGrid;
using prism::PixelSize;
using prism::StrokeField;
using prism::gui::EditSettings;
using prism::gui::PictureFrame;
using prism::gui::PreviewFrame;
using prism::gui::PreviewOptions;
using prism::gui::Session;
using prism::gui::StageId;
using prism::gui::Strokes;

Image make_image(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            const float s = 0.03f + static_cast<float>((x * 7 + y * 13) % 101) / 120.0f;
            p[0] = s;
            p[1] = 0.6f * s + 0.02f * static_cast<float>(x % 5);
            p[2] = 0.2f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    return img;
}

Strokes make_strokes(int w, int h) {
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.1f, 0.2f}, {0.5f, 0.55f}, {0.9f, 0.4f}}, 0.12f, 0.8f, -1.2f}));
    field->strokes.push_back(
        std::make_shared<const FieldStroke>(FieldStroke{{{0.45f, 0.6f}}, 0.2f, 1.0f, 1.5f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.2f, 0.7f}, {0.8f, 0.75f}}, 0.05f, 0.0f, 0.7f}));
    return Strokes(std::move(field));
}

EditSettings look_with_strokes(int w, int h) {
    EditSettings s;
    s.exposure_stops = 0.25f;
    s.saturation = 1.2f;
    s.contrast = 1.1f;
    s.curve = prism::gui::CurveKind::filmic;
    s.strokes = make_strokes(w, h);
    return s;
}

// A turned, cropped frame that fit_frame has to shrink: its corners reach past the turned photo.
Frame turned_frame(float angle) {
    Frame f;
    f.angle = angle;
    f.left = 0.03f;
    f.top = 0.02f;
    f.right = 0.95f;
    f.bottom = 0.97f;
    f.aspect = prism::Aspect::free;
    return f;
}

}  // namespace

// --- No frame, and a whole-photo frame, are the path of before ----------------------------------

TEST(FrameSettings, AWholePhotoFrameInTheSettingsRendersExactlyAsNone) {
    const Image base = make_image(160, 100);
    const EditSettings none = look_with_strokes(160, 100);
    EditSettings whole = none;
    whole.frame = Frame{};
    whole.frame->aspect = prism::Aspect::free;

    PreviewOptions options;
    options.clip_report = true;
    options.clip_mask = true;
    options.stroke_mask = true;
    options.scopes = true;
    for (const StageId stage : {StageId::decode, StageId::dodge_burn, prism::gui::final_stage}) {
        const auto a = prism::gui::render_preview(base, none, 0, {}, {}, stage, options);
        const auto b = prism::gui::render_preview(base, whole, 0, {}, {}, stage, options);
        ASSERT_TRUE(a && b);
        EXPECT_EQ(a->rgb8, b->rgb8);
        EXPECT_EQ(a->clip_mask, b->clip_mask);
        EXPECT_EQ(a->stroke_mask, b->stroke_mask);
        EXPECT_TRUE(a->clips && b->clips && *a->clips == *b->clips);
    }
    PreviewOptions strip;
    strip.test_strip = prism::gui::TestStrip{};
    const auto sa = prism::gui::render_preview(base, none, 0, {}, {}, prism::gui::final_stage, strip);
    const auto sb =
        prism::gui::render_preview(base, whole, 0, {}, {}, prism::gui::final_stage, strip);
    ASSERT_TRUE(sa && sb);
    EXPECT_EQ(sa->rgb8, sb->rgb8);
    EXPECT_EQ(prism::gui::compute_clip_report(base, none), prism::gui::compute_clip_report(base, whole));
    const auto ta = prism::gui::sample_trace(base, none, 70, 61);
    const auto tb = prism::gui::sample_trace(base, whole, 70, 61);
    ASSERT_TRUE(ta && tb);
    EXPECT_EQ(ta->srgb8, tb->srgb8);
    EXPECT_EQ(ta->dodge_burn_stops, tb->dodge_burn_stops);
    EXPECT_NE(ta->dodge_burn_stops, 0.0f);  // the strokes reach it
}

// The export of 0.5.0, spelled out: decode, reduce, then the stages with the strokes on the whole
// reduced image. A job without a frame and one with a whole-photo frame both write exactly that,
// at every export size, with strokes.
// --- FieldCache on framed grids -----------------------------------------------------------------

TEST(FramedFieldCache, ATurnWithTheSameSizeIsAnotherGrid) {
    const Strokes strokes = make_strokes(200, 133);
    const PixelGrid a = PixelGrid::framed(turned_frame(2.0f), 200, 133, 1.0f, 150, 110);
    const PixelGrid b = PixelGrid::framed(turned_frame(2.5f), 200, 133, 1.0f, 150, 110);
    EXPECT_FALSE(prism::gui::same_grid(a, b));
    EXPECT_TRUE(prism::gui::same_grid(a, a));
    EXPECT_FALSE(prism::gui::same_grid(a, PixelGrid{200, 133, 0, 0, 1, 150, 110}));
    prism::gui::FieldCache cache;
    ASSERT_EQ(cache.update(strokes, a, 0), prism::gui::FieldPath::fresh);
    EXPECT_EQ(cache.update(strokes, b, 0), prism::gui::FieldPath::fresh);
    std::vector<float> fresh(150u * 110u);
    prism::rasterise_field(*strokes.field(), b, 0, 110, fresh);
    ASSERT_EQ(cache.field().size(), fresh.size());
    EXPECT_TRUE(std::equal(fresh.begin(), fresh.end(), cache.field().begin(),
                           [](float x, float y) {
                               return std::bit_cast<std::uint32_t>(x) ==
                                      std::bit_cast<std::uint32_t>(y);
                           }));
    // A band of a framed grid is framed the same way, and its values are the whole field's.
    const PixelGrid band = prism::sub_grid(b, prism::GridRect{10, 90, 20, 60});
    ASSERT_TRUE(band.framing.has_value());
    std::vector<float> part;
    ASSERT_TRUE(prism::gui::field_at(cache.field(), b, band, part));
    std::vector<float> band_fresh(80u * 40u);
    prism::rasterise_field(*strokes.field(), band, 0, 40, band_fresh);
    EXPECT_EQ(part, band_fresh);
    // Another frame's band is not a part of this field.
    EXPECT_FALSE(prism::gui::field_at(cache.field(), a, band, part));
}

TEST(FramedFieldCache, DrawingOnAFramedGridIsBitIdenticalToRasterisingAfresh) {
    const PixelGrid grid = PixelGrid::framed(prism::fit_frame(turned_frame(-3.0f), 200, 133), 200,
                                             133, 1.0f, 150, 110);
    auto field = std::make_shared<StrokeField>(*make_strokes(200, 133).field());
    prism::gui::FieldCache cache;
    ASSERT_EQ(cache.update(Strokes(field), grid, 0), prism::gui::FieldPath::fresh);
    FieldStroke stroke{{{0.3f, 0.3f}}, 0.07f, 0.6f, -0.8f};
    for (int k = 0; k < 6; ++k) {
        auto next = std::make_shared<StrokeField>(*field);
        if (k == 0) {
            next->strokes.push_back(std::make_shared<const FieldStroke>(stroke));
        } else {
            stroke.points.push_back({0.3f + 0.06f * k, 0.3f + 0.04f * k});
            next->strokes.back() = std::make_shared<const FieldStroke>(stroke);
        }
        const prism::gui::FieldPath path = cache.update(Strokes(next), grid, 0);
        EXPECT_EQ(path, k == 0 ? prism::gui::FieldPath::append : prism::gui::FieldPath::grow);
        std::vector<float> fresh(150u * 110u);
        prism::rasterise_field(*next, grid, 0, 110, fresh);
        ASSERT_EQ(cache.field().size(), fresh.size());
        for (std::size_t i = 0; i < fresh.size(); ++i) {
            ASSERT_EQ(std::bit_cast<std::uint32_t>(cache.field()[i]),
                      std::bit_cast<std::uint32_t>(fresh[i]))
                << "step " << k << ", pixel " << i;
        }
        field = next;
    }
}
