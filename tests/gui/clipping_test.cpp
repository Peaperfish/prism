#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "prism/gui/clipping.hpp"
#include "prism/gui/live_preview.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

using prism::Image;
using prism::gui::ClipFate;
using prism::gui::ClipReport;
using prism::gui::compute_clip_report;
using prism::gui::describe_clipping;
using prism::gui::EditSettings;
using prism::gui::StageId;

namespace {

Image solid(int w, int h, float r, float g, float b) {
    Image img;
    img.width = w;
    img.height = h;
    for (int i = 0; i < w * h; ++i) {
        img.pixels.insert(img.pixels.end(), {r, g, b});
    }
    return img;
}

// Top half 0.5 grey, bottom half 0.1 grey.
Image half_and_half(int w, int h) {
    Image img = solid(w, h, 0.1f, 0.1f, 0.1f);
    for (std::size_t i = 0; i < img.pixels.size() / 2; ++i) {
        img.pixels[i] = 0.5f;
    }
    return img;
}

EditSettings with_exposure(float stops) {
    EditSettings s;
    s.exposure_stops = stops;
    return s;
}

}  // namespace

TEST(Clipping, ImageFullyInRangeClipsNowhere) {
    EditSettings s;
    s.exposure_stops = -1.0f;
    s.saturation = 0.5f;
    s.contrast = 1.2f;
    s.curve = prism::gui::CurveKind::gamma;
    const Image img = half_and_half(40, 40);
    const ClipReport r = compute_clip_report(img, s);

    EXPECT_EQ(r.sample_count, 1600u);
    for (const prism::gui::StageClip& row : r.stages) {
        EXPECT_EQ(row.over.any, 0u);
        EXPECT_EQ(row.under.any, 0u);
        EXPECT_EQ(row.over_fate, ClipFate::none);
        EXPECT_EQ(row.under_fate, ClipFate::none);
    }
    EXPECT_FALSE(r.first_over || r.first_over_lost || r.first_under || r.first_under_lost);
    EXPECT_EQ(describe_clipping(r), "Nothing clips.");
}

TEST(Clipping, StagesThatDidNotRunAreMarked) {
    EditSettings s = with_exposure(1.0f);
    s.contrast = 1.5f;
    s.contrast_on = false;
    const ClipReport r = compute_clip_report(half_and_half(8, 8), s);
    EXPECT_TRUE(r.at(StageId::decode).ran);
    EXPECT_TRUE(r.at(StageId::exposure).ran);
    EXPECT_FALSE(r.at(StageId::matrix).ran);      // no matrix
    EXPECT_FALSE(r.at(StageId::saturation).ran);  // 1.0 is neutral
    EXPECT_FALSE(r.at(StageId::contrast).ran);    // off
    EXPECT_FALSE(r.at(StageId::curve).ran);       // none
    EXPECT_TRUE(r.at(StageId::encode).ran);
}

TEST(Clipping, ExposureBoostGoesOverWhiteAndIsRecoverableUntilEncode) {
    // +2 stops: 0.5 -> 2.0 (half of the pixels), 0.1 -> 0.4.
    const ClipReport r = compute_clip_report(half_and_half(40, 40), with_exposure(2.0f));

    const prism::gui::StageClip& decode = r.at(StageId::decode);
    EXPECT_EQ(decode.over.any, 0u);
    EXPECT_EQ(decode.over_fate, ClipFate::none);

    const prism::gui::StageClip& exposure = r.at(StageId::exposure);
    EXPECT_EQ(exposure.over.any, 800u);
    EXPECT_EQ(exposure.over.red, 800u);
    EXPECT_EQ(exposure.over.green, 800u);
    EXPECT_EQ(exposure.over.blue, 800u);
    EXPECT_EQ(exposure.over_fate, ClipFate::recoverable);
    EXPECT_EQ(exposure.under.any, 0u);

    const prism::gui::StageClip& encode = r.at(StageId::encode);
    EXPECT_EQ(encode.over.any, 800u);
    EXPECT_EQ(encode.over_fate, ClipFate::lost);

    EXPECT_EQ(r.first_over, StageId::exposure);
    EXPECT_EQ(r.first_over_lost, StageId::encode);
    EXPECT_FALSE(r.first_under || r.first_under_lost);
    EXPECT_EQ(describe_clipping(r),
              "Highlights first clip at Exposure (50% of pixels); "
              "still recoverable until Output encode.");
}

TEST(Clipping, ValuesClampedOnlyAtOutputEncodeAreLostThere) {
    // 0.6 + 1 stop = 1.2: out of range after Exposure, clamped by the encode; nothing at Decode.
    const ClipReport r = compute_clip_report(solid(10, 10, 0.6f, 0.4f, 0.4f), with_exposure(1.0f));
    EXPECT_EQ(r.at(StageId::decode).over.any, 0u);
    EXPECT_EQ(r.at(StageId::decode).over_fate, ClipFate::none);
    EXPECT_EQ(r.at(StageId::exposure).over.any, 100u);
    EXPECT_EQ(r.at(StageId::exposure).over.red, 100u);
    EXPECT_EQ(r.at(StageId::exposure).over.green, 0u);
    EXPECT_EQ(r.at(StageId::exposure).over_fate, ClipFate::recoverable);
    EXPECT_EQ(r.at(StageId::encode).over.any, 100u);
    EXPECT_EQ(r.at(StageId::encode).over_fate, ClipFate::lost);
    EXPECT_EQ(r.first_over, StageId::exposure);
    EXPECT_EQ(r.first_over_lost, StageId::encode);
}

TEST(Clipping, PixelsAtTheCeilingOfAnEightBitFileAreLostAtDecode) {
    // Real 8-bit decode: 255 is exactly 1.0. 30 of the 100 pixels have a channel at 255.
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 100; ++i) {
        const bool clipped = i < 30;
        bytes.insert(bytes.end(), {static_cast<std::uint8_t>(clipped ? 255 : 200), 100, 100});
    }
    const Image img = prism::decode_srgb8(bytes, 10, 10);
    const ClipReport r = compute_clip_report(img, EditSettings{});

    const prism::gui::StageClip& decode = r.at(StageId::decode);
    EXPECT_EQ(decode.over.any, 30u);
    EXPECT_EQ(decode.over.red, 30u);
    EXPECT_EQ(decode.over.green, 0u);
    EXPECT_EQ(decode.over_fate, ClipFate::lost);
    EXPECT_EQ(r.first_over, StageId::decode);
    EXPECT_EQ(r.first_over_lost, StageId::decode);
    EXPECT_EQ(r.at(StageId::encode).over.any, 0u);  // nothing more to lose there
    EXPECT_EQ(describe_clipping(r),
              "30% of pixels were already clipped in the file; no edit can bring them back.");
}

TEST(Clipping, AnImageThatTopsOutBelowTheCeilingHasNoDecodeLoss) {
    // 0.9 is the brightest value: not at the ceiling.
    const ClipReport r = compute_clip_report(solid(10, 10, 0.9f, 0.5f, 0.1f), EditSettings{});
    EXPECT_EQ(r.at(StageId::decode).over.any, 0u);
    EXPECT_EQ(r.at(StageId::decode).over_fate, ClipFate::none);
    EXPECT_FALSE(r.first_over || r.first_over_lost);
    EXPECT_EQ(describe_clipping(r), "Nothing clips.");
}

TEST(Clipping, TheCeilingToleranceIsOneMillionth) {
    Image img;
    img.width = 3;
    img.height = 1;
    // 1 - 5e-7 is within 1e-6 of 1.0; 1 - 2e-6 and 0.999 are not.
    img.pixels = {1.0f - 5e-7f, 0.2f, 0.2f, 1.0f - 2e-6f, 0.2f, 0.2f, 0.999f, 0.2f, 0.2f};
    EXPECT_EQ(compute_clip_report(img, EditSettings{}).at(StageId::decode).over.any, 1u);
}

TEST(Clipping, ValuesAboveWhiteInAFloatImageAreNotLostAtDecode) {
    // A float source (the library can be given one; the app's own decoders never go above 1.0):
    // 1.5 is real highlight data, out of range and recoverable, like Exposure's.
    const ClipReport r = compute_clip_report(solid(10, 10, 1.5f, 0.5f, 0.5f), EditSettings{});
    EXPECT_EQ(r.at(StageId::decode).over.any, 100u);
    EXPECT_EQ(r.at(StageId::decode).over.red, 100u);
    EXPECT_EQ(r.at(StageId::decode).over_fate, ClipFate::recoverable);
    EXPECT_EQ(r.first_over, StageId::decode);
    EXPECT_EQ(r.first_over_lost, StageId::encode);
    EXPECT_NE(r.first_over_lost, StageId::decode);
    EXPECT_EQ(describe_clipping(r),
              "Highlights first clip at Decode (100% of pixels); "
              "still recoverable until Output encode.");
}

TEST(Clipping, EditsThatPushFileClippedPixelsOnlyAreRecoverableWhenAToneCurveHelps) {
    // Half at the ceiling, half at 0.3; +2 stops then Reinhard brings 4.0 back below 1.
    Image img = solid(10, 10, 0.3f, 0.3f, 0.3f);
    for (std::size_t i = 0; i < img.pixels.size() / 2; ++i) {
        img.pixels[i] = 1.0f;
    }
    EditSettings s = with_exposure(1.0f);
    s.curve = prism::gui::CurveKind::reinhard;
    const ClipReport r = compute_clip_report(img, s);
    EXPECT_EQ(r.first_over_lost, StageId::decode);
    EXPECT_EQ(r.at(StageId::encode).over_fate, ClipFate::none);
    EXPECT_EQ(describe_clipping(r),
              "50% of pixels were already clipped in the file. "
              "Edits push 50% over white at Exposure; still recoverable.");
}

TEST(Clipping, DecodeLossAndLaterClippingAreSaidInOneSentence) {
    // Half the pixels are at the ceiling; +1 stop pushes those (and only those) over white.
    Image img = solid(10, 10, 0.3f, 0.3f, 0.3f);
    for (std::size_t i = 0; i < img.pixels.size() / 2; ++i) {
        img.pixels[i] = 1.0f;
    }
    const ClipReport r = compute_clip_report(img, with_exposure(1.0f));
    EXPECT_EQ(r.first_over_lost, StageId::decode);
    EXPECT_EQ(r.at(StageId::exposure).over.any, 50u);
    EXPECT_EQ(describe_clipping(r),
              "50% of pixels were already clipped in the file. "
              "Edits push 50% over white at Exposure; lost at Output encode.");
}

TEST(Clipping, AToneCurveThatBringsHighlightsBackLosesNothing) {
    // 2.0 through Reinhard (white 4) is 0.75: in range again before the encode.
    EditSettings s = with_exposure(2.0f);
    s.curve = prism::gui::CurveKind::reinhard;
    const ClipReport r = compute_clip_report(half_and_half(40, 40), s);
    EXPECT_EQ(r.at(StageId::exposure).over_fate, ClipFate::recoverable);
    EXPECT_EQ(r.at(StageId::curve).over.any, 0u);
    EXPECT_EQ(r.at(StageId::encode).over.any, 0u);
    EXPECT_EQ(r.first_over, StageId::exposure);
    EXPECT_FALSE(r.first_over_lost);
    EXPECT_EQ(describe_clipping(r),
              "Highlights first clip at Exposure (50% of pixels); "
              "a later stage brings them back, so nothing is lost.");
}

TEST(Clipping, NegativeValuesFromAMatrixAreLostAtSaturation) {
    // R' = R - 20 G: (0.1, 0.5, 0.1) -> (-9.9, 0.5, 0.1), which has Y < 0: no light, a shadow.
    EditSettings s;
    s.matrix = prism::gui::MatrixFile{{}, {1, -20, 0, 0, 1, 0, 0, 0, 1}};
    s.saturation = 1.5f;
    const ClipReport r = compute_clip_report(solid(10, 10, 0.1f, 0.5f, 0.1f), s);

    EXPECT_EQ(r.at(StageId::matrix).under.any, 100u);
    EXPECT_EQ(r.at(StageId::matrix).under.red, 100u);
    EXPECT_EQ(r.at(StageId::matrix).under.blue, 0u);
    EXPECT_EQ(r.at(StageId::matrix).under_fate, ClipFate::recoverable);
    EXPECT_EQ(r.at(StageId::saturation).under.any, 100u);
    EXPECT_EQ(r.at(StageId::saturation).under_fate, ClipFate::lost);
    EXPECT_EQ(r.at(StageId::contrast).under_fate, ClipFate::none);  // clamped away already
    EXPECT_EQ(r.first_under, StageId::matrix);
    EXPECT_EQ(r.first_under_lost, StageId::saturation);
    EXPECT_EQ(describe_clipping(r),
              "Shadows first clip at Colour matrix (100% of pixels); "
              "still recoverable until Saturation.");
}

TEST(Clipping, ContrastKeepsNegativesRecoverableAndTheCurveClampsThem) {
    EditSettings s;
    s.matrix = prism::gui::MatrixFile{{}, {1, -20, 0, 0, 1, 0, 0, 0, 1}};
    s.contrast = 1.3f;
    s.curve = prism::gui::CurveKind::gamma;
    const ClipReport r = compute_clip_report(solid(10, 10, 0.1f, 0.5f, 0.1f), s);
    EXPECT_EQ(r.at(StageId::contrast).under_fate, ClipFate::recoverable);
    EXPECT_EQ(r.at(StageId::curve).under_fate, ClipFate::lost);
    EXPECT_EQ(r.at(StageId::curve).under.any, 100u);
    EXPECT_EQ(r.first_under_lost, StageId::curve);
}

TEST(Clipping, HighlightsAndShadowsAreDescribedTogether) {
    // Left pixel above white, right pixel below black (Y < 0).
    Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {1.5f, 0.5f, 0.5f, -1.0f, 0.2f, 0.2f};
    const ClipReport r = compute_clip_report(img, EditSettings{});
    EXPECT_EQ(
        describe_clipping(r),
        "Highlights first clip at Decode (50% of pixels); still recoverable until Output encode. "
        "Shadows first clip at Decode (50% of pixels); still recoverable until Output encode.");
}

TEST(Clipping, NaNCountsAsNeitherAndInfinityAsOutOfRange) {
    Image img;
    img.width = 3;
    img.height = 1;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    img.pixels = {nan, nan, nan, inf, 0.5f, 0.5f, -inf, 0.5f, 0.5f};
    const ClipReport r = compute_clip_report(img, EditSettings{});
    EXPECT_EQ(r.at(StageId::decode).over.any, 1u);
    EXPECT_EQ(r.at(StageId::decode).under.any, 1u);
}

TEST(Clipping, AnEmptyOrTinyImageDoesNotCrash) {
    const ClipReport empty = compute_clip_report(Image{}, with_exposure(2.0f));
    EXPECT_EQ(empty.sample_count, 0u);
    EXPECT_EQ(describe_clipping(empty), "Nothing clips.");

    const ClipReport one = compute_clip_report(solid(1, 1, 3.0f, 3.0f, 3.0f), with_exposure(1.0f));
    EXPECT_EQ(one.sample_count, 1u);
    EXPECT_EQ(one.at(StageId::exposure).over.any, 1u);

    const ClipReport thin = compute_clip_report(solid(1, 5, 0.5f, 0.5f, 0.5f), with_exposure(2.0f));
    EXPECT_EQ(thin.sample_count, 5u);
    EXPECT_EQ(thin.at(StageId::exposure).over.any, 5u);

    // Mismatched buffer: the stages reject it as ever.
    Image broken = solid(2, 2, 0.5f, 0.5f, 0.5f);
    broken.pixels.pop_back();
    EXPECT_THROW((void)compute_clip_report(broken, with_exposure(1.0f)), std::invalid_argument);
}

TEST(Clipping, ABigImageIsSampledOnAGrid) {
    // 1000 x 1000 = 1M pixels: stride 2, 500 x 500 samples.
    const ClipReport r =
        compute_clip_report(solid(1000, 1000, 0.5f, 0.5f, 0.5f), with_exposure(2.0f));
    EXPECT_EQ(r.stride, 2);
    EXPECT_EQ(r.sample_count, 250'000u);
    EXPECT_EQ(r.at(StageId::exposure).over.any, 250'000u);
}

TEST(Clipping, PercentsAreShortAndNeverRoundDownToNothing) {
    using prism::gui::format_clip_percent;
    EXPECT_EQ(format_clip_percent(0, 100), "0%");
    EXPECT_EQ(format_clip_percent(1, 100000), "<0.1%");
    EXPECT_EQ(format_clip_percent(21, 1000), "2.1%");
    EXPECT_EQ(format_clip_percent(1, 2), "50%");
    EXPECT_EQ(format_clip_percent(5, 0), "0%");
}

TEST(Clipping, TheClipMaskMarksPixelsOutOfRange) {
    Image img;
    img.width = 4;
    img.height = 1;
    img.pixels = {0.5f, 0.5f, 0.5f, 1.5f, 0.5f, 0.5f, 0.1f, -0.5f, 0.1f, 2.0f, -1.0f, 0.5f};
    std::vector<std::uint8_t> mask;
    prism::gui::compute_clip_mask(img, 1, mask);
    EXPECT_EQ(mask,
              (std::vector<std::uint8_t>{0, prism::gui::clip_mask_high, prism::gui::clip_mask_low,
                                         prism::gui::clip_mask_high | prism::gui::clip_mask_low}));
    prism::gui::compute_clip_mask(Image{}, 1, mask);
    EXPECT_TRUE(mask.empty());
}

// --- Gamut ---------------------------------------------------------------------------------------

namespace {

// A saturated orange; the matrix takes 0.3 of red away from blue: B' = 0.05 - 0.27 = -0.22.
EditSettings orange_matrix() {
    EditSettings s;
    s.matrix = prism::gui::MatrixFile{{}, {1, 0, 0, 0, 1, 0, -0.3f, 0, 1}};
    return s;
}
Image orange() {
    return solid(10, 10, 0.9f, 0.4f, 0.05f);
}

}  // namespace

TEST(ClippingGamut, AnImageInGamutHasNoneAtAnyStage) {
    EditSettings s = with_exposure(0.5f);
    s.saturation = 3.0f;  // limited to the boundary (a channel at exactly 0): not out of gamut
    s.contrast = 1.2f;
    s.curve = prism::gui::CurveKind::filmic;
    Image img = solid(10, 10, 0.4f, 0.3f, 0.2f);
    for (std::size_t i = 0; i < 30; ++i) {
        img.pixels[i] = 0.05f * static_cast<float>(i % 7);
    }
    const ClipReport r = compute_clip_report(img, s);
    for (const prism::gui::StageClip& row : r.stages) {
        EXPECT_EQ(row.gamut.any, 0u) << static_cast<int>(row.stage);
        EXPECT_EQ(row.gamut_fate, ClipFate::none);
    }
    EXPECT_FALSE(r.first_gamut || r.first_gamut_lost);
    EXPECT_EQ(prism::gui::describe_gamut(r), "");
}

TEST(ClippingGamut, ASaturatedOrangeThatTheMatrixPushesOutIsGamutNotAShadow) {
    EditSettings s = orange_matrix();
    s.saturation = 1.5f;
    const ClipReport r = compute_clip_report(orange(), s);
    const prism::gui::StageClip& matrix = r.at(StageId::matrix);
    EXPECT_EQ(matrix.gamut.any, 100u);
    EXPECT_EQ(matrix.gamut.red, 0u);
    EXPECT_EQ(matrix.gamut.green, 0u);
    EXPECT_EQ(matrix.gamut.blue, 100u);
    EXPECT_EQ(matrix.gamut_fate, ClipFate::recoverable);
    EXPECT_EQ(matrix.under.any, 0u);  // it still has light: not a shadow
    EXPECT_EQ(r.first_under, std::nullopt);
    EXPECT_EQ(r.first_gamut, StageId::matrix);
    EXPECT_EQ(r.first_gamut_lost, StageId::saturation);
    EXPECT_EQ(r.at(StageId::saturation).gamut.any, 100u);  // what arrived and was clamped
    EXPECT_EQ(r.at(StageId::saturation).gamut_fate, ClipFate::lost);
    EXPECT_EQ(r.at(StageId::curve).gamut.any, 0u);
    EXPECT_EQ(r.at(StageId::encode).gamut.any, 0u);
    EXPECT_EQ(prism::gui::describe_gamut(r),
              "100% of colours are too saturated for sRGB after Colour matrix; "
              "Saturation clips them, so hues may shift.");
    EXPECT_EQ(describe_clipping(r), "Nothing clips.");  // the first line is unchanged
}

TEST(ClippingGamut, TheColoursAreClippedAtTheToneCurveWhenSaturationIsOff) {
    EditSettings s = orange_matrix();
    s.contrast = 1.2f;  // keeps the negative
    s.curve = prism::gui::CurveKind::gamma;
    const ClipReport r = compute_clip_report(orange(), s);
    EXPECT_EQ(r.first_gamut, StageId::matrix);
    EXPECT_EQ(r.at(StageId::contrast).gamut_fate, ClipFate::recoverable);
    EXPECT_EQ(r.first_gamut_lost, StageId::curve);
    EXPECT_EQ(r.at(StageId::curve).gamut.any, 100u);
    EXPECT_EQ(r.at(StageId::curve).gamut_fate, ClipFate::lost);
    EXPECT_EQ(prism::gui::describe_gamut(r),
              "100% of colours are too saturated for sRGB after Colour matrix; "
              "Tone curve clips them, so hues may shift.");
}

TEST(ClippingGamut, TheColoursAreClippedAtOutputEncodeWhenNothingElseDoes) {
    const ClipReport r = compute_clip_report(orange(), orange_matrix());
    EXPECT_EQ(r.first_gamut, StageId::matrix);
    EXPECT_EQ(r.first_gamut_lost, StageId::encode);
    EXPECT_EQ(r.at(StageId::encode).gamut.any, 100u);
    EXPECT_EQ(r.at(StageId::encode).gamut_fate, ClipFate::lost);
}

TEST(ClippingGamut, ANegativeWithinHalfAnEightBitStepIsNeitherGamutNorShadow) {
    // Y > 0 and min channel -1e-4, above -1.5e-4.
    const ClipReport r = compute_clip_report(solid(4, 4, 0.5f, 0.5f, -1e-4f), EditSettings{});
    EXPECT_EQ(r.at(StageId::decode).gamut.any, 0u);
    EXPECT_EQ(r.at(StageId::decode).under.any, 0u);
    EXPECT_EQ(r.at(StageId::decode).over.any, 0u);
    EXPECT_EQ(describe_clipping(r), "Nothing clips.");
    EXPECT_EQ(prism::gui::describe_gamut(r), "");
    // Just past the tolerance it counts.
    EXPECT_EQ(compute_clip_report(solid(4, 4, 0.5f, 0.5f, -2e-4f), EditSettings{})
                  .at(StageId::decode)
                  .gamut.any,
              16u);
}

TEST(ClippingGamut, NoLightWithAPositiveChannelIsALowNotGamut) {
    // Y = 0.2126 * -0.5 + 0.7152 * 0.1 = -0.035 <= 0.
    const ClipReport r = compute_clip_report(solid(4, 4, -0.5f, 0.1f, 0.0f), EditSettings{});
    const prism::gui::StageClip& decode = r.at(StageId::decode);
    EXPECT_EQ(decode.under.any, 16u);
    EXPECT_EQ(decode.under.red, 16u);
    EXPECT_EQ(decode.under.green, 0u);
    EXPECT_EQ(decode.gamut.any, 0u);
    EXPECT_EQ(r.first_gamut, std::nullopt);
}

TEST(ClippingGamut, TheMaskMarksGamutLowsAndHighsIndependently) {
    Image img;
    img.width = 6;
    img.height = 1;
    img.pixels = {0.5f, 0.5f,  0.5f,     // in range
                  0.5f, -0.1f, 0.5f,     // light, one channel negative: gamut
                  0.1f, -0.5f, 0.1f,     // no light: low
                  1.5f, -0.1f, 0.5f,     // high and gamut
                  2.0f, -1.0f, 0.5f,     // high and low (Y < 0)
                  0.5f, 0.5f,  -1e-4f};  // within tolerance: nothing
    std::vector<std::uint8_t> mask;
    prism::gui::compute_clip_mask(img, 1, mask);
    using prism::gui::clip_mask_gamut;
    using prism::gui::clip_mask_high;
    using prism::gui::clip_mask_low;
    EXPECT_EQ(mask, (std::vector<std::uint8_t>{0, clip_mask_gamut, clip_mask_low,
                                               clip_mask_high | clip_mask_gamut,
                                               clip_mask_high | clip_mask_low, 0}));
}

// --- Pixels lost at an earlier stage stay marked
// ---------------------------------------------------

namespace {

// Two pixels wide: an orange the matrix pushes out of gamut, and a grey that never leaves range.
Image orange_and_grey() {
    Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {0.9f, 0.4f, 0.05f, 0.3f, 0.3f, 0.3f};
    return img;
}

std::vector<std::uint8_t> mask_at(const Image& img, const EditSettings& s, StageId stage) {
    prism::gui::PreviewOptions options;
    options.clip_mask = true;
    const auto frame = prism::gui::render_preview(img, s, 1, {}, {}, stage, options);
    EXPECT_TRUE(frame.has_value());
    return frame ? frame->clip_mask : std::vector<std::uint8_t>{};
}

}  // namespace

TEST(ClippingMask, GamutPixelsClippedAtSaturationStayMarkedAtOutputEncode) {
    EditSettings s = orange_matrix();
    s.saturation = 1.5f;
    EXPECT_EQ(mask_at(orange_and_grey(), s, prism::gui::final_stage),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_gamut, 0}));
    // At the stage itself, and before it, too.
    EXPECT_EQ(mask_at(orange_and_grey(), s, StageId::saturation),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_gamut, 0}));
    EXPECT_EQ(mask_at(orange_and_grey(), s, StageId::matrix),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_gamut, 0}));
}

TEST(ClippingMask, WithSaturationOffThePixelsClippedAtTheToneCurveStayMarked) {
    EditSettings s = orange_matrix();
    s.curve = prism::gui::CurveKind::gamma;
    EXPECT_EQ(mask_at(orange_and_grey(), s, prism::gui::final_stage),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_gamut, 0}));
    // Before the curve there is nothing to remember: the negatives are still in the picture.
    EXPECT_EQ(mask_at(orange_and_grey(), s, StageId::matrix),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_gamut, 0}));
}

TEST(ClippingMask, ShadowsClippedAtTheToneCurveStayMarkedAtOutputEncode) {
    // R' = R - 20 G makes (0.1, 0.5, 0.1) negative with Y < 0: no light.
    EditSettings s;
    s.matrix = prism::gui::MatrixFile{{}, {1, -20, 0, 0, 1, 0, 0, 0, 1}};
    s.contrast = 1.2f;
    s.curve = prism::gui::CurveKind::gamma;
    // The second pixel has no green, so the matrix leaves it alone.
    Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {0.1f, 0.5f, 0.1f, 0.2f, 0.0f, 0.3f};
    EXPECT_EQ(mask_at(img, s, prism::gui::final_stage),
              (std::vector<std::uint8_t>{prism::gui::clip_mask_low, 0}));
}

TEST(ClippingMask, PixelsNeverOutOfRangeStayUnmarked) {
    EditSettings s = with_exposure(-1.0f);
    s.saturation = 1.5f;
    s.curve = prism::gui::CurveKind::filmic;
    EXPECT_EQ(mask_at(orange_and_grey(), s, prism::gui::final_stage),
              (std::vector<std::uint8_t>{0, 0}));
}

TEST(ClippingMask, AReportOnlyRenderGivesTheSameReportAndNoMask) {
    EditSettings s = orange_matrix();
    s.saturation = 1.5f;
    s.curve = prism::gui::CurveKind::filmic;
    prism::gui::PreviewOptions options;
    options.clip_report = true;
    const auto frame = prism::gui::render_preview(orange_and_grey(), s, 1, {}, {},
                                                  prism::gui::final_stage, options);
    ASSERT_TRUE(frame && frame->clips);
    EXPECT_TRUE(frame->clip_mask.empty());
    EXPECT_EQ(*frame->clips, compute_clip_report(orange_and_grey(), s, 1));
    // With the mask too, the report is the same.
    options.clip_mask = true;
    const auto both = prism::gui::render_preview(orange_and_grey(), s, 1, {}, {},
                                                 prism::gui::final_stage, options);
    ASSERT_TRUE(both && both->clips);
    EXPECT_EQ(*both->clips, *frame->clips);
}

TEST(ClippingMask, NothingIsRememberedWhenTheMaskIsNotAsked) {
    EditSettings s = orange_matrix();
    s.saturation = 1.5f;
    const auto frame =
        prism::gui::render_preview(orange_and_grey(), s, 1, {}, {}, prism::gui::final_stage, {});
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(frame->clip_mask.empty());
}

TEST(Clipping, TheStageClampTable) {
    using prism::gui::ClipDirection;
    using prism::gui::stage_clamps;
    for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
        const bool lows = info.id == StageId::saturation || info.id == StageId::curve ||
                          info.id == StageId::encode;
        const bool highs = info.id == StageId::decode || info.id == StageId::encode;
        EXPECT_EQ(stage_clamps(info.id, ClipDirection::shadows), lows) << info.name;
        EXPECT_EQ(stage_clamps(info.id, ClipDirection::highlights), highs) << info.name;
    }
}

// --- In a preview frame ------------------------------------------------------------------------

TEST(ClippingPreview, TheFrameCarriesTheReportOnlyWhenAskedAndMatchesThePicture) {
    const Image base = half_and_half(32, 32);
    const EditSettings s = with_exposure(2.0f);

    const auto plain = prism::gui::render_preview(base, s, 1, {});
    ASSERT_TRUE(plain);
    EXPECT_FALSE(plain->clips);
    EXPECT_TRUE(plain->clip_mask.empty());

    prism::gui::PreviewOptions options;
    options.clip_report = true;
    options.clip_mask = true;
    const auto with =
        prism::gui::render_preview(base, s, 1, {}, {}, prism::gui::final_stage, options);
    ASSERT_TRUE(with);
    ASSERT_TRUE(with->clips);
    EXPECT_EQ(*with->clips, compute_clip_report(base, s, 1));
    EXPECT_EQ(with->rgb8, plain->rgb8);
    EXPECT_EQ(with->histogram, plain->histogram);
    ASSERT_EQ(with->clip_mask.size(), 32u * 32u);
    EXPECT_EQ(with->clip_mask.front(), prism::gui::clip_mask_high);
    EXPECT_EQ(with->clip_mask.back(), 0);
}

TEST(ClippingPreview, ViewingAnEarlierStageStillReportsEveryStage) {
    const Image base = half_and_half(32, 32);
    EditSettings s = with_exposure(2.0f);
    s.saturation = 1.2f;
    s.curve = prism::gui::CurveKind::filmic;
    const ClipReport expected = compute_clip_report(base, s, 1);

    prism::gui::PreviewOptions options;
    options.clip_report = true;
    options.clip_mask = true;
    for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
        const auto plain = prism::gui::render_preview(base, s, 1, {}, {}, info.id, {});
        const auto with = prism::gui::render_preview(base, s, 1, {}, {}, info.id, options);
        ASSERT_TRUE(plain && with) << info.name;
        ASSERT_TRUE(with->clips) << info.name;
        EXPECT_EQ(*with->clips, expected) << info.name;
        EXPECT_EQ(with->rgb8, plain->rgb8) << info.name;  // the picture is still the viewed stage
        EXPECT_EQ(with->stage, info.id);
        // The mask is of the viewed stage: above white after exposure, back in range after the
        // curve.
        const std::uint8_t first = with->clip_mask.front();
        if (info.id == StageId::exposure || info.id == StageId::saturation) {
            EXPECT_EQ(first, prism::gui::clip_mask_high) << info.name;
        }
        if (info.id == StageId::curve || info.id == StageId::encode) {
            EXPECT_EQ(first, 0) << info.name;
        }
    }
}

TEST(ClippingPreview, ARequestedStopCancelsTheReportToo) {
    std::stop_source source;
    source.request_stop();
    prism::gui::PreviewOptions options;
    options.clip_report = true;
    EXPECT_FALSE(prism::gui::render_preview(half_and_half(8, 8), with_exposure(1.0f), 1,
                                            source.get_token(), {}, prism::gui::final_stage,
                                            options));
}

TEST(ClippingPreview, LivePreviewAsksAgainOnlyWhenClippingTurnsOn) {
    std::atomic<int> calls{0};
    std::atomic<bool> last_report{false};
    prism::gui::LivePreview preview(prism::gui::PreviewRenderer::OptionsRenderFunction(
        [&](const Image&, const EditSettings&, StageId, const prism::gui::PreviewOptions& o,
            std::stop_token) {
            ++calls;
            last_report = o.clip_report;
            prism::gui::PreviewFrame f;
            f.width = 1;
            f.height = 1;
            f.rgb8 = {0, 0, 0};
            return std::optional<prism::gui::PreviewFrame>(f);
        }));
    const auto base = std::make_shared<const Image>(solid(2, 2, 0.5f, 0.5f, 0.5f));
    const EditSettings s = with_exposure(1.0f);
    const auto next = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto f = preview.poll()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };

    prism::gui::PreviewOptions off;
    prism::gui::PreviewOptions on;
    on.clip_report = true;
    preview.update(base, s, prism::gui::final_stage, off);
    ASSERT_TRUE(next());
    EXPECT_FALSE(last_report.load());
    preview.update(base, s, prism::gui::final_stage, on);
    EXPECT_TRUE(preview.rendering());
    ASSERT_TRUE(next());
    EXPECT_TRUE(last_report.load());
    EXPECT_EQ(calls.load(), 2);
    preview.update(base, s, prism::gui::final_stage, off);  // hidden: the frame is still right
    EXPECT_FALSE(preview.rendering());
    EXPECT_EQ(calls.load(), 2);
}

TEST(ClippingPreview, TheOverlayAloneNeedsNoReport) {
    // The overlay's mask is its own option: it works with the Pipeline section collapsed, where
    // the app asks for no report.
    prism::gui::PreviewOptions options;
    options.clip_mask = true;
    const auto frame = prism::gui::render_preview(half_and_half(8, 8), with_exposure(2.0f), 1, {},
                                                  {}, prism::gui::final_stage, options);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->clips);
    ASSERT_EQ(frame->clip_mask.size(), 64u);
    EXPECT_EQ(frame->clip_mask.front(), prism::gui::clip_mask_high);  // 0.5 * 4 = 2.0
    EXPECT_EQ(frame->clip_mask.back(), 0);
}

TEST(ClippingPreview, TheMaskAtDecodeMarksPixelsAtTheFilesCeiling) {
    Image img = solid(4, 1, 0.5f, 0.5f, 0.5f);
    img.pixels[0] = 1.0f;  // pixel 0: red at the ceiling
    img.pixels[3] = 1.5f;  // pixel 1: over white (a float source)
    prism::gui::PreviewOptions options;
    options.clip_mask = true;
    const auto at_decode =
        prism::gui::render_preview(img, EditSettings{}, 1, {}, {}, StageId::decode, options);
    ASSERT_TRUE(at_decode);
    EXPECT_EQ(at_decode->clip_mask, (std::vector<std::uint8_t>{prism::gui::clip_mask_high,
                                                               prism::gui::clip_mask_high, 0, 0}));
    // Past Decode only values above 1.0 are out of range; the 1.0 is just white.
    const auto at_exposure =
        prism::gui::render_preview(img, with_exposure(0.0f), 1, {}, {}, StageId::exposure, options);
    ASSERT_TRUE(at_exposure);
    EXPECT_EQ(at_exposure->clip_mask,
              (std::vector<std::uint8_t>{0, prism::gui::clip_mask_high, 0, 0}));
}

TEST(ClippingPreview, RunningTheLaterStagesOnTheSampledGridGivesTheFullImagesNumbers) {
    // 1000 x 1000: sampled with stride 2, so the rest of the pipeline runs on a 500 x 500 grid.
    Image img = solid(1000, 1000, 0.0f, 0.0f, 0.0f);
    for (int y = 0; y < 1000; ++y) {
        for (int x = 0; x < 1000; ++x) {
            float* p = &img.pixels[(static_cast<std::size_t>(y) * 1000 + x) * 3];
            p[0] = static_cast<float>(x % 101) / 90.0f;  // some at and above 1.0
            p[1] = static_cast<float>(y % 97) / 120.0f;
            p[2] = static_cast<float>((x + y) % 83) / 100.0f;
        }
    }
    EditSettings s = with_exposure(0.7f);
    s.matrix =
        prism::gui::MatrixFile{{}, {1.4f, -8.0f, 0.0f, -0.2f, 1.2f, 0.0f, 0.0f, -0.4f, 1.3f}};
    s.saturation = 1.4f;
    s.contrast = 1.2f;
    s.curve = prism::gui::CurveKind::filmic;
    const ClipReport expected = compute_clip_report(img, s, 1);
    EXPECT_EQ(expected.stride, 2);
    EXPECT_TRUE(expected.first_over && expected.first_under);
    EXPECT_TRUE(expected.first_gamut && expected.first_gamut_lost);
    EXPECT_NE(expected.at(StageId::matrix).gamut.any, 0u);

    prism::gui::PreviewOptions options;
    options.clip_report = true;
    for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
        const auto frame = prism::gui::render_preview(img, s, 1, {}, {}, info.id, options);
        ASSERT_TRUE(frame && frame->clips) << info.name;
        EXPECT_EQ(*frame->clips, expected) << info.name;
    }
}

// --- One per-pixel range test --------------------------------------------------------------------

namespace {

using prism::gui::clip_mask_bits;
using prism::gui::clip_mask_lost_bits;
using prism::gui::CurveKind;
using prism::gui::PixelRange;
using prism::gui::pixel_range;

// Every kind of pixel the test tells apart, and some ordinary ones around them.
Image range_cases() {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float ceiling = 1.0f - prism::gui::file_ceiling_epsilon;
    Image img;
    img.pixels = {
        0.5f,   0.4f,   0.3f,    // ordinary
        0.0f,   0.0f,   0.0f,    // pure black
        1.0f,   0.5f,   0.5f,    // at the ceiling
        ceiling, ceiling, 1.0f,  // at the ceiling, all three
        1.5f,   0.5f,   0.5f,    // over white
        1.5f,   1.0f,   0.2f,    // over in one channel, at the ceiling in another
        0.9f,   0.4f,   -0.22f,  // has light, out of gamut
        0.9f,   0.4f,   -1e-4f,  // has light, negative within the tolerance
        -0.5f,  0.1f,   0.1f,    // no light, one channel positive
        -0.5f,  -0.1f,  -0.2f,   // no light, all negative
        2.0f,   -1.0f,  0.5f,    // over white and no light
        3.0f,   0.2f,   -0.5f,   // over white and out of gamut
        inf,    0.1f,   0.1f,    // +infinity
        -inf,   0.1f,   0.1f,    // -infinity
        nan,    0.5f,   0.5f,    // NaN
        0.5f,   nan,    -0.5f,   // NaN with a negative
        -0.5f,  nan,    0.5f,    // a negative with NaN
        0.18f,  0.18f,  0.18f,   // middle grey
    };
    img.width = static_cast<int>(img.pixels.size() / 3);
    img.height = 1;
    return img;
}

// The settings that clamp negatives at each place they can: Saturation, the Tone curve, or only
// Output encode; and none that make any.
std::vector<EditSettings> range_settings() {
    EditSettings plain = with_exposure(0.5f);

    EditSettings saturation = with_exposure(0.5f);
    saturation.matrix = prism::gui::MatrixFile{{}, {1, 0, 0, 0, 1, 0, -0.3f, 0, 1}};
    saturation.saturation = 1.2f;
    saturation.contrast = 1.3f;
    saturation.curve = CurveKind::filmic;

    EditSettings curve = saturation;
    curve.saturation_on = false;

    EditSettings encode = curve;
    encode.curve_on = false;

    EditSettings all_off;
    all_off.exposure_on = all_off.matrix_on = all_off.saturation_on = false;
    all_off.contrast_on = all_off.curve_on = false;

    return {plain, saturation, curve, encode, all_off, EditSettings{}};
}

// What the pipeline leaves in the pixels at `stage`, and what its clamping stages lost on the way,
// worked out here with nothing but pixel_range.
std::vector<std::uint8_t> mask_from_pixel_range(const Image& base, const EditSettings& s,
                                                StageId stage) {
    const std::size_t pixels = static_cast<std::size_t>(base.width) * base.height;
    std::vector<std::uint8_t> bits(pixels, 0);
    Image img = base;
    if (stage != StageId::decode) {
        bool clamped = false;
        const prism::BeforeStageCallback before = [&](std::string_view name, const Image& input) {
            const std::optional<StageId> id = prism::gui::stage_from_name(name);
            if (id && !clamped &&
                prism::gui::stage_clamps(*id, prism::gui::ClipDirection::shadows)) {
                clamped = true;
                for (std::size_t i = 0; i < pixels; ++i) {
                    bits[i] |= clip_mask_lost_bits(pixel_range(input.pixels.data() + i * 3));
                }
            }
        };
        (void)prism::process(img, prism::gui::to_process_settings(s, stage), 1, {}, {}, before);
    }
    for (std::size_t i = 0; i < pixels; ++i) {
        bits[i] |= clip_mask_bits(pixel_range(img.pixels.data() + i * 3), stage == StageId::decode);
    }
    return bits;
}

}  // namespace

TEST(PixelRange, NamesEachChannelOutOfRange) {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto of = [](float r, float g, float b) {
        const float p[3] = {r, g, b};
        return pixel_range(p);
    };
    EXPECT_EQ(of(0.5f, 0.5f, 0.5f), PixelRange{});
    EXPECT_EQ(of(0.0f, 0.0f, 0.0f), PixelRange{});  // exactly black is not out of range
    EXPECT_EQ(of(1.5f, 0.5f, 1.2f).over, 0b101);
    EXPECT_EQ(of(1.0f, 0.5f, 0.5f).ceiling, 0b001);
    EXPECT_EQ(of(1.0f, 0.5f, 0.5f).over, 0);
    EXPECT_EQ(of(1.5f, 0.5f, 0.5f).ceiling, 0);  // above the band
    EXPECT_EQ(of(-0.5f, 0.1f, 0.1f).low, 0b001);
    EXPECT_EQ(of(-0.5f, 0.1f, 0.1f).gamut, 0);
    EXPECT_EQ(of(0.9f, 0.4f, -0.22f).gamut, 0b100);
    EXPECT_EQ(of(0.9f, 0.4f, -0.22f).low, 0);
    EXPECT_EQ(of(0.9f, 0.4f, -1e-4f), PixelRange{});  // within half an 8-bit step
    EXPECT_EQ(of(inf, 0.1f, 0.1f).over, 0b001);
    EXPECT_EQ(of(nan, nan, nan), PixelRange{});
    EXPECT_EQ(of(nan, 0.5f, -0.5f), PixelRange{});
}

TEST(PixelRange, MatchesTheClipMaskForEveryPixelAtEveryStage) {
    const Image base = range_cases();
    int settings_index = 0;
    for (const EditSettings& s : range_settings()) {
        for (const prism::gui::StageInfo& info : prism::gui::pipeline_stages()) {
            prism::gui::PreviewOptions options;
            options.clip_mask = true;
            const std::optional<prism::gui::PreviewFrame> frame =
                prism::gui::render_preview(base, s, 1, {}, {}, info.id, options);
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(mask_from_pixel_range(base, s, info.id), frame->clip_mask)
                << "settings " << settings_index << ", " << info.name;
        }
        ++settings_index;
    }
}

TEST(PixelRange, APixelAnEarlierStageClippedStaysMarked) {
    // The orange's blue goes negative at the matrix and Saturation clamps it, so its output is in
    // range; the mask still marks it, and pixel_range of that stage's input says why.
    EditSettings s = orange_matrix();
    s.saturation = 1.2f;
    const Image base = orange();
    const std::optional<Image> after_matrix =
        prism::gui::render_stage_image(base, s, StageId::matrix, 1);
    const std::optional<Image> after_saturation =
        prism::gui::render_stage_image(base, s, StageId::saturation, 1);
    ASSERT_TRUE(after_matrix && after_saturation);
    EXPECT_NE(pixel_range(after_matrix->pixels.data()).gamut, 0);
    EXPECT_EQ(pixel_range(after_saturation->pixels.data()), PixelRange{});

    const auto mask_at = [&](StageId stage) {
        prism::gui::PreviewOptions options;
        options.clip_mask = true;
        return prism::gui::render_preview(base, s, 1, {}, {}, stage, options)->clip_mask;
    };
    for (const StageId stage : {StageId::saturation, StageId::contrast, StageId::curve,
                                StageId::encode}) {
        EXPECT_EQ(mask_at(stage)[0], prism::gui::clip_mask_gamut);
        EXPECT_EQ(mask_from_pixel_range(base, s, stage), mask_at(stage));
    }
}

TEST(PixelRange, MatchesTheReportsCounts) {
    const Image base = range_cases();
    for (const EditSettings& s : range_settings()) {
        const ClipReport report = compute_clip_report(base, s, 1);
        // Exposure, Colour matrix and Contrast never clamp, so their rows count their own output.
        for (const StageId stage : {StageId::exposure, StageId::matrix, StageId::contrast}) {
            const prism::gui::StageClip& row = report.at(stage);
            if (!row.ran) {
                continue;
            }
            const std::optional<Image> img = prism::gui::render_stage_image(base, s, stage, 1);
            ASSERT_TRUE(img.has_value());
            prism::gui::ClipCounts over;
            prism::gui::ClipCounts under;
            prism::gui::ClipCounts gamut;
            const auto add = [](prism::gui::ClipCounts& counts, std::uint8_t channels) {
                counts.red += (channels & 1u) != 0;
                counts.green += (channels & 2u) != 0;
                counts.blue += (channels & 4u) != 0;
                counts.any += channels != 0;
            };
            for (int x = 0; x < base.width; ++x) {
                const PixelRange r =
                    pixel_range(img->pixels.data() + static_cast<std::size_t>(x) * 3);
                add(over, r.over);
                add(under, r.low);
                add(gamut, r.gamut);
            }
            EXPECT_EQ(row.over, over);
            EXPECT_EQ(row.under, under);
            EXPECT_EQ(row.gamut, gamut);
        }
    }
}

TEST(Clipping, AStageNameThatIsNotARowOfThePipelineListIsNotAStage) {
    EXPECT_FALSE(prism::gui::stage_from_name("white_balance"));
    EXPECT_FALSE(prism::gui::stage_from_name(""));
    EXPECT_EQ(prism::gui::stage_from_name("matrix"), StageId::matrix);

    // The engine's white balance stage reports its name to the clip recorder's callback: it is
    // ignored there, the report is the same as without the stage's name, and nothing crashes.
    prism::ProcessSettings settings;
    settings.white_balance = prism::Matrix3{1.1f, 0, 0, 0, 1, 0, 0, 0, 0.9f};
    Image img = half_and_half(8, 8);
    prism::gui::ClipRecorder recorder(img.width, img.height);
    recorder.decoded(img);
    std::vector<std::string> seen;
    (void)prism::process(img, settings, 1, {}, [&](const prism::StageTime& t) {
        seen.emplace_back(t.name);
        if (const auto stage = prism::gui::stage_from_name(t.name)) {
            recorder.stage_done(*stage, img);
        }
    });
    recorder.finish();
    EXPECT_EQ(seen, std::vector<std::string>{"white_balance"});
    for (const auto& row : recorder.report().stages) {
        EXPECT_EQ(row.ran, row.stage == StageId::decode || row.stage == StageId::encode);
    }
}

TEST(Clipping, SettingsAfterTheMatrixDropWhiteBalanceToo) {
    prism::ProcessSettings full;
    full.white_balance = prism::identity_matrix;
    full.matrix = prism::identity_matrix;
    EXPECT_TRUE(prism::gui::process_settings_after(full, StageId::exposure).white_balance);
    EXPECT_FALSE(prism::gui::process_settings_after(full, StageId::matrix).white_balance);
}
