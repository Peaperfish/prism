#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
        for (const FilePixel p : pixels_of(plan.size)) {
            SCOPED_TRACE(std::to_string(p.x) + ", " + std::to_string(p.y));
            const NeutralPick want = pick_neutral(whole, p.x, p.y, as_shot);
            const NeutralPick got = pick_neutral_photo(photo, frame, p.x, p.y, as_shot);
            EXPECT_EQ(got.outcome, want.outcome);
            EXPECT_EQ(got.light.kelvin, want.light.kelvin);
            EXPECT_EQ(got.light.tint, want.light.tint);
            EXPECT_EQ(got.limited, want.limited);
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
              "A real pixel: one pixel of the photo at its full size, as it exports.");
    EXPECT_EQ(sample_photo_pixel_text(),
              "A real pixel: one pixel of the photo at its full size, as it exports.");
    // A preview pixel still says what it averages.
    scale.photo_pixel = false;
    EXPECT_EQ(
        sample_rows(*t, StageId::encode, scale).average.rfind("One preview pixel averages", 0), 0u);
}

// --- Which pixel, and from where
// ------------------------------------------------------------------

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
    const FileScale k = file_scale(full_7, preview_7);
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
    EXPECT_EQ(file_pixel_of(0, 0, preview_7, full_7),
              (FilePixel{static_cast<int>(0.5 * k.x), static_cast<int>(0.5 * k.y)}));
    EXPECT_EQ(file_pixel_of(2176, 1452, preview_7, full_7),
              (FilePixel{static_cast<int>(2176.5 * k.x), static_cast<int>(1452.5 * k.y)}));
}

TEST(DetailSampleSource, ThePhotoIsReadOnlyWhereItsCurrentPixelsAreDrawn) {
    DetailFramePlan plan;
    plan.draw = true;
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

// --- The brush
// --------------------------------------------------------------------------------------

TEST(DetailBrush, PointsAreAFilePixelApartWhileRealPixelsAreDrawn) {
    BrushCanvas canvas;
    canvas.width = 2560;
    canvas.height = 1707;
    const PixelSize full{9564, 6376};
    EXPECT_EQ(brush_spacing_long_side(canvas, false, full), 2560);
    EXPECT_EQ(brush_spacing_long_side(canvas, true, full), 9564);
    // A preview that is the file: the same either way.
    EXPECT_EQ(brush_spacing_long_side(canvas, true, {2560, 1707}), 2560);
    // A small brush (0.5% of the long side): its points are much closer at real pixels.
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
