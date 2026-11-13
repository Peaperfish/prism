#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/gui/clipping.hpp"
#include "prism/gui/history.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"
#include "prism/stroke_field.hpp"

// Dodge & burn as a stage of the app: where it sits in the stage list, and that every place that
// renders hands it the grid of the image it receives, so the strokes land on the same pixels in the
// preview, the clip report, the sampler, a test strip band and an export.

namespace fs = std::filesystem;

namespace {

using prism::FieldPoint;
using prism::FieldStroke;
using prism::Image;
using prism::StrokeField;
using prism::gui::ClipReport;
using prism::gui::EditSettings;
using prism::gui::PreviewFrame;
using prism::gui::PreviewOptions;
using prism::gui::StageId;
using prism::gui::Strokes;

// A w x h image of varied colours up to well above white, so every stage changes most pixels and
// a stroke can push pixels over white.
Image make_image(int w, int h) {
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.pixels.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            const float s = 0.02f + static_cast<float>((x * 7 + y * 13) % 101) / 110.0f;  // to 0.94
            p[0] = s;
            p[1] = 0.6f * s + 0.01f * static_cast<float>(x % 5);
            p[2] = 0.3f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    return img;
}

// Three strokes on a w x h photo: a long soft burn across it, a soft dodge dab, and a hard-edged
// dodge along a row that runs across the tiles the stage works in.
Strokes make_strokes(int w, int h) {
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    const auto add = [&](std::vector<FieldPoint> points, float radius, float softness,
                         float weight) {
        field->strokes.push_back(std::make_shared<const FieldStroke>(
            FieldStroke{std::move(points), radius, softness, weight}));
    };
    add({{0.1f, 0.15f}, {0.4f, 0.5f}, {0.85f, 0.8f}}, 0.06f, 0.6f, -1.0f);
    add({{0.7f, 0.2f}}, 0.12f, 0.3f, 0.8f);
    add({{0.05f, 0.6f}, {0.95f, 0.62f}}, 0.03f, 0.0f, 1.5f);
    return Strokes(std::move(field));
}

EditSettings edited(const Strokes& strokes) {
    EditSettings s;
    s.exposure_stops = 0.3f;
    s.saturation = 1.2f;
    s.contrast = 1.1f;
    s.curve = prism::gui::CurveKind::filmic;
    s.strokes = strokes;
    return s;
}

std::vector<std::uint32_t> bits_of(const Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

std::array<std::uint32_t, 3> pixel_bits(const Image& img, int x, int y) {
    const float* p = img.pixels.data() + (static_cast<std::size_t>(y) * img.width + x) * 3;
    return {std::bit_cast<std::uint32_t>(p[0]), std::bit_cast<std::uint32_t>(p[1]),
            std::bit_cast<std::uint32_t>(p[2])};
}

std::array<std::uint8_t, 3> pixel_bytes(const PreviewFrame& f, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * f.width + x) * 3;
    return {f.rgb8[i], f.rgb8[i + 1], f.rgb8[i + 2]};
}

PreviewFrame preview(const Image& base, const EditSettings& s, StageId stage,
                     PreviewOptions options = {}, int threads = 0) {
    std::optional<PreviewFrame> frame =
        prism::gui::render_preview(base, s, threads, {}, {}, stage, options);
    EXPECT_TRUE(frame.has_value());
    EXPECT_TRUE(frame && frame->error.empty()) << (frame ? frame->error : "");
    return frame ? std::move(*frame) : PreviewFrame{};
}

Image stage_image(const Image& base, const EditSettings& s, StageId stage, int threads = 0) {
    std::optional<Image> img = prism::gui::render_stage_image(base, s, stage, threads);
    EXPECT_TRUE(img.has_value());
    return img ? std::move(*img) : Image{};
}

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_dodge_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

std::string file_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

// --- The stage in the stage list ---------------------------------------------------------------

TEST(DodgeBurnStage, ItsSwitchIsItsOwn) {
    EditSettings s;
    EXPECT_TRUE(prism::gui::stage_enabled(s, StageId::dodge_burn));
    prism::gui::set_stage_enabled(s, StageId::dodge_burn, false);
    EXPECT_FALSE(s.dodge_burn_on);
    EXPECT_FALSE(prism::gui::stage_enabled(s, StageId::dodge_burn));
    EXPECT_TRUE(s.exposure_on);
    prism::gui::set_stage_enabled(s, StageId::dodge_burn, true);
    EXPECT_TRUE(s.dodge_burn_on);
}

TEST(DodgeBurnStage, StrokesCompareByPointerThenByContent) {
    const Strokes none;
    const Strokes empty{std::make_shared<StrokeField>()};
    EXPECT_TRUE(none.empty());
    EXPECT_TRUE(empty.empty());
    EXPECT_EQ(none, empty);  // no strokes is no strokes, however it is held

    const Strokes a = make_strokes(300, 200);
    const Strokes same = a;
    const Strokes twin = make_strokes(300, 200);  // other objects, same content
    const Strokes other = make_strokes(300, 100);  // another aspect
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, same);
    EXPECT_EQ(a.field(), same.field());
    EXPECT_NE(a.field(), twin.field());
    EXPECT_EQ(a, twin);
    EXPECT_NE(a, other);
    EXPECT_NE(a, none);
    EXPECT_NE(none, a);

    EditSettings x;
    EditSettings y;
    EXPECT_EQ(x, y);
    y.strokes = a;
    EXPECT_NE(x, y);
    x.strokes = twin;
    EXPECT_EQ(x, y);
    x.dodge_burn_on = false;
    EXPECT_NE(x, y);
}

TEST(DodgeBurnStage, ProcessSettingsAfterResetsIt) {
    const EditSettings s = edited(make_strokes(300, 200));
    const prism::ProcessSettings full =
        prism::gui::to_process_settings(s, prism::gui::final_stage, std::nullopt,
                                        prism::gui::stage_grid(300, 200));
    ASSERT_FALSE(full.dodge_burn.field.strokes.empty());
    for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
        const prism::ProcessSettings after = prism::gui::process_settings_after(full, info.id);
        const bool gone = static_cast<int>(info.id) >= static_cast<int>(StageId::dodge_burn);
        EXPECT_EQ(after.dodge_burn.field.strokes.empty(), gone) << info.slug;
        EXPECT_EQ(after.dodge_burn.grid.full_w, gone ? 0 : 300) << info.slug;
        // The edits before it are untouched by resetting it, and the ones after still stand.
        EXPECT_EQ(after.exposure_stops.has_value(), info.id < StageId::exposure) << info.slug;
        EXPECT_EQ(after.saturation.has_value(), info.id < StageId::saturation) << info.slug;
    }
}

TEST(DodgeBurnStage, StageDoesNotClamp) {
    using prism::gui::ClipDirection;
    EXPECT_FALSE(prism::gui::stage_clamps(StageId::dodge_burn, ClipDirection::highlights));
    EXPECT_FALSE(prism::gui::stage_clamps(StageId::dodge_burn, ClipDirection::shadows));

    // So a stroke that pushes pixels over white leaves them recoverable, not lost.
    const Image base = make_image(200, 120);
    EditSettings s;
    s.strokes = make_strokes(200, 120);
    const ClipReport report = prism::gui::compute_clip_report(base, s, 1);
    const prism::gui::StageClip& row = report.at(StageId::dodge_burn);
    EXPECT_TRUE(row.ran);
    EXPECT_GT(row.over.any, 0u);
    EXPECT_EQ(row.over_fate, prism::gui::ClipFate::recoverable);
    EXPECT_EQ(report.first_over, StageId::dodge_burn);
}

TEST(DodgeBurnStage, StrokesWithoutAGridAreRefusedAndAnOffStageNeedsNone) {
    EditSettings s = edited(make_strokes(300, 200));
    EXPECT_THROW((void)prism::gui::to_process_settings(s), std::invalid_argument);
    // Before the stage nothing needs a grid, and off means nothing is carried.
    EXPECT_NO_THROW((void)prism::gui::to_process_settings(s, StageId::exposure));
    s.dodge_burn_on = false;
    const prism::ProcessSettings off = prism::gui::to_process_settings(s);
    EXPECT_TRUE(off.dodge_burn.field.strokes.empty());
    EXPECT_TRUE(prism::gui::to_process_settings(EditSettings{}).dodge_burn.field.strokes.empty());
}

// --- Nothing changes without strokes -----------------------------------------------------------

TEST(DodgeBurnStage, NoStrokesChangesNothing) {
    const Image base = make_image(257, 131);
    const EditSettings plain = edited(Strokes{});
    EditSettings empty = plain;
    empty.strokes = Strokes(std::make_shared<StrokeField>());
    EditSettings off = edited(make_strokes(257, 131));
    off.dodge_burn_on = false;

    PreviewOptions options;
    options.scopes = true;
    options.clip_report = true;
    options.clip_mask = true;
    for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
        if (info.hidden) {
            continue;
        }
        const PreviewFrame want = preview(base, plain, info.id, options);
        for (const EditSettings* other : {&empty, &off}) {
            const PreviewFrame got = preview(base, *other, info.id, options);
            EXPECT_EQ(got.rgb8, want.rgb8) << info.slug;
            EXPECT_EQ(got.clip_mask, want.clip_mask) << info.slug;
            EXPECT_EQ(got.clips, want.clips) << info.slug;
            EXPECT_EQ(got.histogram.luminance, want.histogram.luminance) << info.slug;
            EXPECT_EQ(bits_of(stage_image(base, *other, info.id)),
                      bits_of(stage_image(base, plain, info.id)))
                << info.slug;
        }
    }
}

TEST(DodgeBurnStage, NoStrokesChangesNothingInAnExport) {
    prism::Image img = make_image(180, 120);
    const fs::path source = temp_file("in.png");
    prism::save_image(source, img);

    const EditSettings plain = edited(Strokes{});
    EditSettings empty = plain;
    empty.strokes = Strokes(std::make_shared<StrokeField>());
    EditSettings off = edited(make_strokes(180, 120));
    off.dodge_burn_on = false;

    for (const std::optional<int> long_side : {std::optional<int>{}, std::optional<int>{90}}) {
        const fs::path want = temp_file("want.png");
        ASSERT_EQ(prism::gui::export_image(
                      {source, prism::gui::InputKind::image, plain, 1, long_side, nullptr}, want),
                  std::nullopt);
        for (const EditSettings* other : {&empty, &off}) {
            const fs::path got = temp_file("got.png");
            ASSERT_EQ(
                prism::gui::export_image(
                    {source, prism::gui::InputKind::image, *other, 1, long_side, nullptr}, got),
                std::nullopt);
            EXPECT_EQ(file_bytes(got), file_bytes(want));
            fs::remove(got);
        }
        fs::remove(want);
    }
    fs::remove(source);
}

// --- Every place that renders puts the strokes on the same pixels -------------------------------

namespace {

struct Size {
    int w;
    int h;
};

class DodgeBurnAgreement : public ::testing::TestWithParam<Size> {};

}  // namespace

// The sizes are above the library's multi-threaded threshold (a landscape one and a portrait one,
// whose test strip bands run along the other axis); threads 0 uses every core.
INSTANTIATE_TEST_SUITE_P(Sizes, DodgeBurnAgreement,
                         ::testing::Values(Size{1501, 701}, Size{733, 1409}),
                         [](const ::testing::TestParamInfo<Size>& info) {
                             return std::to_string(info.param.w) + "x" +
                                    std::to_string(info.param.h);
                         });

TEST_P(DodgeBurnAgreement, TheClipReportSamplesTheFullRendersPixels) {
    const auto [w, h] = GetParam();
    const Image base = make_image(w, h);
    const EditSettings s = edited(make_strokes(w, h));
    const ClipReport whole = prism::gui::compute_clip_report(base, s);
    // The strokes do something to the counts, or this would prove nothing.
    EditSettings none = s;
    none.strokes = Strokes{};
    const ClipReport without = prism::gui::compute_clip_report(base, none);
    EXPECT_NE(whole.at(StageId::dodge_burn), without.at(StageId::dodge_burn));
    EXPECT_GT(whole.at(StageId::dodge_burn).over.any, without.at(StageId::dodge_burn).over.any);

    PreviewOptions options;
    options.clip_report = true;
    // Viewing Decode, Exposure or the Tone curve: the stages after run on the sample grid only.
    for (const StageId viewed : {StageId::decode, StageId::exposure, StageId::curve}) {
        const PreviewFrame frame = preview(base, s, viewed, options);
        ASSERT_TRUE(frame.clips.has_value());
        EXPECT_EQ(*frame.clips, whole) << prism::gui::stage_info(viewed).slug;
    }
    for (const int threads : {1, 3}) {
        const PreviewFrame frame = preview(base, s, StageId::exposure, options, threads);
        ASSERT_TRUE(frame.clips.has_value());
        EXPECT_EQ(*frame.clips, whole) << threads << " threads";
    }
}

TEST_P(DodgeBurnAgreement, TheSampleGridIsOnTheSamePixelsAsTheFullImage) {
    const auto [w, h] = GetParam();
    const Image base = make_image(w, h);
    const EditSettings s = edited(make_strokes(w, h));
    const Image full = stage_image(base, s, StageId::dodge_burn);

    prism::gui::ClipRecorder recorder(w, h);
    const Image sampled = recorder.sample_grid(base);
    const prism::PixelGrid grid = recorder.grid(prism::gui::stage_grid(w, h));
    ASSERT_EQ(grid.width(), sampled.width);
    ASSERT_EQ(grid.height(), sampled.height);
    ASSERT_GT(recorder.report().stride, 1);

    prism::ProcessSettings process_settings = prism::gui::to_process_settings(
        s, StageId::dodge_burn, std::nullopt, grid);
    Image run = sampled;
    ASSERT_EQ(prism::process(run, process_settings, 1), prism::ProcessResult::completed);
    const int stride = recorder.report().stride;
    const int first = grid.x0;
    int changed = 0;
    for (int j = 0; j < run.height; ++j) {
        for (int i = 0; i < run.width; ++i) {
            const int x = first + i * stride;
            const int y = first + j * stride;
            EXPECT_EQ(pixel_bits(run, i, j), pixel_bits(full, x, y)) << x << "," << y;
            changed += pixel_bits(run, i, j) != pixel_bits(base, x, y);
        }
    }
    EXPECT_GT(changed, 100);
}

TEST_P(DodgeBurnAgreement, TheSamplerReadsTheFullRendersPixels) {
    const auto [w, h] = GetParam();
    const Image base = make_image(w, h);
    const EditSettings s = edited(make_strokes(w, h));
    const Image dodged = stage_image(base, s, StageId::dodge_burn);
    const Image last = stage_image(base, s, prism::gui::final_stage);
    const PreviewFrame frame = preview(base, s, prism::gui::final_stage);

    const std::vector<std::pair<int, int>> pixels = {
        {0, 0},         {w - 1, h - 1}, {w / 2, h / 2},      {w / 10, h * 15 / 100},
        {w * 7 / 10, h / 5},            {w / 3, h * 61 / 100}, {w - 1, 0},  {0, h - 1},
        {w * 4 / 10, h / 2},            {w * 85 / 100, h * 8 / 10}, {123, 321 % h}};
    int on_strokes = 0;
    for (const auto& [x, y] : pixels) {
        const std::optional<prism::gui::PixelTrace> trace = prism::gui::sample_trace(base, s, x, y);
        ASSERT_TRUE(trace.has_value());
        const auto bits = [](const std::array<float, 3>& v) {
            return std::array<std::uint32_t, 3>{std::bit_cast<std::uint32_t>(v[0]),
                                                std::bit_cast<std::uint32_t>(v[1]),
                                                std::bit_cast<std::uint32_t>(v[2])};
        };
        EXPECT_EQ(bits(trace->at(StageId::dodge_burn).rgb), pixel_bits(dodged, x, y))
            << x << "," << y;
        EXPECT_EQ(bits(trace->at(StageId::encode).rgb), pixel_bits(last, x, y)) << x << "," << y;
        EXPECT_EQ(trace->srgb8, pixel_bytes(frame, x, y)) << x << "," << y;
        on_strokes += trace->dodge_burn_stops != 0.0f;
        // The line about the strokes agrees with what they did to the pixel.
        EXPECT_EQ(trace->dodge_burn_stops != 0.0f,
                  trace->at(StageId::dodge_burn).rgb != trace->at(StageId::exposure).rgb)
            << x << "," << y;
    }
    EXPECT_GT(on_strokes, 3);
    EXPECT_LT(on_strokes, static_cast<int>(pixels.size()));
}

TEST_P(DodgeBurnAgreement, ATestStripBandIsTheFullRenderAtItsOwnExposure) {
    const auto [w, h] = GetParam();
    const Image base = make_image(w, h);
    const EditSettings s = edited(make_strokes(w, h));
    const prism::gui::TestStrip strip{prism::gui::StripStep::half, 5};
    PreviewOptions options;
    options.test_strip = strip;
    const std::vector<prism::gui::StripRect> bands = prism::gui::strip_bands(w, h, strip.count);
    for (const StageId viewed : {StageId::dodge_burn, prism::gui::final_stage}) {
        const PreviewFrame framed = preview(base, s, viewed, options);
        ASSERT_EQ(framed.width, w);
        ASSERT_EQ(framed.height, h);
        for (int band = 0; band < strip.count; ++band) {
            const EditSettings at = prism::gui::strip_band_settings(s, strip.step, strip.count, band);
            const PreviewFrame whole = preview(base, at, viewed);
            const prism::gui::StripRect& r = bands[static_cast<std::size_t>(band)];
            int different = 0;
            for (int y = r.y; y < r.y + r.height; ++y) {
                for (int x = r.x; x < r.x + r.width; ++x) {
                    different += pixel_bytes(framed, x, y) != pixel_bytes(whole, x, y);
                }
            }
            EXPECT_EQ(different, 0) << "band " << band << " of " << prism::gui::stage_info(viewed).slug;
        }
    }
}

// --- An export is the same strokes on a smaller image ------------------------------------------

TEST(DodgeBurnStage, ExportAtAnySizeCoversTheSamePart) {
    constexpr int w = 1200;
    constexpr int h = 800;
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(static_cast<std::size_t>(w) * h * 3, 0.2f);
    const fs::path source = temp_file("grey.png");
    prism::save_image(source, img);

    EditSettings with;
    with.strokes = make_strokes(w, h);
    EditSettings without = with;
    without.dodge_burn_on = false;

    // The share of pixels the strokes changed, at each export size.
    const auto changed_share = [&](std::optional<int> long_side) {
        const fs::path a = temp_file("a.png");
        const fs::path b = temp_file("b.png");
        EXPECT_EQ(prism::gui::export_image(
                      {source, prism::gui::InputKind::image, with, 0, long_side, nullptr}, a),
                  std::nullopt);
        EXPECT_EQ(prism::gui::export_image(
                      {source, prism::gui::InputKind::image, without, 0, long_side, nullptr}, b),
                  std::nullopt);
        const Image dodged = prism::load_image(a);
        const Image plain = prism::load_image(b);
        EXPECT_EQ(dodged.width, plain.width);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < dodged.pixels.size(); i += 3) {
            changed += dodged.pixels[i] != plain.pixels[i] ||
                       dodged.pixels[i + 1] != plain.pixels[i + 1] ||
                       dodged.pixels[i + 2] != plain.pixels[i + 2];
        }
        fs::remove(a);
        fs::remove(b);
        return static_cast<double>(changed) / static_cast<double>(dodged.pixels.size() / 3);
    };
    const double original = changed_share(std::nullopt);
    EXPECT_GT(original, 0.15);  // the strokes are a real part of the picture
    EXPECT_LT(original, 0.6);
    for (const int long_side : {600, 300, 150}) {
        // The soft edges round differently at each size: a hundredth of the picture is the room.
        EXPECT_NEAR(changed_share(long_side), original, 0.01) << long_side;
    }
    fs::remove(source);
}

// --- Recipes, looks, reset and undo -------------------------------------------------------------

TEST(DodgeBurnStage, ARecipeIsTheSameWithAndWithoutStrokes) {
    EditSettings plain = edited(Strokes{});
    EditSettings stroked = plain;
    stroked.strokes = make_strokes(300, 200);
    EditSettings stroked_off = stroked;
    stroked_off.dodge_burn_on = false;
    const std::string want = prism::write_recipe(prism::gui::recipe_from(plain));
    EXPECT_EQ(prism::write_recipe(prism::gui::recipe_from(stroked)), want);
    EXPECT_EQ(prism::write_recipe(prism::gui::recipe_from(stroked_off)), want);
    EXPECT_EQ(want.find("dodge"), std::string::npos);
}

TEST(DodgeBurnStage, ALookKeepsTheStrokes) {
    EditSettings s = edited(make_strokes(300, 200));
    s.dodge_burn_on = false;
    const auto strokes = s.strokes.field();
    for (const prism::gui::Preset& preset : prism::gui::presets_for(prism::gui::InputKind::image)) {
        EditSettings t = s;
        prism::gui::apply_preset(t, preset);
        EXPECT_EQ(t.strokes.field(), strokes) << preset.id;  // the same object, not a copy
        EXPECT_FALSE(t.dodge_burn_on) << preset.id;
    }
}

TEST(DodgeBurnStage, ResetClearsTheStrokesInOneStepAndUndoBringsThemBack) {
    const EditSettings drawn = edited(make_strokes(300, 200));
    prism::gui::History history(drawn);
    EditSettings s = history.current();
    prism::gui::reset_to_defaults(s);
    EXPECT_TRUE(s.strokes.empty());
    EXPECT_TRUE(s.dodge_burn_on);
    history.set(s);
    EXPECT_TRUE(history.current().strokes.empty());
    EXPECT_EQ(history.current(), EditSettings{});
    ASSERT_TRUE(history.undo());
    EXPECT_FALSE(history.current().strokes.empty());
    EXPECT_EQ(history.current(), drawn);
    EXPECT_EQ(history.current().strokes.field(), drawn.strokes.field());  // copied as a pointer
    EXPECT_FALSE(history.can_undo());  // the reset was one step
    ASSERT_TRUE(history.redo());
    EXPECT_TRUE(history.current().strokes.empty());
}

// --- The sampler's line ------------------------------------------------------------------------

TEST(DodgeBurnStage, TheSamplersLineSaysLighterOrDarkerAndStopOrStops) {
    const auto text = [](float stops) {
        prism::gui::PixelTrace trace;
        trace.dodge_burn_stops = stops;
        return prism::gui::sample_dodge_burn_text(trace);
    };
    EXPECT_EQ(text(0.0f), "");
    EXPECT_EQ(text(-0.0f), "");
    EXPECT_EQ(text(0.5f), "Dodge & burn: 0.5 stop lighter here");
    EXPECT_EQ(text(-0.5f), "Dodge & burn: 0.5 stop darker here");
    EXPECT_EQ(text(1.0f), "Dodge & burn: 1.0 stop lighter here");
    EXPECT_EQ(text(-1.0f), "Dodge & burn: 1.0 stop darker here");
    EXPECT_EQ(text(1.04f), "Dodge & burn: 1.0 stop lighter here");  // as written, so one stop
    EXPECT_EQ(text(1.06f), "Dodge & burn: 1.1 stops lighter here");
    EXPECT_EQ(text(-1.5f), "Dodge & burn: 1.5 stops darker here");
    EXPECT_EQ(text(-12.0f), "Dodge & burn: 12.0 stops darker here");
    EXPECT_EQ(text(0.049f), "");  // would read "0.0 stop": no line instead
    EXPECT_EQ(text(-0.049f), "");
    EXPECT_EQ(text(0.05f), "Dodge & burn: 0.1 stop lighter here");
    EXPECT_EQ(text(-0.05f), "Dodge & burn: 0.1 stop darker here");
    EXPECT_EQ(text(std::nanf("")), "");
    EXPECT_EQ(text(-0.5f).find("brightness"), std::string::npos);
}

TEST(DodgeBurnStage, TheLineShowsFromDodgeAndBurnOnAndOnlyWhereAStrokeIs) {
    const Image base = make_image(200, 120);
    const EditSettings s = edited(make_strokes(200, 120));
    // The middle of the soft dodge dab at (0.7, 0.2) and a corner no stroke reaches.
    const auto on = prism::gui::sample_trace(base, s, 140, 24);
    const auto off = prism::gui::sample_trace(base, s, 0, 119);
    ASSERT_TRUE(on && off);
    EXPECT_GT(on->dodge_burn_stops, 0.0f);
    EXPECT_EQ(off->dodge_burn_stops, 0.0f);

    for (const StageId stage : {StageId::decode, StageId::exposure}) {
        EXPECT_EQ(prism::gui::sample_rows(*on, stage).dodge_burn, "") << static_cast<int>(stage);
    }
    for (const StageId stage : {StageId::dodge_burn, StageId::curve, StageId::encode}) {
        EXPECT_EQ(prism::gui::sample_rows(*on, stage).dodge_burn,
                  prism::gui::sample_dodge_burn_text(*on));
        EXPECT_EQ(prism::gui::sample_rows(*off, stage).dodge_burn, "");
    }
    EXPECT_NE(prism::gui::describe_sample(*on, StageId::curve).find("lighter here"),
              std::string::npos);
    EXPECT_EQ(prism::gui::describe_sample(*off, StageId::curve).find("Dodge"), std::string::npos);

    // With the stage off, the pixel is not touched and the line is not shown.
    EditSettings switched_off = s;
    switched_off.dodge_burn_on = false;
    const auto none = prism::gui::sample_trace(base, switched_off, 140, 24);
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(none->dodge_burn_stops, 0.0f);
    EXPECT_EQ(prism::gui::sample_dodge_burn_text(*none), "");
}

// --- How long the preview takes with strokes ----------------------------------------------------

TEST(DodgeBurnStage, FiftyStrokesOnAPreviewAreTimedAndChangeThePicture) {
    constexpr int w = 2560;
    constexpr int h = 1707;
    const Image base = make_image(w, h);
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    for (int i = 0; i < 50; ++i) {
        std::vector<FieldPoint> points;
        for (int k = 0; k < 40; ++k) {  // a 40 point stroke, a few percent of the picture long
            points.push_back({0.1f + 0.015f * static_cast<float>(i % 50) + 0.002f * static_cast<float>(k),
                              0.1f + 0.012f * static_cast<float>(i) + 0.003f * static_cast<float>(k)});
        }
        field->strokes.push_back(std::make_shared<const FieldStroke>(
            FieldStroke{std::move(points), 0.04f, 0.5f, i % 2 ? -0.5f : 0.5f}));
    }
    EditSettings none = edited(Strokes{});
    EditSettings fifty = none;
    fifty.strokes = Strokes(std::move(field));

    const auto best_ms = [&](const EditSettings& s) {
        double best = 1e9;
        for (int i = 0; i < 5; ++i) {
            best = std::min(best, preview(base, s, prism::gui::final_stage).total_ms);
        }
        return best;
    };
    const double without = best_ms(none);
    const double with = best_ms(fifty);
    RecordProperty("preview_ms_without_strokes", static_cast<int>(without));
    RecordProperty("preview_ms_with_50_strokes", static_cast<int>(with));
    std::printf("preview 2560x1707: %.1f ms without strokes, %.1f ms with 50\n", without, with);
    // Not asserted: a timing bound is a flake under a loaded machine (ctest -j). The numbers are
    // there for whoever reads the test output; what is checked is that the strokes did something.
    EXPECT_NE(preview(base, fifty, prism::gui::final_stage).rgb8,
              preview(base, none, prism::gui::final_stage).rgb8);
}
