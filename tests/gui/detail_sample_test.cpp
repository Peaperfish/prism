#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "env_var.hpp"
#include "prism/frame.hpp"
#include "prism/gui/brush.hpp"
#include "prism/gui/detail_canvas.hpp"
#include "prism/gui/neutral_pick.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/raw.hpp"
#include "prism/resize.hpp"
#include "prism/stroke_field.hpp"

// The tools on real pixels (#37, session 7): Sample and Pick read the photo's own pixel where real
// pixels are on the canvas, and that pixel is the export's; the brush lays its points a file pixel
// apart there, and a stroke's place on the photo never depends on the zoom.

namespace {

namespace fs = std::filesystem;
using prism::FieldPoint;
using prism::FieldStroke;
using prism::Frame;
using prism::FrameBox;
using prism::Image;
using prism::Photo;
using prism::PixelSize;
using prism::StrokeField;
using namespace prism::gui;

Photo make_photo(int w, int h) {
    Photo photo;
    photo.image.width = w;
    photo.image.height = h;
    photo.image.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = photo.image.pixels.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(x)) *
                           3;
            const float s = 0.03f + static_cast<float>((x * 7 + y * 13) % 101) / 120.0f;
            p[0] = s;
            p[1] = 0.6f * s + 0.02f * static_cast<float>(x % 5);
            p[2] = 0.2f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    photo.info.as_shot_white_xy = std::array<double, 2>{0.3457, 0.3585};
    return photo;
}

Strokes make_strokes(int w, int h) {
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.05f, 0.2f}, {0.5f, 0.55f}, {0.95f, 0.4f}}, 0.12f, 0.8f, -1.2f}));
    field->strokes.push_back(
        std::make_shared<const FieldStroke>(FieldStroke{{{0.45f, 0.6f}}, 0.2f, 1.0f, 1.5f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.0f, 0.5f}, {1.0f, 0.52f}}, 0.04f, 0.0f, 0.7f}));
    return Strokes(std::move(field));
}

Frame turned_7() {
    Frame f;
    f.angle = 7.0f;
    f.left = 0.04f;
    f.top = 0.06f;
    f.right = 0.97f;
    f.bottom = 0.95f;
    f.aspect = prism::Aspect::free;
    return f;
}

EditSettings look(int w, int h, bool black_and_white, bool framed) {
    EditSettings s;
    s.exposure_stops = 0.4f;
    s.strokes = make_strokes(w, h);
    s.white_balance = prism::WhiteBalance{5200.0, 8.0};
    s.saturation = black_and_white ? 0.0f : 1.3f;
    s.contrast = 1.15f;
    s.curve = CurveKind::filmic;
    if (framed) {
        s.frame = turned_7();
    }
    return s;
}

// Pixels to read: the corners, the middle, and some on the strokes' edges and the frame's.
std::vector<FilePixel> pixels_of(PixelSize size) {
    const int w = size.width;
    const int h = size.height;
    return {{0, 0},         {w - 1, 0},     {0, h - 1},      {w - 1, h - 1},
            {w / 2, h / 2}, {w / 3, h / 5}, {w / 20, h / 2}, {w * 19 / 20, h * 2 / 5},
            {w / 2, h - 2}, {1, h / 2}};
}

// The finished export at full framed size, and its 8-bit encode, for `photo` and `s`.
struct Exported {
    Image linear;
    std::vector<std::uint8_t> srgb8;
};
Exported exported(Photo photo, const EditSettings& s) {
    std::variant<Image, Error> result = finish_export(std::move(photo), s, std::nullopt, 0);
    Exported out;
    if (const Error* e = std::get_if<Error>(&result)) {
        ADD_FAILURE() << e->message;
        return out;
    }
    out.linear = std::move(std::get<Image>(result));
    out.srgb8 = prism::encode_srgb8(out.linear, 0);
    return out;
}

// Every sampled pixel's Output encode values and 8-bit codes against the export's, exactly.
void expect_export_pixels(const std::vector<std::pair<FilePixel, PixelTrace>>& traces,
                          const Exported& ex) {
    for (const auto& [p, trace] : traces) {
        SCOPED_TRACE("pixel " + std::to_string(p.x) + ", " + std::to_string(p.y));
        const std::size_t i =
            (static_cast<std::size_t>(p.y) * static_cast<std::size_t>(ex.linear.width) +
             static_cast<std::size_t>(p.x)) *
            3;
        for (std::size_t c = 0; c < 3; ++c) {
            // Bit for bit: the same float, not merely a close one.
            EXPECT_EQ(trace.at(StageId::encode).rgb[c], ex.linear.pixels[i + c]) << c;
            EXPECT_EQ(trace.srgb8[c], ex.srgb8[i + c]) << c;
        }
    }
}

TEST(DetailSample, APhotoPixelIsTheExportsPixel) {
    const int w = 640;
    const int h = 430;
    for (const bool framed : {true, false}) {
        for (const bool bw : {false, true}) {
            SCOPED_TRACE(std::string(framed ? "turned 7 degrees" : "unframed") +
                         (bw ? ", black and white" : ", colour"));
            const Photo photo = make_photo(w, h);
            const EditSettings s = look(w, h, bw, framed);
            const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
            std::vector<std::pair<FilePixel, PixelTrace>> traces;
            for (const FilePixel p : pixels_of(size)) {
                const std::optional<PixelTrace> t =
                    sample_photo_trace(photo, s, p.x, p.y, as_shot_white_of(photo.info));
                ASSERT_TRUE(t);
                EXPECT_EQ(t->x, p.x);
                EXPECT_EQ(t->y, p.y);
                traces.emplace_back(p, *t);
            }
            expect_export_pixels(traces, exported(photo, s));
        }
    }
}

TEST(DetailSample, APhotoWhosePixelsDontMatchItsSizeIsRefused) {
    Photo photo = make_photo(20, 10);
    photo.image.pixels.pop_back();
    EXPECT_THROW((void)sample_photo_trace(photo, EditSettings{}, 1, 1), std::invalid_argument);
}

TEST(DetailSample, APixelOffTheFramedPictureReadsNothing) {
    const Photo photo = make_photo(200, 150);
    const EditSettings s = look(200, 150, false, true);
    const PixelSize size = prism::plan_export_frame(s.frame, 200, 150).size;
    EXPECT_FALSE(sample_photo_trace(photo, s, -1, 0));
    EXPECT_FALSE(sample_photo_trace(photo, s, size.width, 0));
    EXPECT_FALSE(sample_photo_trace(photo, s, 0, size.height));
    EXPECT_TRUE(sample_photo_trace(photo, s, size.width - 1, size.height - 1));
}

TEST(DetailSample, PixelsOfTheRawFileAreTheExportsPixels) {
    const std::string raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "Prism was built without LibRaw";
    }
    std::variant<Photo, Error> decoded =
        decode_for_export(fs::path(raw), InputKind::raw, EditSettings{}, 0);
    if (const Error* e = std::get_if<Error>(&decoded)) {
        FAIL() << e->message << " " << e->details;
    }
    Photo& photo = std::get<Photo>(decoded);
    const int w = photo.image.width;
    const int h = photo.image.height;
    const EditSettings s = look(w, h, true, true);  // turned 7 degrees, strokes, black and white
    const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
    std::vector<std::pair<FilePixel, PixelTrace>> traces;
    for (const FilePixel p : pixels_of(size)) {
        const std::optional<PixelTrace> t =
            sample_photo_trace(photo, s, p.x, p.y, as_shot_white_of(photo.info));
        ASSERT_TRUE(t);
        traces.emplace_back(p, *t);
    }
    expect_export_pixels(traces, exported(std::move(photo), s));
}

TEST(DetailSample, APhotoPickIsThePickOnTheFramedFullSizePicture) {
    const int w = 320;
    const int h = 220;
    const Photo photo = make_photo(w, h);
    for (const bool framed : {true, false}) {
        SCOPED_TRACE(framed ? "turned" : "unframed");
        const std::optional<Frame> frame = framed ? std::optional<Frame>(turned_7()) : std::nullopt;
        const prism::ExportFrame plan = prism::plan_export_frame(frame, w, h);
        const Image whole =
            plan.fitted ? prism::apply_frame(photo.image, *plan.fitted, 1) : photo.image;
        ASSERT_EQ(whole.width, plan.size.width);
        const std::optional<prism::Chromaticity> as_shot = as_shot_white_of(photo.info);
        for (const int size : {5, 11, 19}) {
            for (const FilePixel p : pixels_of(plan.size)) {
                SCOPED_TRACE(std::to_string(size) + ": " + std::to_string(p.x) + ", " +
                             std::to_string(p.y));
                const NeutralPick want = pick_neutral(whole, p.x, p.y, as_shot, size);
                const NeutralPick got = pick_neutral_photo(photo, frame, p.x, p.y, as_shot, size);
                EXPECT_EQ(got.outcome, want.outcome);
                EXPECT_EQ(got.light.kelvin, want.light.kelvin);
                EXPECT_EQ(got.light.tint, want.light.tint);
                EXPECT_EQ(got.limited, want.limited);
            }
        }
        EXPECT_EQ(pick_neutral_photo(photo, frame, plan.size.width, 0, as_shot).outcome,
                  PickOutcome::unavailable);
    }
}

TEST(DetailSample, TheReadoutSaysItIsARealPixel) {
    const Photo photo = make_photo(64, 48);
    const std::optional<PixelTrace> t = sample_photo_trace(photo, EditSettings{}, 10, 10);
    ASSERT_TRUE(t);
    SampleScale scale{2560, 9564, false, true};
    EXPECT_EQ(sample_rows(*t, StageId::encode, scale).average,
              "A real pixel: one pixel of the photo at its full size.");
    EXPECT_EQ(sample_photo_pixel_text(), "A real pixel: one pixel of the photo at its full size.");
    // A preview pixel still says what it averages.
    scale.photo_pixel = false;
    EXPECT_EQ(
        sample_rows(*t, StageId::encode, scale).average.rfind("One preview pixel averages", 0), 0u);
}

TEST(DetailSample, APhotoPixelsTraceIsKeptUntilSomethingItDependsOnChanges) {
    auto photo = std::make_shared<const Photo>(make_photo(64, 48));
    PhotoSampleCache cache;
    EditSettings s;
    const PixelTrace* first = cache.read(photo, s, 10, 12);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->x, 10);
    EXPECT_EQ(cache.computes(), 1u);
    (void)cache.read(photo, s, 10, 12);  // nothing changed
    EXPECT_EQ(cache.computes(), 1u);
    (void)cache.read(photo, s, 11, 12);  // another pixel
    EXPECT_EQ(cache.computes(), 2u);
    s.exposure_stops = 1.0f;
    (void)cache.read(photo, s, 11, 12);  // other settings
    EXPECT_EQ(cache.computes(), 3u);
    auto other = std::make_shared<const Photo>(make_photo(64, 48));
    (void)cache.read(other, s, 11, 12);  // another photo
    EXPECT_EQ(cache.computes(), 4u);
    EXPECT_EQ(cache.read(other, s, 64, 0), nullptr);  // off the picture
    cache.clear();
    EXPECT_EQ(cache.current(), nullptr);
    EXPECT_EQ(other.use_count(), 1);  // let go
}

// --- Which pixel, and from where -----------------------------------------------------------------

constexpr PixelSize full_7{8138, 5424};
constexpr PixelSize preview_7{2177, 1453};

ViewGeometry canvas_7() {
    ViewGeometry g;
    g.x = 20.0f;
    g.y = 40.0f;
    g.width = 1500.0f;
    g.height = 1000.0f;
    g.density = 2.0f;
    g.image_width = preview_7.width;
    g.image_height = preview_7.height;
    g.file_scale = static_cast<float>(file_scale(full_7, preview_7).shown);
    return g;
}

ViewState at_percent(const ViewGeometry& g, double percent, float cx, float cy) {
    ViewState s;
    s.fit = false;
    s.zoom = static_cast<float>(percent / 100.0 * g.file_scale);
    s.centre_x = cx;
    s.centre_y = cy;
    return view_settled(s, g);
}

TEST(DetailSampleSource, TheFilePixelUnderTheCursorIsFoundAlongEachAxis) {
    const ViewGeometry g = canvas_7();
    for (const double percent : {100.0, 400.0, 800.0}) {
        SCOPED_TRACE(percent);
        const ViewState s = at_percent(g, percent, 2177.0f, 1453.0f);  // the bottom right corner
        // The middle of a file pixel's square on the screen is that pixel, near the far corner
        // where one scale for both axes would be several pixels off.
        for (const FilePixel p :
             {FilePixel{8000, 5300}, FilePixel{8137, 5423}, FilePixel{7901, 5377}}) {
            const SampleMarker m = file_pixel_marker(s, g, full_7, p);
            ASSERT_TRUE(m.on_canvas)
                << p.x << ", " << p.y << " at " << m.centre.x << ", " << m.centre.y;
            EXPECT_EQ(file_pixel_at(s, g, full_7, m.centre), p);
        }
    }
    // Off the picture: nothing.
    const ViewState s = at_percent(g, 100.0, 2100.0f, 1400.0f);
    const ViewPlacement p = view_placement(s, g);
    EXPECT_FALSE(file_pixel_at(s, g, full_7, {p.at.x + p.shown.x + 5.0f, p.at.y + 5.0f}));
    EXPECT_FALSE(file_pixel_at(s, g, full_7, {g.x - 1.0f, g.y + 10.0f}));
    // A preview pixel's middle, in file pixels.
    // The scale is 8138 / 2177 = 3.738 across and 5424 / 1453 = 3.733 down, so the middle of the
    // first pixel is at 1.87 and 1.87, and of the last at 8135.9 and 5422.0.
    EXPECT_EQ(file_pixel_of(0, 0, preview_7, full_7), (FilePixel{1, 1}));
    EXPECT_EQ(file_pixel_of(2176, 1452, preview_7, full_7), (FilePixel{8136, 5422}));
    // A pixel past either edge of the preview is held inside the photo: its middle maps to 8139.8
    // and 5426.6 (past the last photo pixel), or to -1.9 (before the first).
    EXPECT_EQ(file_pixel_of(2177, 1453, preview_7, full_7), (FilePixel{8137, 5423}));
    EXPECT_EQ(file_pixel_of(-1, -1, preview_7, full_7), (FilePixel{0, 0}));
}

TEST(DetailSampleSource, ThePhotoIsReadOnlyWhereItsCurrentPixelsAreDrawn) {
    DetailFramePlan plan;
    plan.draw = true;
    plan.whole_pixels = true;
    const ShownDetail shown{{3, 9}, {1000, 800, 512, 384}, full_7, std::nullopt};
    const FilePixel inside{1200, 900};
    EXPECT_EQ(sample_source(plan, shown, 3, 9, inside), PixelSource::photo);
    // The region's edges: its first pixel in, the one past its last out.
    EXPECT_EQ(sample_source(plan, shown, 3, 9, {1000, 800}), PixelSource::photo);
    EXPECT_EQ(sample_source(plan, shown, 3, 9, {1511, 1183}), PixelSource::photo);
    EXPECT_EQ(sample_source(plan, shown, 3, 9, {1512, 900}), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, shown, 3, 9, {1200, 1184}), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, shown, 3, 9, {999, 900}), PixelSource::preview);
    // Not drawn (Fit, a preview-only view, hidden), none shown, another base, older settings: the
    // preview's pixel, as in 0.6.0.
    DetailFramePlan hidden = plan;
    hidden.draw = false;
    EXPECT_EQ(sample_source(hidden, shown, 3, 9, inside), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, std::nullopt, 3, 9, inside), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, shown, 4, 9, inside), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, shown, 0, 9, inside), PixelSource::preview);
    EXPECT_EQ(sample_source(plan, shown, 3, 10, inside), PixelSource::preview);
    // Below 100% the frame drawn is shrunk: a device pixel shows several photo pixels averaged, so
    // the preview's pixel is read, with its line about what it averages.
    DetailFramePlan shrunk = plan;
    shrunk.whole_pixels = false;
    EXPECT_EQ(sample_source(shrunk, shown, 3, 9, inside), PixelSource::preview);
}

TEST(DetailSampleSource, ThePlanSaysWhenEachPhotoPixelIsDrawnWhole) {
    // A 60 MP photo on a 1500 x 1000 point canvas at 2x, real pixels shown.
    DetailFrameInput in;
    in.geometry = canvas_7();
    in.framed_preview_now = preview_7;
    in.framed_full = full_7;
    in.base_state = DetailBaseState::ready;
    in.base = 1;
    in.settings = 5;
    for (const auto& [percent, whole] : std::vector<std::pair<double, bool>>{
             {50.0, false}, {99.0, false}, {100.0, true}, {400.0, true}}) {
        SCOPED_TRACE(percent);
        in.view = at_percent(in.geometry, percent, 1088.0f, 726.0f);
        const DetailFramePlan first = plan_detail_frame(in);
        ASSERT_TRUE(first.submit);
        DetailFrameInput shown = in;
        shown.submitted = SubmittedDetail{first.submit->key,      first.submit->request.region,
                                          first.submit->output,   first.submit->picture,
                                          first.submit->admitted, first.submit->hold};
        shown.shown = ShownDetail{first.submit->key, first.submit->request.region,
                                  first.submit->picture, std::nullopt};
        const DetailFramePlan plan = plan_detail_frame(shown);
        ASSERT_TRUE(plan.draw);
        EXPECT_EQ(plan.whole_pixels, whole);
    }
}

TEST(DetailSampleSource, APixelOfAnEstimatedSizeMovesToTheRealOne) {
    // A pin taken on an estimated 8000 x 5333 picture; the photo read is 8138 x 5424.
    const FilePixel p = file_pixel_rescaled({7999, 2666}, {8000, 5333}, full_7);
    // The middles, 7999.5 and 2666.5, become 8137.4 and 2712.0 on the real size.
    EXPECT_EQ(p, (FilePixel{8137, 2712}));
    EXPECT_EQ(p.x, 8137);  // the last pixel stays the last
    // The same size, or a size that can't be used: unchanged.
    EXPECT_EQ(file_pixel_rescaled({5, 6}, full_7, full_7), (FilePixel{5, 6}));
    EXPECT_EQ(file_pixel_rescaled({5, 6}, {0, 0}, full_7), (FilePixel{5, 6}));
}

TEST(DetailSampleSource, PicksRingIsAroundThePhotoPixelsItAverages) {
    const ViewGeometry g = canvas_7();
    const ViewState s = at_percent(g, 800.0, 1000.0f, 700.0f);
    const int size = pick_footprint_size(file_scale(full_7, preview_7));
    ASSERT_EQ(size, 19);
    const PickMarker m = file_pick_marker(s, g, full_7, {3740, 2610}, size);
    ASSERT_TRUE(m.on_canvas);
    // 19 photo pixels across, eight device pixels each at 800%: 76 points on a 2x screen.
    EXPECT_NEAR(m.patch.width(), 76.0f, 0.51f);
    EXPECT_NEAR(m.patch.height(), 76.0f, 0.51f);
    const ViewRect patch = detail_screen_rect(s, g, full_7, FrameBox{3731, 2601, 19, 19});
    EXPECT_FLOAT_EQ(m.patch.min.x, patch.min.x);
    EXPECT_FLOAT_EQ(m.patch.min.y, patch.min.y);
    // Cut off at the picture's edge, as the patch is.
    const ViewState corner = at_percent(g, 800.0, 0.0f, 0.0f);
    const PickMarker edge = file_pick_marker(corner, g, full_7, {0, 1}, size);
    EXPECT_NEAR(edge.patch.width(), 40.0f, 0.51f);   // pixels 0 to 9
    EXPECT_NEAR(edge.patch.height(), 44.0f, 0.51f);  // pixels 0 to 10
    EXPECT_FALSE(file_pick_marker(corner, g, full_7, {8138, 0}, size).on_canvas);
    // The same ring is drawn at Fit: the same piece of the photo, smaller.
    const PickMarker fit = file_pick_marker(ViewState{}, g, full_7, {3740, 2610}, size);
    EXPECT_TRUE(fit.on_canvas);
    EXPECT_LT(fit.patch.width(), 12.0f);
    EXPECT_GT(fit.patch.width(), 1.0f);
}

// --- Pick's footprint: the same photo area at every zoom -----------------------------------------

TEST(DetailPickFootprint, SizeIsTheFivePixelPatchInPhotoPixelsAndOdd) {
    const auto size_for = [](double k) { return pick_footprint_size(FileScale{k, k, k}); };
    EXPECT_EQ(size_for(1.0), 5);   // the preview is the photo
    EXPECT_EQ(size_for(1.04), 5);  // 5.2 rounds to 5
    EXPECT_EQ(size_for(1.1), 7);   // 5.5 rounds to 6, even, so 7
    EXPECT_EQ(size_for(1.5), 9);   // 7.5 rounds to 8, even, so 9
    EXPECT_EQ(size_for(2.0), 11);  // 10 is even, so 11
    EXPECT_EQ(size_for(2.2), 11);  // 11 is odd
    EXPECT_EQ(size_for(3.0), 15);  // 15 is odd
    // Never below 5, whatever the scale (a preview larger than the photo, or no scale).
    EXPECT_EQ(size_for(0.4), 5);
    EXPECT_EQ(size_for(0.0), 5);
    EXPECT_EQ(size_for(std::numeric_limits<double>::quiet_NaN()), 5);
    EXPECT_EQ(size_for(std::numeric_limits<double>::infinity()), 5);
    for (double k = 1.0; k < 12.0; k += 0.037) {
        EXPECT_EQ(size_for(k) % 2, 1) << k;
        EXPECT_GE(size_for(k), 5) << k;
    }
}

TEST(DetailPickFootprint, ASevenR5PhotoOnTheRealPreviewIsNineteenAcross) {
    // The Sony A7R V's photos are 9564 pixels wide (6376 high, 3:2); the preview is made at the
    // session's own long side.
    const int preview_long_side = SessionOptions{}.preview_long_side;
    ASSERT_EQ(preview_long_side, 2560);
    const PixelSize full{9564, 6376};
    const PixelSize preview = prism::downscaled_size(full.width, full.height, preview_long_side);
    ASSERT_EQ(preview.width, 2560);
    const FileScale k = file_scale(full, preview);
    EXPECT_NEAR(k.shown, 9564.0 / 2560.0, 1e-12);
    // 5 x 3.7359 = 18.68: 19, which is odd.
    EXPECT_EQ(pick_footprint_size(k), 19);
}

TEST(DetailPickFootprint, IsCentredAndCutOffAtThePicturesEdges) {
    const PixelSize full{100, 80};
    EXPECT_EQ(pick_footprint(full, 19, {50, 40}), (FrameBox{41, 31, 19, 19}));
    EXPECT_EQ(pick_footprint(full, 5, {50, 40}), (FrameBox{48, 38, 5, 5}));
    EXPECT_EQ(pick_footprint(full, 1, {50, 40}), (FrameBox{50, 40, 1, 1}));
    // Its middle pixel is the centre: as many on each side.
    const std::optional<FrameBox> mid = pick_footprint(full, 19, {30, 20});
    ASSERT_TRUE(mid);
    EXPECT_EQ(30 - mid->x0, mid->x0 + mid->width - 1 - 30);
    EXPECT_EQ(20 - mid->y0, mid->y0 + mid->height - 1 - 20);
    // Cut off at each edge.
    EXPECT_EQ(pick_footprint(full, 19, {0, 0}), (FrameBox{0, 0, 10, 10}));
    EXPECT_EQ(pick_footprint(full, 19, {99, 79}), (FrameBox{90, 70, 10, 10}));
    EXPECT_EQ(pick_footprint(full, 19, {3, 79}), (FrameBox{0, 70, 13, 10}));
    EXPECT_EQ(pick_footprint(full, 19, {99, 5}), (FrameBox{90, 0, 10, 15}));
    // Off the picture, or no picture: nothing.
    EXPECT_FALSE(pick_footprint(full, 19, {100, 0}));
    EXPECT_FALSE(pick_footprint(full, 19, {0, -1}));
    EXPECT_FALSE(pick_footprint({0, 0}, 19, {0, 0}));
}

TEST(DetailPickFootprint, AFitAndA100PercentClickCoverTheSamePhotoRectangle) {
    const ViewGeometry g = canvas_7();
    // A point of the photo, in preview pixels, not on the edge of a photo pixel.
    const ViewPoint at_image{1000.37f, 700.61f};
    std::optional<PickFootprint> first;
    for (const double percent : {0.0, 100.0, 400.0, 800.0}) {
        SCOPED_TRACE(percent);
        const ViewState s = percent == 0.0 ? ViewState{}
                                           : at_percent(g, percent, 1000.0f, 700.0f);
        const ViewPoint screen = view_image_to_screen(s, g, at_image);
        const std::optional<PickFootprint> fp = pick_footprint_at(s, g, full_7, screen);
        ASSERT_TRUE(fp);
        EXPECT_EQ(fp->full, full_7);
        EXPECT_EQ(fp->size, 19);
        EXPECT_EQ(fp->box.width, 19);
        EXPECT_EQ(fp->box.height, 19);
        if (!first) {
            first = fp;
        }
        EXPECT_EQ(fp->centre, first->centre);
        EXPECT_EQ(fp->box, first->box);
    }
    ASSERT_TRUE(first);
    // The photo pixel under the point: 1000.37 x 3.738 = 3739.6, 700.61 x 3.733 = 2615.4.
    EXPECT_EQ(first->centre, (FilePixel{3739, 2615}));
}

TEST(DetailPickFootprint, ThePreviewAreaIsTheSameBoxInPreviewPixels) {
    const ViewGeometry g = canvas_7();
    const ViewPoint screen = view_image_to_screen(ViewState{}, g, {1000.37f, 700.61f});
    const std::optional<PickFootprint> fp = pick_footprint_at(ViewState{}, g, full_7, screen);
    ASSERT_TRUE(fp);
    const PatchArea area = pick_footprint_in_preview(fp->box, preview_7, full_7);
    const FileScale k = file_scale(full_7, preview_7);
    // Back to photo pixels, it is the box.
    EXPECT_NEAR(area.x0 * k.x, fp->box.x0, 1e-9);
    EXPECT_NEAR(area.y0 * k.y, fp->box.y0, 1e-9);
    EXPECT_NEAR(area.x1 * k.x, fp->box.x0 + fp->box.width, 1e-9);
    EXPECT_NEAR(area.y1 * k.y, fp->box.y0 + fp->box.height, 1e-9);
    // About 5 preview pixels across: the 5 x 5 patch, to the rounding of an odd size.
    EXPECT_NEAR(area.x1 - area.x0, 5.0, 0.35);
    EXPECT_NEAR(area.y1 - area.y0, 5.0, 0.35);
    // Cut off at the picture's edge: the area is too.
    const PatchArea corner = pick_footprint_in_preview({0, 0, 10, 10}, preview_7, full_7);
    EXPECT_EQ(corner.x0, 0.0);
    EXPECT_EQ(corner.y0, 0.0);
    // Empty sizes: an empty area.
    const PatchArea none = pick_footprint_in_preview({0, 0, 10, 10}, {0, 0}, full_7);
    EXPECT_EQ(none.x1, none.x0);
}

TEST(DetailPickFootprint, WithoutAFullSizePhotoItIsTheFiveByFivePatchOnThePreview) {
    const ViewGeometry g = canvas_7();
    const ViewPoint screen = view_image_to_screen(ViewState{}, g, {1000.37f, 700.61f});
    const std::optional<PickFootprint> fp = pick_footprint_at(ViewState{}, g, {0, 0}, screen);
    ASSERT_TRUE(fp);
    EXPECT_EQ(fp->full, preview_7);
    EXPECT_EQ(fp->size, 5);
    EXPECT_EQ(fp->box, (FrameBox{998, 698, 5, 5}));
    // Off the canvas: nothing.
    EXPECT_FALSE(pick_footprint_at(ViewState{}, g, full_7, {g.x - 5.0f, g.y + 10.0f}));
}

TEST(DetailSampleSource, AFilePixelsMarkerIsItsOwnSquare) {
    const ViewGeometry g = canvas_7();
    const ViewState s = at_percent(g, 800.0, 1000.0f, 700.0f);
    const SampleMarker m = file_pixel_marker(s, g, full_7, {3740, 2610});
    // Eight device pixels across at 800%: four points on a 2x screen.
    EXPECT_NEAR(m.square.width(), 4.0f, 0.51f);
    EXPECT_NEAR(m.square.height(), 4.0f, 0.51f);
    EXPECT_TRUE(m.shows_square);
    EXPECT_FALSE(file_pixel_marker(at_percent(g, 150.0, 1000.0f, 700.0f), g, full_7, {3740, 2610})
                     .shows_square);
}

// --- The brush -----------------------------------------------------------------------------------

TEST(DetailBrush, PointsAreAFilePixelApartWhileRealPixelsAreDrawn) {
    BrushCanvas canvas;
    canvas.width = 2560;
    canvas.height = 1707;
    const PixelSize full{9564, 6376};
    EXPECT_EQ(brush_spacing_long_side(canvas, false, full), 2560);
    EXPECT_EQ(brush_spacing_long_side(canvas, true, full), 9564);
    // A preview that is the file: the same either way.
    EXPECT_EQ(brush_spacing_long_side(canvas, true, {2560, 1707}), 2560);
    // With the smallest brush the UI allows, the 2560 px preview's pixel is already finer than the
    // radius's own spacing: nothing changes (the one-pixel floor only matters under ~1333 px).
    EXPECT_EQ(
        prism::min_point_spacing(brush_radius_min, brush_spacing_long_side(canvas, true, full)),
        prism::min_point_spacing(brush_radius_min, brush_spacing_long_side(canvas, false, full)));
    // A framed canvas: the whole photo's long side, in file pixels by the framed picture's scale.
    BrushCanvas framed;
    framed.width = 2177;
    framed.height = 1453;
    framed.framing = prism::gui::picture_frame_of(turned_7(), 2560, 1707);
    ASSERT_TRUE(framed.framing);
    const int whole_preview = brush_long_side(framed);
    EXPECT_EQ(brush_spacing_long_side(framed, false, full_7), whole_preview);
    EXPECT_EQ(brush_spacing_long_side(framed, true, full_7),
              static_cast<int>(std::round(whole_preview * file_scale(full_7, preview_7).shown)));
    // A brush smaller than the UI allows (0.05%): its points are closer at real pixels.
    EXPECT_LT(prism::min_point_spacing(0.0005f, brush_spacing_long_side(canvas, true, full)),
              prism::min_point_spacing(0.0005f, brush_spacing_long_side(canvas, false, full)));
}

// The stroke a drag along `path` (image coordinates) lays with `spacing`.
FieldStroke stroke_of(const std::vector<ViewPoint>& path, float spacing,
                      const BrushCanvas& canvas) {
    BrushGesture gesture;
    FieldStroke stroke;
    stroke.radius = 0.001f;
    stroke.softness = 0.5f;
    stroke.weight = 1.0f;
    const auto take = [&](const std::vector<BrushAction>& actions) {
        for (const BrushAction& a : actions) {
            if (a.kind == BrushActionKind::add_point) {
                stroke.points.push_back(a.point);
            }
        }
    };
    take(gesture.press(path.front(), {}, BrushTone::dodge, spacing, canvas));
    for (std::size_t i = 1; i + 1 < path.size(); ++i) {
        take(gesture.move(path[i]));
    }
    take(gesture.release(path.back()));
    return stroke;
}

TEST(DetailBrush, AStrokesPlaceOnThePhotoDoesNotDependOnTheZoom) {
    // The same straight drag across the picture, as painted at Fit (points a preview pixel apart
    // for this small brush) and at 100% (a file pixel apart): many more points, the same line.
    BrushCanvas canvas;
    canvas.width = 2560;
    canvas.height = 1707;
    const PixelSize full{9564, 6376};
    std::vector<ViewPoint> path;
    for (int i = 0; i <= 6000; ++i) {  // a third of a preview pixel at a time
        const float t = static_cast<float>(i) / 6000.0f;
        path.push_back({300.0f + 1500.0f * t, 400.0f + 700.0f * t});
    }
    // A small brush, whose spacing is the one-pixel floor: a preview pixel at Fit, a file pixel at
    // 100%.
    const float radius = 0.001f;
    const float fit_spacing =
        prism::min_point_spacing(radius, brush_spacing_long_side(canvas, false, full));
    const float spacing_100 =
        prism::min_point_spacing(radius, brush_spacing_long_side(canvas, true, full));
    const FieldStroke at_fit = stroke_of(path, fit_spacing, canvas);
    const FieldStroke at_100 = stroke_of(path, spacing_100, canvas);
    ASSERT_GT(at_100.points.size(), at_fit.points.size());
    // Each stroke's points are its own spacing apart (to within one step of the drag): the zoom
    // sets how close the points are, never where they are.
    const double step = std::hypot(1500.0 / 6000.0, 700.0 / 6000.0) / 2560.0;  // long-side units
    const auto spaced = [&](const FieldStroke& stroke, float spacing) {
        for (std::size_t i = 1; i + 1 < stroke.points.size(); ++i) {
            const double du = static_cast<double>(stroke.points[i].u) - stroke.points[i - 1].u;
            const double dv = (static_cast<double>(stroke.points[i].v) - stroke.points[i - 1].v) /
                              static_cast<double>(prism::aspect_of(full.width, full.height));
            const double d = std::hypot(du, dv);
            if (d < spacing * 0.999 || d > spacing + step * 1.01) {
                ADD_FAILURE() << "point " << i << " is " << d << " from the last, spacing "
                              << spacing;
                return;
            }
        }
    };
    spaced(at_fit, fit_spacing);
    spaced(at_100, spacing_100);
    // Each point is where the cursor was on the photo, whatever the spacing: the first and last
    // are the same, and every point lies on the line.
    EXPECT_EQ(at_fit.points.front(), at_100.points.front());
    EXPECT_EQ(at_fit.points.back(), at_100.points.back());
    const FieldPoint a = brush_point(path.front(), canvas);
    const FieldPoint b = brush_point(path.back(), canvas);
    for (const FieldPoint& p : at_100.points) {
        const double cross = (static_cast<double>(b.u) - a.u) * (static_cast<double>(p.v) - a.v) -
                             (static_cast<double>(b.v) - a.v) * (static_cast<double>(p.u) - a.u);
        EXPECT_NEAR(cross, 0.0, 1e-6);
    }
    // The fields over the whole photo, at full size along a band across the line: the same light,
    // to within float rounding of the distances.
    StrokeField fit_field;
    fit_field.aspect = prism::aspect_of(full.width, full.height);
    StrokeField hundred_field = fit_field;
    fit_field.strokes.push_back(std::make_shared<const FieldStroke>(at_fit));
    hundred_field.strokes.push_back(std::make_shared<const FieldStroke>(at_100));
    const prism::PixelGrid grid = stage_grid(full.width, full.height, 0, 1500, 1, 0, 400);
    std::vector<float> f1(static_cast<std::size_t>(full.width) * 400);
    std::vector<float> f2(f1.size());
    prism::rasterise_field(fit_field, grid, 0, 400, f1);
    prism::rasterise_field(hundred_field, grid, 0, 400, f2);
    float worst = 0.0f;
    bool reached = false;
    for (std::size_t i = 0; i < f1.size(); ++i) {
        worst = std::max(worst, std::fabs(f1[i] - f2[i]));
        reached = reached || f1[i] > 0.5f;
    }
    EXPECT_TRUE(reached);  // the band crosses the stroke
    EXPECT_LT(worst, 1e-4f);
}

}  // namespace
