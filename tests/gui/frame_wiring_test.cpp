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

namespace {

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) / ("prism_frame_wiring_" + std::string(test->name()) +
                                           "_" + std::to_string(pid) + "_" + name);
}

// A w x h image of varied colours within 0..1 (so a PNG keeps them), different in every pixel's
// neighbourhood so that a turn or a misplaced stroke changes many values.
fs::path write_png(const std::string& name, int w, int h) {
    const fs::path path = temp_file(name);
    prism::save_image(path, make_image(w, h));
    return path;
}

// Strokes on a w x h photo: a long soft burn across it, a soft dodge dab and a hard dodge line.
std::vector<std::uint32_t> bits(const Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

// The stages run on `picture` with the strokes on `grid`, as 8-bit sRGB.
std::vector<std::uint8_t> rendered(Image picture, const EditSettings& s, const PixelGrid& grid,
                                   StageId stop_after = prism::gui::final_stage, int threads = 0) {
    EXPECT_EQ(prism::process(picture,
                             prism::gui::to_process_settings(s, stop_after, std::nullopt, grid),
                             threads),
              prism::ProcessResult::completed);
    return prism::encode_srgb8(picture, threads);
}

// A Session on a 300 x 200 PNG with a 200 px preview (200 x 133), its look with strokes and,
// if given, a frame.
struct Opened {
    fs::path png;
    Session session{{.preview_long_side = 200, .threads = 0}};

    explicit Opened(const std::string& name, std::optional<Frame> frame = std::nullopt,
                    int w = 300, int h = 200)
        : png(write_png(name, w, h)) {
        EXPECT_EQ(session.open_file(png), std::nullopt);
        EditSettings s = look_with_strokes(w, h);
        s.frame = frame;
        EXPECT_EQ(session.set_settings(s), std::nullopt);
    }
    ~Opened() { fs::remove(png); }
    Opened(const Opened&) = delete;
    Opened& operator=(const Opened&) = delete;
};

PreviewFrame render(const Session& session, PreviewOptions options = {},
                    StageId stop_after = prism::gui::final_stage,
                    prism::gui::FieldCache* cache = nullptr) {
    options.framing = session.preview_framing();
    std::optional<PreviewFrame> frame =
        prism::gui::render_preview(*session.preview_base(), session.settings(), 0, {}, {},
                                   stop_after, options, cache);
    EXPECT_TRUE(frame.has_value());
    EXPECT_EQ(frame ? frame->error : std::string("none"), "");
    return frame ? std::move(*frame) : PreviewFrame{};
}

prism::gui::ExportSize long_side_size(int pixels) {
    prism::gui::ExportSize size;
    size.mode = prism::gui::ExportSize::Mode::long_side;
    size.long_side = pixels;
    return size;
}

prism::gui::ExportSize percent_size(int percent) {
    prism::gui::ExportSize size;
    size.mode = prism::gui::ExportSize::Mode::percent;
    size.percent = percent;
    return size;
}

}  // namespace

// --- The frame in the session, and a whole-photo one --------------------------------------------

TEST(FrameSettings, AWholePhotoFrameIsStoredAsNoneAndAddsNoUndoStep) {
    Opened o("in.png");
    const std::size_t steps = o.session.history().undo_steps();
    EditSettings s = o.session.settings();
    s.frame = Frame{};
    s.frame->aspect = prism::Aspect::square;  // a lock moves no pixel
    ASSERT_EQ(o.session.set_settings(s), std::nullopt);
    EXPECT_FALSE(o.session.settings().frame.has_value());
    EXPECT_EQ(o.session.history().undo_steps(), steps);
    EXPECT_EQ(o.session.preview_base(), o.session.whole_base());  // the very same image
    EXPECT_FALSE(o.session.preview_framing().has_value());
    EXPECT_EQ(o.session.framed_file_size(), (PixelSize{300, 200}));
    EXPECT_EQ(o.session.framed_start_size(), (PixelSize{300, 200}));
}

TEST(FrameSettings, AnInvalidFrameIsRefusedWithItsMessage) {
    Opened o("in.png");
    EditSettings s = o.session.settings();
    s.frame = Frame{50.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    const prism::gui::MaybeError error = o.session.set_settings(s);
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->kind, prism::gui::ErrorKind::invalid_settings);
    EXPECT_NE(error->details.find("Frame angle must be from -45 to 45"), std::string::npos)
        << error->details;
    EXPECT_FALSE(o.session.settings().frame.has_value());
}

// Every reader of the preview base, with a whole-photo frame left in the settings (not
// normalised): the same bits as with no frame at all, which is the path 0.5.0 took.
TEST(FrameSettings, ExportsWithoutAFrameOrWithAWholeOneAreThoseOfBefore) {
    const fs::path png = write_png("in.png", 301, 199);
    const fs::path out = temp_file("out.png");
    Session session({.preview_long_side = 64, .threads = 0});
    ASSERT_EQ(session.open_file(png), std::nullopt);
    ASSERT_EQ(session.set_settings(look_with_strokes(301, 199)), std::nullopt);
    for (const prism::gui::ExportSize& size :
         {prism::gui::ExportSize{}, long_side_size(101), percent_size(40)}) {
        session.set_export_size(size);
        Image expected = prism::load_image(png);
        if (const std::optional<int> side = session.export_long_side()) {
            expected = prism::downscale(expected, *side);
        }
        const std::vector<std::uint8_t> want =
            rendered(expected, session.settings(), PixelGrid::whole(expected.width, expected.height));
        for (const bool whole_frame : {false, true}) {
            prism::gui::ExportJob job = session.export_job();
            if (whole_frame) {
                job.settings.frame = Frame{};
            }
            ASSERT_EQ(prism::gui::export_image(job, out), std::nullopt);
            const Image written = prism::load_image(out);
            EXPECT_EQ(written.width, expected.width);
            EXPECT_EQ(written.height, expected.height);
            EXPECT_EQ(prism::encode_srgb8(written), want)
                << "mode " << static_cast<int>(size.mode) << (whole_frame ? ", whole frame" : "");
        }
    }
    fs::remove(png);
    fs::remove(out);
}

// --- The framed preview -------------------------------------------------------------------------

TEST(FramedPreview, TheFramedBaseIsTheWholeBaseFramedAndPickReadsIt) {
    const Frame frame = turned_frame(3.0f);
    Opened o("in.png", frame);
    const Image& whole = *o.session.whole_base();
    ASSERT_EQ(whole.width, 200);
    ASSERT_EQ(whole.height, 133);
    const Frame fitted = prism::fit_frame(frame, whole.width, whole.height);
    ASSERT_NE(fitted, frame) << "the frame must need fitting for this test";
    ASSERT_TRUE(o.session.preview_framing().has_value());
    EXPECT_EQ(*o.session.preview_framing(), (PictureFrame{fitted, 200, 133, 1.0f}));
    const Image expected = prism::apply_frame(whole, fitted);
    const Image& base = *o.session.preview_base();
    ASSERT_EQ(base.width, expected.width);
    ASSERT_EQ(base.height, expected.height);
    EXPECT_EQ(bits(base), bits(expected));
    EXPECT_LT(base.width, whole.width);
    // The Decode view shows the framed base.
    const PreviewFrame decode = render(o.session, {}, StageId::decode);
    EXPECT_EQ(decode.width, expected.width);
    EXPECT_EQ(decode.rgb8, prism::encode_srgb8(expected));
}

TEST(FramedPreview, TheRenderIsTheStagesOnTheFramedBaseWithTheFramedGrid) {
    for (const float angle : {3.0f, -2.0f, 0.0f}) {
        const Frame frame = turned_frame(angle);
        Opened o("in.png", frame);
        const Image& whole = *o.session.whole_base();
        const Frame fitted = prism::fit_frame(frame, whole.width, whole.height);
        const Image framed = prism::apply_frame(whole, fitted);
        const PixelGrid grid =
            PixelGrid::framed(fitted, whole.width, whole.height, 1.0f, framed.width, framed.height);
        const std::vector<std::uint8_t> want = rendered(framed, o.session.settings(), grid);

        PreviewOptions options;
        options.clip_report = true;
        prism::gui::FieldCache cache;
        const PreviewFrame without_cache = render(o.session, options);
        const PreviewFrame with_cache = render(o.session, options, prism::gui::final_stage, &cache);
        EXPECT_EQ(without_cache.rgb8, want) << angle;
        EXPECT_EQ(with_cache.rgb8, want) << angle;
        EXPECT_EQ(cache.grid().framing.has_value(), true);
        // The strokes moved the picture: the same stages with the whole photo's grid differ.
        EXPECT_NE(rendered(framed, o.session.settings(),
                           prism::gui::stage_grid(framed.width, framed.height)),
                  want);
        // The clip report measures the framed picture.
        ASSERT_TRUE(with_cache.clips.has_value());
        EXPECT_EQ(*with_cache.clips,
                  prism::gui::compute_clip_report(*o.session.preview_base(), o.session.settings(),
                                                  0, std::nullopt, o.session.preview_framing()));
        EXPECT_EQ(*with_cache.clips,
                  prism::gui::compute_clip_report(framed, o.session.settings(), 0, std::nullopt,
                                                  PictureFrame{fitted, whole.width, whole.height,
                                                               1.0f}));
    }
}

TEST(FramedPreview, AFramingThatDoesNotFitTheSettingsIsRefused) {
    Opened o("in.png", turned_frame(3.0f));
    const Image& base = *o.session.preview_base();
    // The framed base handed over as if it were the whole photo.
    EXPECT_THROW((void)prism::gui::render_preview(base, o.session.settings(), 0, {}),
                 std::invalid_argument);
    // The whole base with the framing of the frame.
    PreviewOptions options;
    options.framing = o.session.preview_framing();
    EXPECT_THROW((void)prism::gui::render_preview(*o.session.whole_base(), o.session.settings(), 0,
                                                  {}, {}, prism::gui::final_stage, options),
                 std::invalid_argument);
    EXPECT_THROW((void)prism::gui::sample_trace(base, o.session.settings(), 1, 1),
                 std::invalid_argument);
}

// --- The framed export --------------------------------------------------------------------------

// The sheet's numbers are the file written, and its pixels are the decode framed (full resolution),
// then reduced, then the stages with the strokes on the framed grid with the reduction as its scale.
TEST(FramedExport, SizesMeasureTheFrameAndPixelsAreFramedThenReducedThenEdited) {
    const Frame frame = turned_frame(2.5f);
    const fs::path out = temp_file("out.png");
    Opened o("in.png", frame, 301, 199);
    const Frame fitted = prism::fit_frame(frame, 301, 199);
    const PixelSize framed = prism::framed_size(fitted, 301, 199);
    EXPECT_EQ(o.session.framed_file_size(), framed);
    EXPECT_EQ(o.session.framed_start_size(), framed);
    for (const prism::gui::ExportSize& size :
         {prism::gui::ExportSize{}, long_side_size(101), percent_size(40)}) {
        o.session.set_export_size(size);
        const prism::gui::ExportPlan plan = o.session.export_plan();
        EXPECT_EQ(plan.start, framed);
        if (size.mode == prism::gui::ExportSize::Mode::percent) {
            ASSERT_TRUE(plan.long_side.has_value());
            EXPECT_EQ(*plan.long_side,
                      prism::long_side_for_percent(framed.width, framed.height, 40));
        }
        ASSERT_EQ(prism::gui::export_image(o.session.export_job(), out), std::nullopt);
        const Image written = prism::load_image(out);
        EXPECT_EQ((PixelSize{written.width, written.height}), plan.size);

        prism::FramedExport expected = prism::frame_and_reduce(
            prism::load_image(o.png), o.session.settings().frame, plan.long_side);
        ASSERT_TRUE(expected.frame.fitted.has_value());
        const Image& img = expected.image;
        const float scale = static_cast<float>(std::max(framed.width, framed.height)) /
                            static_cast<float>(std::max(img.width, img.height));
        const PixelGrid grid = PixelGrid::framed(*expected.frame.fitted, 301, 199, scale,
                                                 img.width, img.height);
        EXPECT_EQ(prism::encode_srgb8(written), rendered(img, o.session.settings(), grid))
            << "mode " << static_cast<int>(size.mode);
        EXPECT_EQ(expected.reduced, plan.reduced());
    }
    fs::remove(out);
}

// RAW, with half-size decoding on and off: the sheet's sizes (estimated or not) are the file's.
TEST(FramedExport, RawSizesMeasureTheFrameWithAndWithoutHalfSize) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "needs LibRaw";
    }
    const fs::path dng = temp_file("in.dng");
    {
        const std::vector<std::byte> bytes = prism_test::synthetic_dng(16384);
        std::ofstream file(dng, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    const fs::path out = temp_file("out.png");
    Session session;
    ASSERT_EQ(session.open_file(dng), std::nullopt);
    for (const bool half : {false, true}) {
        EditSettings s = session.settings();
        s.raw_half_size = half;
        s.frame = turned_frame(-4.0f);
        ASSERT_EQ(session.set_settings(s), std::nullopt);
        for (const prism::gui::ExportSize& size :
             {prism::gui::ExportSize{}, long_side_size(9), percent_size(50)}) {
            session.set_export_size(size);
            const prism::gui::ExportPlan plan = session.export_plan();
            ASSERT_EQ(prism::gui::export_image(session.export_job(), out), std::nullopt);
            const Image written = prism::load_image(out);
            EXPECT_EQ((PixelSize{written.width, written.height}), plan.size)
                << (half ? "half size, " : "full size, ") << static_cast<int>(size.mode);
            const PixelSize start = session.export_start_size();
            EXPECT_EQ(plan.start, prism::framed_size(prism::fit_frame(*s.frame, start.width,
                                                                      start.height),
                                                     start.width, start.height));
        }
    }
    fs::remove(dng);
    fs::remove(out);
}

// --- Sample and Pick on the framed picture ------------------------------------------------------

TEST(FramedSample, SampleAndPickReadTheFramedRender) {
    Opened o("in.png", turned_frame(-3.5f));
    const Image& base = *o.session.preview_base();
    const PreviewFrame frame = render(o.session);
    for (const auto& [x, y] : {std::pair{10, 12}, std::pair{base.width / 2, base.height / 2},
                               std::pair{base.width - 3, base.height - 2}}) {
        const auto trace = prism::gui::sample_trace(base, o.session.settings(), x, y, std::nullopt,
                                                    o.session.preview_framing());
        ASSERT_TRUE(trace.has_value());
        const std::size_t i = (static_cast<std::size_t>(y) * base.width + x) * 3;
        EXPECT_EQ(trace->srgb8[0], frame.rgb8[i]) << x << ", " << y;
        EXPECT_EQ(trace->srgb8[1], frame.rgb8[i + 1]);
        EXPECT_EQ(trace->srgb8[2], frame.rgb8[i + 2]);
        // The Decode row is the framed base's own pixel, which Pick averages around.
        EXPECT_EQ(trace->at(StageId::decode).rgb[0], base.pixels[i]);
    }
    // The cache passes the framing on.
    prism::gui::SampleCache cache;
    const prism::gui::PixelTrace* cached =
        cache.read(o.session.preview_base(), o.session.settings(), std::nullopt,
                   prism::gui::final_stage, std::nullopt, 10, 12, std::nullopt,
                   o.session.preview_framing());
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->srgb8[0], frame.rgb8[(12u * base.width + 10u) * 3u]);

    // Pick: the patch is the framed base's (Session::preview_base), the picture on screen.
    const int x = base.width / 3;
    const int y = base.height / 3;
    const std::optional<prism::gui::PickPatch> patch = prism::gui::pick_patch(base, x, y);
    ASSERT_TRUE(patch.has_value());
    const PreviewFrame decode = render(o.session, {}, StageId::decode);
    ASSERT_EQ(decode.rgb8, prism::encode_srgb8(base));
    const std::optional<prism::gui::PickPatch> whole_patch =
        prism::gui::pick_patch(*o.session.whole_base(), x, y);
    ASSERT_TRUE(whole_patch.has_value());
    EXPECT_NE(patch->mean, whole_patch->mean) << "the framed picture is another part of the photo";
    // A pixel inside the whole base but past the framed picture's edge is off the picture.
    EXPECT_EQ(prism::gui::pick_neutral(base, base.width + 2, 5, std::nullopt).outcome,
              prism::gui::PickOutcome::unavailable);
    ASSERT_LT(base.width + 2, o.session.whole_base()->width);
}

// --- The test strip on a framed grid ------------------------------------------------------------

TEST(FramedStrip, EachBandIsTheFullFramedRenderAtItsExposure) {
    Opened o("in.png", turned_frame(2.0f));
    const Image& base = *o.session.preview_base();
    PreviewOptions options;
    options.test_strip = prism::gui::TestStrip{};
    prism::gui::FieldCache cache;
    for (prism::gui::FieldCache* c : {static_cast<prism::gui::FieldCache*>(nullptr), &cache}) {
        const PreviewFrame strip = render(o.session, options, prism::gui::final_stage, c);
        const std::vector<prism::gui::StripRect> bands =
            prism::gui::strip_bands(base.width, base.height, options.test_strip->count);
        for (std::size_t b = 0; b < bands.size(); ++b) {
            const EditSettings band = prism::gui::strip_band_settings(
                o.session.settings(), options.test_strip->step, options.test_strip->count,
                static_cast<int>(b));
            const auto full = prism::gui::render_preview(base, band, 0, {}, {},
                                                         prism::gui::final_stage,
                                                         [&] {
                                                             PreviewOptions p;
                                                             p.framing = o.session.preview_framing();
                                                             return p;
                                                         }());
            ASSERT_TRUE(full.has_value());
            const prism::gui::StripRect& r = bands[b];
            for (int y = r.y; y < r.y + r.height; ++y) {
                const std::size_t from = (static_cast<std::size_t>(y) * base.width + r.x) * 3;
                ASSERT_TRUE(std::equal(full->rgb8.begin() + static_cast<std::ptrdiff_t>(from),
                                       full->rgb8.begin() +
                                           static_cast<std::ptrdiff_t>(from + r.width * 3u),
                                       strip.rgb8.begin() + static_cast<std::ptrdiff_t>(from)))
                    << "band " << b << ", row " << y << (c ? ", cached" : "");
            }
        }
    }
}

// --- Recipes and undo ---------------------------------------------------------------------------

TEST(FrameRecipes, ARecipeNeitherSavesNorChangesTheFrame) {
    Opened o("in.png", turned_frame(1.5f));
    const std::optional<Frame> frame = o.session.settings().frame;
    ASSERT_TRUE(frame.has_value());
    EditSettings without = o.session.settings();
    without.frame.reset();
    const std::string text = prism::write_recipe(prism::gui::recipe_from(o.session.settings()));
    EXPECT_EQ(text, prism::write_recipe(prism::gui::recipe_from(without)));
    EXPECT_EQ(prism::recipe_format_version, 2);
    EXPECT_EQ(text.find("frame"), std::string::npos);
    EXPECT_EQ(text.find("angle"), std::string::npos);

    // Applying (or loading and applying) a recipe of another look keeps the frame and the base.
    EditSettings other;
    other.exposure_stops = -1.0f;
    other.contrast = 1.6f;
    const prism::Recipe recipe =
        prism::parse_recipe(prism::write_recipe(prism::gui::recipe_from(other)));
    const std::shared_ptr<const Image> base = o.session.preview_base();
    ASSERT_EQ(o.session.apply_recipe(recipe), std::nullopt);
    EXPECT_EQ(o.session.settings().exposure_stops, -1.0f);
    EXPECT_EQ(o.session.settings().frame, frame);
    EXPECT_EQ(o.session.preview_base(), base) << "the same frame needs no new framed base";
}

TEST(FrameRecipes, UndoAndRedoBringFrameChangesBackWithTheirBases) {
    Opened o("in.png");
    const std::shared_ptr<const Image> whole = o.session.preview_base();
    EditSettings s = o.session.settings();
    s.frame = turned_frame(2.0f);
    ASSERT_EQ(o.session.set_settings(s), std::nullopt);
    const std::shared_ptr<const Image> first = o.session.preview_base();
    s.frame = turned_frame(-1.0f);
    ASSERT_EQ(o.session.set_settings(s), std::nullopt);
    const std::vector<std::uint32_t> second = bits(*o.session.preview_base());

    ASSERT_EQ(o.session.undo(), std::nullopt);
    EXPECT_EQ(o.session.settings().frame, turned_frame(2.0f));
    EXPECT_EQ(bits(*o.session.preview_base()), bits(*first));
    ASSERT_EQ(o.session.undo(), std::nullopt);
    EXPECT_FALSE(o.session.settings().frame.has_value());
    EXPECT_EQ(o.session.preview_base(), whole);
    EXPECT_FALSE(o.session.preview_framing().has_value());
    ASSERT_EQ(o.session.redo(), std::nullopt);
    ASSERT_EQ(o.session.redo(), std::nullopt);
    EXPECT_EQ(o.session.settings().frame, turned_frame(-1.0f));
    EXPECT_EQ(bits(*o.session.preview_base()), second);
    EXPECT_EQ(o.session.preview_framing(),
              prism::gui::picture_frame_of(turned_frame(-1.0f), 200, 133));
}

// --- Threads ------------------------------------------------------------------------------------

// A framed export with strokes, large enough that the frame, the reduction and every stage split
// into chunks: 1 thread and 8 threads write the same bytes.
TEST(FramedExport, OneAndEightThreadsWriteTheSameFile) {
    constexpr int w = 2400;
    constexpr int h = 1600;
    const fs::path png = write_png("big.png", w, h);
    const fs::path out1 = temp_file("one.png");
    const fs::path out8 = temp_file("eight.png");
    Session session({.preview_long_side = 64, .threads = 0});
    ASSERT_EQ(session.open_file(png), std::nullopt);
    EditSettings s = look_with_strokes(w, h);
    Frame frame;
    frame.angle = 1.5f;
    frame.left = 0.04f;
    frame.top = 0.05f;
    frame.right = 0.97f;
    frame.bottom = 0.96f;
    frame.aspect = prism::Aspect::free;
    s.frame = frame;
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    session.set_export_size(long_side_size(1700));

    const double least = 1.5 * static_cast<double>(prism::parallel_min_pixels);
    const PixelSize framed = session.framed_start_size();
    const prism::gui::ExportPlan plan = session.export_plan();
    ASSERT_TRUE(plan.reduced());
    // The framed decode and the reduced image (what the stages run on) are both large enough to
    // split.
    ASSERT_GE(static_cast<double>(framed.width) * framed.height, least);
    ASSERT_GE(static_cast<double>(plan.size.width) * plan.size.height, least);
    const prism::FramedExport check =
        prism::frame_and_reduce(prism::load_image(png), s.frame, plan.long_side, 8);
    EXPECT_EQ(check.frame_threads, 8);
    EXPECT_EQ(check.reduce_threads, 8);

    prism::gui::ExportJob job = session.export_job();
    job.threads = 1;
    ASSERT_EQ(prism::gui::export_image(job, out1), std::nullopt);
    job.threads = 8;
    ASSERT_EQ(prism::gui::export_image(job, out8), std::nullopt);
    const Image one = prism::load_image(out1);
    const Image eight = prism::load_image(out8);
    EXPECT_EQ((PixelSize{one.width, one.height}), plan.size);
    EXPECT_EQ(prism::encode_srgb8(one), prism::encode_srgb8(eight));
    fs::remove(png);
    fs::remove(out1);
    fs::remove(out8);
}

// --- The brush on a framed canvas ---------------------------------------------------------------

namespace {

// Where the dab of a stroke painted at `at` on the framed canvas peaks in the framed render: the
// centroid of the stroke mask's dodge, in canvas units (pixel i's centre at i + 0.5).
prism::gui::ViewPoint painted_peak(Session& session, prism::gui::ViewPoint at) {
    const prism::gui::BrushCanvas canvas = prism::gui::brush_canvas(session);
    prism::gui::BrushStyle style;
    style.radius = 0.05f;
    style.softness = 1.0f;
    style.strength = 1.0f;
    prism::gui::BrushGesture gesture;
    prism::gui::StrokePainter painter;
    const float spacing = prism::min_point_spacing(style.radius, prism::gui::brush_long_side(canvas));
    for (const auto& action : gesture.press(at, {}, prism::gui::BrushTone::dodge, spacing, canvas)) {
        EXPECT_EQ(painter.apply(action, style, canvas, session), std::nullopt);
    }
    for (const auto& action : gesture.release(at)) {
        EXPECT_EQ(painter.apply(action, style, canvas, session), std::nullopt);
    }
    PreviewOptions options;
    options.stroke_mask = true;
    const PreviewFrame frame = render(session, options);
    double sum = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int byte = frame.stroke_mask[static_cast<std::size_t>(y) * frame.width + x];
            const double w = byte > 128 ? byte - 128 : 0;
            sum += w;
            sx += w * (x + 0.5);
            sy += w * (y + 0.5);
        }
    }
    EXPECT_GT(sum, 0.0);
    return {static_cast<float>(sx / sum), static_cast<float>(sy / sum)};
}

}  // namespace

TEST(FramedBrush, AStrokeLandsUnderThePointerOnATurnedCroppedCanvas) {
    for (const float angle : {4.0f, -4.0f}) {
        Opened o("in.png", turned_frame(angle));
        EditSettings s = o.session.settings();
        s.strokes = Strokes{};
        ASSERT_EQ(o.session.set_settings(s), std::nullopt);
        const Image& base = *o.session.preview_base();
        // Off the middle, and off the pixel grid, so that a map about the wrong centre, a missing
        // turn or a missing crop all move the dab.
        const prism::gui::ViewPoint at{static_cast<float>(base.width) * 0.62f + 0.3f,
                                       static_cast<float>(base.height) * 0.41f + 0.7f};
        const prism::gui::ViewPoint peak = painted_peak(o.session, at);
        EXPECT_NEAR(peak.x, at.x, 0.5f) << angle;
        EXPECT_NEAR(peak.y, at.y, 0.5f) << angle;
        // The point the stroke holds is the photo point under the pointer, and maps back to it.
        const prism::gui::BrushCanvas canvas = prism::gui::brush_canvas(o.session);
        const FieldPoint p = prism::gui::brush_point(at, canvas);
        const prism::gui::ViewPoint back = prism::gui::brush_canvas_point(p, canvas);
        EXPECT_NEAR(back.x, at.x, 1e-3f);
        EXPECT_NEAR(back.y, at.y, 1e-3f);
        ASSERT_FALSE(o.session.settings().strokes.empty());
        EXPECT_EQ(o.session.settings().strokes.field()->strokes[0]->points[0], p);
    }
}

TEST(FramedBrush, TheRingSpacingAndAspectAreTheWholePhotos) {
    Opened o("in.png", turned_frame(3.0f));
    const prism::gui::BrushCanvas canvas = prism::gui::brush_canvas(o.session);
    ASSERT_TRUE(canvas.framing.has_value());
    ASSERT_LT(canvas.width, 200);
    EXPECT_EQ(prism::gui::brush_photo_size(canvas), (PixelSize{200, 133}));
    EXPECT_EQ(prism::gui::brush_long_side(canvas), 200);
    // The ring: 0.05 of the whole photo's 200 px at the view's zoom, on a density-1 canvas,
    // whatever the framed picture's size.
    const prism::gui::ViewGeometry g{0.0f, 0.0f, 900.0f, 700.0f, 1.0f, canvas.width,
                                     canvas.height};
    const prism::gui::ViewState s = prism::gui::view_zoomed_to(prism::gui::ViewState{}, g, 3.0f);
    const prism::gui::BrushRing ring = prism::gui::brush_ring(s, g, canvas, 0.05f, 0.5f);
    const float zoom = prism::gui::view_zoom(s, g);
    EXPECT_FLOAT_EQ(ring.outer, 0.05f * 200.0f * zoom);
    EXPECT_FLOAT_EQ(ring.inner, 0.05f * 200.0f * zoom * 0.5f);
    // A first stroke sets the field's aspect from the whole photo.
    EditSettings none = o.session.settings();
    none.strokes = Strokes{};
    ASSERT_EQ(o.session.set_settings(none), std::nullopt);
    prism::gui::StrokePainter painter;
    const prism::gui::BrushStyle style;
    ASSERT_EQ(painter.apply({prism::gui::BrushActionKind::begin_stroke, prism::gui::BrushTone::dodge, {}},
                            style, canvas, o.session),
              std::nullopt);
    ASSERT_EQ(painter.apply({prism::gui::BrushActionKind::add_point, prism::gui::BrushTone::dodge,
                             {0.5f, 0.5f}},
                            style, canvas, o.session),
              std::nullopt);
    EXPECT_EQ(o.session.settings().strokes.field()->aspect, prism::aspect_of(200, 133));
    // The point spacing's one-pixel floor is a pixel of the whole photo's long side.
    EXPECT_FLOAT_EQ(prism::min_point_spacing(0.0001f, prism::gui::brush_long_side(canvas)),
                    1.0f / 200.0f);
}
