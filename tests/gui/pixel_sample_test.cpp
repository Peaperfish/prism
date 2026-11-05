#include "prism/gui/pixel_sample.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/test_strip.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"

namespace prism::gui {
namespace {

constexpr std::array<StageId, 7> all_stages = {
    StageId::decode,   StageId::exposure, StageId::matrix, StageId::saturation,
    StageId::contrast, StageId::curve,    StageId::encode};

// The same bits, or both NaN: the finite values must match to the last bit, while a NaN's sign and
// payload mean nothing and differ between instruction sets.
bool same_value(float a, float b) noexcept {
    return (std::isnan(a) && std::isnan(b)) ||
           std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// A pixel's channels, spread over everything a stage treats differently: deep shadows, midtones,
// above white, negatives, denormals. The same every run, and on every platform.
Image varied_image(int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * Image::channels);
    std::uint32_t state = 12345;
    for (float& value : img.pixels) {
        state = state * 1664525u + 1013904223u;
        const float t = static_cast<float>(state >> 8) / 16777216.0f;  // 0 to 1
        switch (state >> 29) {
            case 0: value = t * 0.01f; break;        // deep shadows
            case 1: value = 1.0f + t * 9.0f; break;  // above white
            case 2: value = -0.3f * t; break;        // negative
            default: value = t; break;
        }
    }
    // A pixel for each case that takes its own path through a stage.
    if (img.pixels.size() >= 30) {
        const float specials[] = {0.0f,   0.0f,   0.0f,   0.18f, 0.18f, 0.18f, 1.0f,   1.0f, 1.0f,
                                  1e-40f, 1e-40f, 1e-40f, 1e30f, 0.5f,  0.5f,  -1e-5f, 0.5f, 0.5f};
        std::copy(std::begin(specials), std::end(specials), img.pixels.begin() + 3);
    }
    return img;
}

// Every curve kind, a matrix that makes negatives, and contrast other than 1.
std::vector<std::pair<const char*, EditSettings>> varied_settings() {
    EditSettings base;
    base.exposure_stops = 0.7f;
    base.matrix = MatrixFile{{}, {1.2f, -0.1f, -0.1f, -0.2f, 1.3f, -0.1f, -0.3f, -0.2f, 1.5f}};
    base.saturation = 1.3f;
    base.contrast = 1.25f;

    std::vector<std::pair<const char*, EditSettings>> out;
    EditSettings s = base;
    s.curve = CurveKind::none;
    out.emplace_back("no curve", s);
    s.curve = CurveKind::gamma;
    s.gamma = 2.2f;
    out.emplace_back("gamma", s);
    s.curve = CurveKind::reinhard;
    s.reinhard_white = 4.0f;
    out.emplace_back("Reinhard", s);
    s.curve = CurveKind::filmic;
    out.emplace_back("filmic", s);
    s.saturation_on = false;  // negatives reach the contrast table and the curve
    s.contrast = 0.8f;
    out.emplace_back("filmic, no saturation", s);
    s = EditSettings{};
    out.emplace_back("neutral", s);
    return out;
}

const float* pixel_of(const Image& img, int x, int y) {
    return img.pixels.data() +
           (static_cast<std::size_t>(y) * img.width + static_cast<std::size_t>(x)) *
               Image::channels;
}

PixelTrace trace_of(const Image& img, const EditSettings& s, int x, int y) {
    std::optional<PixelTrace> trace = sample_trace(img, s, x, y);
    if (!trace) {
        throw std::logic_error("no trace");
    }
    return *std::move(trace);
}

}  // namespace

// --- Bit-identical to the render -----------------------------------------------------------------

// A row of 37 pixels is 111 floats, so a stage's vectorised loop (up to 16 floats a step) runs its
// body many times, where a 1x1 image only ever runs the scalar tail.
TEST(PixelSample, EveryPixelOfAWideImageMatchesTheRenderAtEveryStage) {
    const Image base = varied_image(37, 3);
    for (const auto& [name, s] : varied_settings()) {
        std::vector<PixelTrace> traces;
        for (int y = 0; y < base.height; ++y) {
            for (int x = 0; x < base.width; ++x) {
                traces.push_back(trace_of(base, s, x, y));
            }
        }
        for (const int threads : {1, 8}) {
            for (const StageId stage : all_stages) {
                const std::optional<Image> rendered = render_stage_image(base, s, stage, threads);
                ASSERT_TRUE(rendered.has_value());
                for (std::size_t i = 0; i < traces.size(); ++i) {
                    const std::array<float, 3>& rgb = traces[i].at(stage).rgb;
                    for (std::size_t c = 0; c < 3; ++c) {
                        ASSERT_TRUE(same_value(rgb[c], rendered->pixels[i * 3 + c]))
                            << name << ", " << stage_info(stage).name << ", threads " << threads
                            << ", pixel " << i << ", channel " << c << ": " << rgb[c] << " against "
                            << rendered->pixels[i * 3 + c];
                    }
                }
            }
        }
    }
}

// Below parallel_min_pixels a render uses one thread whatever it is asked for, so for 8 threads to
// mean anything the picture must be over a megapixel. 37 is prime, so a tile of that width repeated
// across a picture puts each of its values in every lane of a vector and at every place in a chunk,
// without tracing a million pixels.
TEST(PixelSample, ARenderOnEightThreadsMatchesToo) {
    constexpr int tile_width = 37;
    constexpr int tile_height = 2;
    const Image tile = varied_image(tile_width, tile_height);
    Image big;
    big.width = tile_width * tile_width;
    big.height = 768;
    ASSERT_GE(static_cast<std::size_t>(big.width) * big.height, parallel_min_pixels);
    ASSERT_GT(threads_for_pixels(static_cast<std::size_t>(big.width) * big.height, 8), 1);
    big.pixels.resize(static_cast<std::size_t>(big.width) * big.height * Image::channels);
    for (int y = 0; y < big.height; ++y) {
        for (int x = 0; x < big.width; ++x) {
            const float* from = pixel_of(tile, x % tile_width, y % tile_height);
            std::copy_n(from, Image::channels,
                        big.pixels.begin() +
                            static_cast<std::ptrdiff_t>(pixel_of(big, x, y) - big.pixels.data()));
        }
    }
    for (const auto& [name, s] : varied_settings()) {
        std::vector<PixelTrace> traces;
        for (int y = 0; y < tile_height; ++y) {
            for (int x = 0; x < tile_width; ++x) {
                traces.push_back(trace_of(tile, s, x, y));
            }
        }
        for (const StageId stage : all_stages) {
            const std::optional<Image> rendered = render_stage_image(big, s, stage, 8);
            ASSERT_TRUE(rendered.has_value());
            std::size_t different = 0;
            for (int y = 0; y < big.height; ++y) {
                for (int x = 0; x < big.width; ++x) {
                    const std::size_t t = static_cast<std::size_t>(y % tile_height) * tile_width +
                                          static_cast<std::size_t>(x % tile_width);
                    const std::array<float, 3>& rgb = traces[t].at(stage).rgb;
                    const float* p = pixel_of(*rendered, x, y);
                    different += !(same_value(rgb[0], p[0]) && same_value(rgb[1], p[1]) &&
                                   same_value(rgb[2], p[2]));
                }
            }
            EXPECT_EQ(different, 0u) << name << ", " << stage_info(stage).name;
        }
    }
}

TEST(PixelSample, OutputEncodeMatchesThePreviewsBytes) {
    const Image base = varied_image(37, 3);
    for (const auto& [name, s] : varied_settings()) {
        const std::optional<PreviewFrame> frame = render_preview(base, s, 1, {});
        ASSERT_TRUE(frame.has_value());
        for (int y = 0; y < base.height; ++y) {
            for (int x = 0; x < base.width; ++x) {
                const PixelTrace trace = trace_of(base, s, x, y);
                const std::size_t at = (static_cast<std::size_t>(y) * base.width + x) * 3;
                for (std::size_t c = 0; c < 3; ++c) {
                    ASSERT_EQ(trace.srgb8[c], frame->rgb8[at + c])
                        << name << ", pixel " << x << "," << y << ", channel " << c;
                }
            }
        }
    }
}

TEST(PixelSample, TheMaskMatchesTheOverlayAtEveryStage) {
    const Image base = varied_image(37, 3);
    for (const auto& [name, s] : varied_settings()) {
        for (const StageId stage : all_stages) {
            PreviewOptions options;
            options.clip_mask = true;
            const std::optional<PreviewFrame> frame =
                render_preview(base, s, 1, {}, {}, stage, options);
            ASSERT_TRUE(frame.has_value());
            for (int y = 0; y < base.height; ++y) {
                for (int x = 0; x < base.width; ++x) {
                    const std::size_t at = static_cast<std::size_t>(y) * base.width + x;
                    EXPECT_EQ(trace_of(base, s, x, y).at(stage).mask, frame->clip_mask[at])
                        << name << ", " << stage_info(stage).name << ", pixel " << x << "," << y;
                }
            }
        }
    }
}

TEST(PixelSample, AStripBandMatchesItsFramePixel) {
    const Image base = varied_image(40, 3);
    EditSettings s;
    s.exposure_stops = 0.5f;
    s.contrast = 1.2f;
    s.curve = CurveKind::filmic;
    for (const StripStep step : {StripStep::third, StripStep::half, StripStep::whole}) {
        PreviewOptions options;
        options.test_strip = TestStrip{step, strip_default_count};
        options.clip_mask = true;
        for (const StageId stage : {StageId::exposure, StageId::contrast, StageId::encode}) {
            const std::optional<PreviewFrame> frame =
                render_preview(base, s, 1, {}, {}, stage, options);
            ASSERT_TRUE(frame.has_value());
            for (int y = 0; y < base.height; ++y) {
                for (int x = 0; x < base.width; ++x) {
                    const EditSettings used = sample_settings(s, options.test_strip, stage,
                                                              base.width, base.height, x, y);
                    const PixelTrace trace = trace_of(base, used, x, y);
                    const std::size_t at = static_cast<std::size_t>(y) * base.width + x;
                    EXPECT_EQ(trace.at(stage).mask, frame->clip_mask[at]);
                    if (stage == StageId::encode) {
                        for (std::size_t c = 0; c < 3; ++c) {
                            EXPECT_EQ(trace.srgb8[c], frame->rgb8[at * 3 + c])
                                << "pixel " << x << "," << y;
                        }
                    }
                }
            }
        }
    }
}

TEST(PixelSample, NoStripOrAStripAtDecodeUsesTheSettingsAsTheyAre) {
    EditSettings s;
    s.exposure_stops = 0.5f;
    EXPECT_EQ(sample_settings(s, std::nullopt, StageId::encode, 40, 3, 0, 0), s);
    const std::optional<TestStrip> strip = TestStrip{};
    EXPECT_EQ(sample_settings(s, strip, StageId::decode, 40, 3, 0, 0), s);
    EXPECT_EQ(sample_settings(s, strip, StageId::encode, 40, 3, 20, 0), s);  // the middle band
    EXPECT_NE(sample_settings(s, strip, StageId::encode, 40, 3, 0, 0), s);   // the first
}

TEST(PixelSample, DecodeIsTheBaseAndAStageThatDidNotRunPassesItOn) {
    const Image base = varied_image(37, 3);
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.saturation_on = false;
    s.contrast = 1.5f;
    for (int y = 0; y < base.height; ++y) {
        for (int x = 0; x < base.width; ++x) {
            const PixelTrace trace = trace_of(base, s, x, y);
            EXPECT_EQ(trace.x, x);
            EXPECT_EQ(trace.y, y);
            const float* p = pixel_of(base, x, y);
            for (std::size_t c = 0; c < 3; ++c) {
                EXPECT_TRUE(same_value(trace.at(StageId::decode).rgb[c], p[c]));
                EXPECT_TRUE(same_value(trace.at(StageId::saturation).rgb[c],
                                       trace.at(StageId::exposure).rgb[c]));
                EXPECT_TRUE(same_value(trace.at(StageId::encode).rgb[c],
                                       trace.at(StageId::contrast).rgb[c]));
            }
            EXPECT_TRUE(trace.at(StageId::decode).ran);
            EXPECT_TRUE(trace.at(StageId::exposure).ran);
            EXPECT_FALSE(trace.at(StageId::matrix).ran);
            EXPECT_FALSE(trace.at(StageId::saturation).ran);
            EXPECT_TRUE(trace.at(StageId::contrast).ran);
            EXPECT_FALSE(trace.at(StageId::curve).ran);
            EXPECT_TRUE(trace.at(StageId::encode).ran);
        }
    }
}

// --- Off the picture, and values that aren't numbers ---------------------------------------------

namespace {

Image one_pixel(float r, float g, float b) {
    Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {r, g, b};
    return img;
}

EditSettings filmic() {
    EditSettings s;
    s.curve = CurveKind::filmic;
    return s;
}

// The orange of the clipping tests: the matrix takes 0.3 of red away from blue, to -0.22.
EditSettings orange_matrix() {
    EditSettings s;
    s.matrix = MatrixFile{{}, {1, 0, 0, 0, 1, 0, -0.3f, 0, 1}};
    return s;
}

SampleStats stats_of(float r, float g, float b, const EditSettings& s, StageId stage) {
    return sample_stats(trace_of(one_pixel(r, g, b), s, 0, 0), stage);
}

}  // namespace

TEST(PixelSample, APixelOffThePictureHasNoTrace) {
    const Image base = varied_image(4, 3);
    const EditSettings s;
    EXPECT_TRUE(sample_trace(base, s, 0, 0).has_value());
    EXPECT_TRUE(sample_trace(base, s, 3, 2).has_value());
    EXPECT_FALSE(sample_trace(base, s, -1, 0).has_value());
    EXPECT_FALSE(sample_trace(base, s, 0, -1).has_value());
    EXPECT_FALSE(sample_trace(base, s, 4, 0).has_value());
    EXPECT_FALSE(sample_trace(base, s, 0, 3).has_value());
}

TEST(PixelSample, AnEmptyImageHasNoTrace) {
    EXPECT_FALSE(sample_trace(Image{}, EditSettings{}, 0, 0).has_value());
    Image no_rows;
    no_rows.width = 5;
    EXPECT_FALSE(sample_trace(no_rows, EditSettings{}, 0, 0).has_value());
}

TEST(PixelSample, InvalidSettingsThrowEvenOffThePicture) {
    EditSettings s;
    s.saturation = 12.0f;
    EXPECT_THROW((void)sample_trace(varied_image(4, 3), s, 0, 0), std::invalid_argument);
    EXPECT_THROW((void)sample_trace(varied_image(4, 3), s, 99, 0), std::invalid_argument);
    EXPECT_THROW((void)sample_trace(Image{}, s, 0, 0), std::invalid_argument);
    // An off stage still counts, as in validate().
    s.saturation_on = false;
    EXPECT_THROW((void)sample_trace(varied_image(4, 3), s, 0, 0), std::invalid_argument);
}

TEST(PixelSample, ABufferOfTheWrongSizeThrowsRatherThanReadsOutOfBounds) {
    Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.resize(3);
    EXPECT_THROW((void)sample_trace(img, EditSettings{}, 3, 3), std::invalid_argument);
}

TEST(PixelSample, NaNIsCarriedAndCountsAsNothing) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const PixelTrace trace = trace_of(one_pixel(nan, 0.5f, 0.5f), filmic(), 0, 0);
    EXPECT_TRUE(std::isnan(trace.at(StageId::decode).rgb[0]));
    EXPECT_TRUE(std::isnan(trace.at(StageId::encode).rgb[0]));
    EXPECT_EQ(trace.srgb8[0], 0);  // the encode's NaN becomes 0
    const SampleStats stats = sample_stats(trace, StageId::decode);
    EXPECT_TRUE(std::isnan(stats.luminance));
    EXPECT_FALSE(stats.stops.has_value());
    EXPECT_FALSE(stats.no_light);
    EXPECT_FALSE(stats.over || stats.low || stats.gamut);
    EXPECT_NE(stats.fate.find("Not a number"), std::string::npos);
    EXPECT_EQ(stats.fate.find("Nothing clipped"), std::string::npos);
}

// --- Light
// -----------------------------------------------------------------------------------------

TEST(PixelSampleStats, MiddleGreyIsExactlyZeroStops) {
    const SampleStats grey = stats_of(0.18f, 0.18f, 0.18f, EditSettings{}, StageId::decode);
    EXPECT_FLOAT_EQ(grey.luminance, 0.18f);
    ASSERT_TRUE(grey.stops.has_value());
    EXPECT_EQ(*grey.stops, 0.0f);
    const SampleStats up = stats_of(0.36f, 0.36f, 0.36f, EditSettings{}, StageId::decode);
    ASSERT_TRUE(up.stops.has_value());
    EXPECT_EQ(*up.stops, 1.0f);
    const SampleStats down = stats_of(0.09f, 0.09f, 0.09f, EditSettings{}, StageId::decode);
    ASSERT_TRUE(down.stops.has_value());
    EXPECT_EQ(*down.stops, -1.0f);
}

TEST(PixelSampleStats, LuminanceUsesTheRec709Weights) {
    EXPECT_FLOAT_EQ(stats_of(1, 0, 0, EditSettings{}, StageId::decode).luminance, 0.2126f);
    EXPECT_FLOAT_EQ(stats_of(0, 1, 0, EditSettings{}, StageId::decode).luminance, 0.7152f);
    EXPECT_FLOAT_EQ(stats_of(0, 0, 1, EditSettings{}, StageId::decode).luminance, 0.0722f);
}

TEST(PixelSampleStats, NoLightMeansYAtOrBelowZero) {
    const SampleStats black = stats_of(0, 0, 0, EditSettings{}, StageId::decode);
    EXPECT_TRUE(black.no_light);
    EXPECT_FALSE(black.stops.has_value());
    EXPECT_FALSE(black.low);  // exactly black: nothing is out of range
    EXPECT_EQ(black.fate, "Nothing clipped.");

    const SampleStats negative = stats_of(-0.5f, 0.1f, 0.1f, EditSettings{}, StageId::decode);
    EXPECT_TRUE(negative.no_light);
    EXPECT_FALSE(negative.stops.has_value());
    EXPECT_TRUE(negative.low);
}

// --- What becomes of it
// ----------------------------------------------------------------------------

TEST(PixelSampleFate, BroughtBackByTheFilmicCurve) {
    const SampleStats stats = stats_of(1.21f, 1.05f, 0.86f, filmic(), StageId::exposure);
    EXPECT_TRUE(stats.over);
    EXPECT_EQ(
        stats.fate,
        "Red and green are over white. Not lost yet: the Tone curve (filmic) brings them back "
        "under 1.00.");
}

TEST(PixelSampleFate, ClippedAtOutputEncode) {
    const EditSettings none;
    EXPECT_EQ(stats_of(1.2f, 0.5f, 0.5f, none, StageId::exposure).fate,
              "Red is over white. Still recoverable until Output encode, which clips it.");
    const SampleStats here = stats_of(1.2f, 1.1f, 0.5f, none, StageId::encode);
    EXPECT_TRUE(here.over);
    EXPECT_EQ(here.fate, "Red and green are over white: Output encode clips them.");
}

TEST(PixelSampleFate, FilmicCannotBringBackWhatIsAboveItsWhite) {
    // Filmic maps 5.6 to white. Red is beyond it; green is not.
    EXPECT_EQ(
        stats_of(8.0f, 2.0f, 0.5f, filmic(), StageId::contrast).fate,
        "Red and green are over white. Not lost yet: the Tone curve (filmic) brings green back "
        "under 1.00; Output encode clips red.");
}

TEST(PixelSampleFate, OutOfGamutThenClampedAtSaturation) {
    EditSettings s = orange_matrix();
    s.saturation = 1.2f;
    const SampleStats at_matrix = stats_of(0.9f, 0.4f, 0.05f, s, StageId::matrix);
    EXPECT_TRUE(at_matrix.gamut);
    EXPECT_EQ(at_matrix.fate,
              "Too saturated for sRGB: blue is below 0. Still recoverable until Saturation, which "
              "clips it, so the hue may shift.");
    // The stage that clamped it, and every stage after it, still say so, as J still marks it.
    for (const StageId stage : {StageId::saturation, StageId::contrast, StageId::encode}) {
        const SampleStats after = stats_of(0.9f, 0.4f, 0.05f, s, stage);
        EXPECT_TRUE(after.gamut);
        EXPECT_EQ(after.fate,
                  "It was too saturated for sRGB until Saturation clipped it, so the hue may have "
                  "shifted.");
    }
    // Before the matrix it was still fine.
    EXPECT_EQ(stats_of(0.9f, 0.4f, 0.05f, s, StageId::exposure).fate,
              "Nothing clipped here, but it ends up too saturated for sRGB.");
}

TEST(PixelSampleFate, WithSaturationOffTheNextStageThatClampsTakesIt) {
    EditSettings s = orange_matrix();
    s.curve = CurveKind::filmic;
    EXPECT_EQ(stats_of(0.9f, 0.4f, 0.05f, s, StageId::matrix).fate,
              "Too saturated for sRGB: blue is below 0. Still recoverable until the Tone curve "
              "(filmic), which clips it, so the hue may shift.");
    s.curve = CurveKind::none;
    EXPECT_EQ(stats_of(0.9f, 0.4f, 0.05f, s, StageId::matrix).fate,
              "Too saturated for sRGB: blue is below 0. Still recoverable until Output encode, "
              "which clips it, so the hue may shift.");
    EXPECT_EQ(stats_of(0.9f, 0.4f, 0.05f, s, StageId::encode).fate,
              "Too saturated for sRGB: blue is below 0. Output encode clips it, so the hue may "
              "shift.");
}

TEST(PixelSampleFate, NoLightSaysWhichStageClipsIt) {
    EditSettings s = filmic();
    const SampleStats low = stats_of(-0.5f, -0.1f, 0.1f, s, StageId::contrast);
    EXPECT_TRUE(low.low);
    EXPECT_EQ(low.fate,
              "No light: red and green are below 0. Still recoverable until the Tone curve "
              "(filmic), which clips them.");
    EXPECT_EQ(stats_of(-0.5f, -0.1f, 0.1f, s, StageId::encode).fate,
              "It had no light until the Tone curve (filmic) clipped it.");
}

TEST(PixelSampleFate, ALaterStageThatPushesItOverIsNamedAsAWarning) {
    EditSettings s;
    s.contrast = 1.5f;
    EXPECT_EQ(stats_of(0.95f, 0.5f, 0.5f, s, StageId::exposure).fate,
              "Nothing clipped here, but it ends up over white.");
    s.curve = CurveKind::filmic;
    EXPECT_EQ(stats_of(0.95f, 0.5f, 0.5f, s, StageId::exposure).fate, "Nothing clipped.");
}

TEST(PixelSampleFate, AValueClippedInTheFileIsSaidAtEveryStage) {
    EditSettings s;
    s.exposure_stops = -1.0f;
    const SampleStats decode = stats_of(1.0f, 0.5f, 0.5f, s, StageId::decode);
    EXPECT_TRUE(decode.over);  // as J marks it at Decode
    EXPECT_EQ(decode.clipped_in_file, 0b001);
    EXPECT_EQ(decode.fate, "Red was already clipped in the file; no edit can bring it back.");
    const SampleStats later = stats_of(1.0f, 0.5f, 0.5f, s, StageId::encode);
    EXPECT_FALSE(later.over);  // J no longer does: 0.5 is in range
    EXPECT_EQ(later.clipped_in_file, 0b001);
    EXPECT_EQ(later.fate, "Red was already clipped in the file; no edit can bring it back.");
}

TEST(PixelSampleFate, SeveralChannelsClippedInTheFileAreSaidInThePlural) {
    EXPECT_EQ(stats_of(1.0f, 1.0f, 1.0f, EditSettings{}, StageId::decode).fate,
              "Red, green and blue were already clipped in the file; no edit can bring them back.");
}

TEST(PixelSampleFate, ValuesInRangeClipNowhere) {
    EXPECT_EQ(stats_of(0.42f, 0.27f, 0.19f, filmic(), StageId::encode).fate, "Nothing clipped.");
}

}  // namespace prism::gui
