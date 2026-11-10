#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "prism/camera_profile.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/neutral_pick.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/image.hpp"
#include "prism/white_balance.hpp"

namespace {

using prism::Chromaticity;
using prism::Image;
using prism::WhiteBalance;
using namespace prism::gui;

Image image_of(int width, int height, const std::vector<std::array<float, 3>>& pixels) {
    Image img;
    img.width = width;
    img.height = height;
    for (const auto& p : pixels) {
        img.pixels.insert(img.pixels.end(), p.begin(), p.end());
    }
    return img;
}

// A width x height picture whose pixel (x, y) is (x, y, x + y) / 100: every pixel different, so a
// patch's mean says exactly which pixels it took.
Image ramp(int width, int height) {
    std::vector<std::array<float, 3>> pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels.push_back({x / 100.0f, y / 100.0f, (x + y) / 100.0f});
        }
    }
    return image_of(width, height, pixels);
}

// What a pixel (x, y) of ramp() is.
std::array<double, 3> ramp_pixel(int x, int y) {
    return {x / 100.0, y / 100.0, (x + y) / 100.0};
}

// The mean of the ramp over the pixels x0..x1, y0..y1, worked out the long way.
std::array<double, 3> ramp_mean(int x0, int y0, int x1, int y1) {
    std::array<double, 3> sum{};
    int n = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] += ramp_pixel(x, y)[static_cast<std::size_t>(c)];
            }
            ++n;
        }
    }
    for (double& v : sum) {
        v /= n;
    }
    return sum;
}

void expect_mean(const Image& img, int x, int y, std::array<double, 3> want, int pixels) {
    const auto patch = pick_patch(img, x, y);
    ASSERT_TRUE(patch) << x << "," << y;
    EXPECT_EQ(patch->pixels, pixels) << x << "," << y;
    for (std::size_t c = 0; c < 3; ++c) {
        EXPECT_NEAR(patch->mean[c], want[c], 1e-6) << x << "," << y << " channel " << c;
    }
}

// The colour a grey of level `grey` has on a photo whose white balance chosen is `chosen`, as the
// picker sees it: the chosen light's matrix undone. Applying that matrix to it gives the grey back.
std::array<float, 3> colour_lit_by(const WhiteBalance& chosen, Chromaticity as_shot, double grey) {
    const prism::Matrix3 m = prism::white_balance_matrix(chosen, as_shot);
    const prism::Mat3 forward = {{{m[0], m[1], m[2]}, {m[3], m[4], m[5]}, {m[6], m[7], m[8]}}};
    const prism::Vec3 c = prism::multiply(prism::invert(forward), prism::Vec3{grey, grey, grey});
    return {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
}

std::array<double, 3> through_stage(const WhiteBalance& chosen, Chromaticity as_shot,
                                    const std::array<float, 3>& c) {
    const prism::Matrix3 m = prism::white_balance_matrix(chosen, as_shot);
    return {m[0] * c[0] + m[1] * c[1] + m[2] * c[2], m[3] * c[0] + m[4] * c[1] + m[5] * c[2],
            m[6] * c[0] + m[7] * c[1] + m[8] * c[2]};
}

}  // namespace

// --- The patch --------------------------------------------------------------------------------

TEST(NeutralPickPatch, BoundsAreFiveAcrossAndCutAtTheEdges) {
    EXPECT_EQ(pick_patch_bounds(20, 10, 10, 5), (PatchBounds{8, 3, 12, 7}));
    EXPECT_EQ(pick_patch_bounds(20, 10, 0, 0), (PatchBounds{0, 0, 2, 2}));
    EXPECT_EQ(pick_patch_bounds(20, 10, 19, 9), (PatchBounds{17, 7, 19, 9}));
    EXPECT_EQ(pick_patch_bounds(20, 10, 0, 9), (PatchBounds{0, 7, 2, 9}));
    EXPECT_EQ(pick_patch_bounds(20, 10, 1, 5), (PatchBounds{0, 3, 3, 7}));
    EXPECT_EQ(pick_patch_bounds(1, 1, 0, 0), (PatchBounds{0, 0, 0, 0}));
    EXPECT_FALSE(pick_patch_bounds(20, 10, -1, 0));
    EXPECT_FALSE(pick_patch_bounds(20, 10, 20, 0));
    EXPECT_FALSE(pick_patch_bounds(20, 10, 0, 10));
    EXPECT_FALSE(pick_patch_bounds(0, 0, 0, 0));
}

TEST(NeutralPickPatch, MeanOfFiveByFiveAtTheCentreAndAroundTheEdges) {
    const Image img = ramp(11, 9);
    expect_mean(img, 5, 4, ramp_mean(3, 2, 7, 6), 25);
    // Corners: a 3x3 corner of the patch is all that is left.
    expect_mean(img, 0, 0, ramp_mean(0, 0, 2, 2), 9);
    expect_mean(img, 10, 0, ramp_mean(8, 0, 10, 2), 9);
    expect_mean(img, 0, 8, ramp_mean(0, 6, 2, 8), 9);
    expect_mean(img, 10, 8, ramp_mean(8, 6, 10, 8), 9);
    // Edges: 3 across the edge, 5 along it.
    expect_mean(img, 5, 0, ramp_mean(3, 0, 7, 2), 15);
    expect_mean(img, 5, 8, ramp_mean(3, 6, 7, 8), 15);
    expect_mean(img, 0, 4, ramp_mean(0, 2, 2, 6), 15);
    expect_mean(img, 10, 4, ramp_mean(8, 2, 10, 6), 15);
    // One pixel in from the edge: 4 across it.
    expect_mean(img, 1, 4, ramp_mean(0, 2, 3, 6), 20);
}

TEST(NeutralPickPatch, SmallImagesAreTakenWhole) {
    const Image one = image_of(1, 1, {{0.2f, 0.3f, 0.4f}});
    expect_mean(one, 0, 0, {0.2, 0.3, 0.4}, 1);
    const Image three_by_two = ramp(3, 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            // The patch reaches 2 each way, so every pixel's covers the whole picture.
            expect_mean(three_by_two, x, y, ramp_mean(0, 0, 2, 1), 6);
        }
    }
    EXPECT_FALSE(pick_patch(three_by_two, 3, 0));
    EXPECT_FALSE(pick_patch(three_by_two, 0, 2));
    EXPECT_FALSE(pick_patch(three_by_two, -1, 0));
}

TEST(NeutralPickPatch, AnImageWhosePixelsDoNotMatchItsSizeIsRefused) {
    Image img = ramp(4, 4);
    img.pixels.pop_back();
    EXPECT_FALSE(pick_patch(img, 1, 1));
    EXPECT_FALSE(pick_patch(Image{}, 0, 0));
}

TEST(NeutralPickPatch, ClippedMeansAnyPixelAtTheCeilingOrAboveWhite) {
    Image img = image_of(5, 5, std::vector<std::array<float, 3>>(25, {0.3f, 0.3f, 0.3f}));
    EXPECT_FALSE(pick_patch(img, 2, 2)->clipped);
    img.pixels[(4 * 5 + 4) * 3 + 1] = 1.0f - file_ceiling_epsilon;  // a corner of the patch
    EXPECT_TRUE(pick_patch(img, 2, 2)->clipped);
    img.pixels[(4 * 5 + 4) * 3 + 1] = 1.5f;
    EXPECT_TRUE(pick_patch(img, 2, 2)->clipped);
    img.pixels[(4 * 5 + 4) * 3 + 1] = 0.999f;  // under the ceiling: not clipped
    EXPECT_FALSE(pick_patch(img, 2, 2)->clipped);
    // A clipped pixel outside the patch does not count.
    Image wide = image_of(9, 1, std::vector<std::array<float, 3>>(9, {0.3f, 0.3f, 0.3f}));
    wide.pixels[8 * 3] = 1.0f;
    EXPECT_FALSE(pick_patch(wide, 4, 0)->clipped);
    EXPECT_TRUE(pick_patch(wide, 6, 0)->clipped);
}

// --- The light --------------------------------------------------------------------------------

TEST(NeutralPickLight, ThePickedLightMakesTheClickedColourGrey) {
    // c is what a grey looks like once lit by L; the picker must name L, and L's stage must turn c
    // back into a grey. As shot is a JPEG's 6500 K, or a RAW camera's own light.
    const std::vector<std::optional<Chromaticity>> as_shots = {
        std::nullopt,
        prism::white_of(WhiteBalance{5330, 27}),
        prism::white_of(WhiteBalance{3200, -15}),
    };
    const std::vector<WhiteBalance> lights = {{3000, -20}, {5200, 0}, {9000, 40}, {6500, 0},
                                              {4100, 60}};
    for (const std::optional<Chromaticity>& as_shot : as_shots) {
        const Chromaticity base = as_shot.value_or(prism::white_of(prism::default_as_shot));
        for (const WhiteBalance& chosen : lights) {
            const std::array<float, 3> c = colour_lit_by(chosen, base, 0.3);
            const std::string label = std::to_string(chosen.kelvin) + "/" +
                                      std::to_string(chosen.tint) + " as shot " +
                                      (as_shot ? "RAW" : "JPEG");
            for (const float v : c) {
                ASSERT_GT(v, 0.01f) << label;
                ASSERT_LT(v, 0.99f) << label;
            }
            const Image img = image_of(1, 1, {c});
            const NeutralPick pick = pick_neutral(img, 0, 0, as_shot);
            ASSERT_EQ(pick.outcome, PickOutcome::picked) << label;
            EXPECT_FALSE(pick.limited) << label;
            EXPECT_NEAR(pick.light.kelvin, chosen.kelvin, 10.0) << label;
            EXPECT_NEAR(pick.light.tint, chosen.tint, 1.0) << label;
            // Rounded the way the sliders round.
            EXPECT_EQ(pick.light.kelvin, snap_kelvin(pick.light.kelvin)) << label;
            EXPECT_EQ(pick.light.tint, snap_tint(pick.light.tint)) << label;
            // And the stage that picked light makes is grey, after rounding.
            const std::array<double, 3> out = through_stage(pick.light, base, c);
            const double high = *std::max_element(out.begin(), out.end());
            const double low = *std::min_element(out.begin(), out.end());
            EXPECT_LT((high - low) / ((out[0] + out[1] + out[2]) / 3), 0.005) << label;
        }
    }
}

TEST(NeutralPickLight, ALightThatChangesNothingIsAlreadyNeutral) {
    // A grey on a photo left as shot is already neutral: the light that keeps it so is the as-shot one.
    const WhiteBalance as_shot{5330, 27};
    const Image img = image_of(1, 1, {{0.3f, 0.3f, 0.3f}});
    const NeutralPick pick = pick_neutral(img, 0, 0, prism::white_of(as_shot));
    ASSERT_EQ(pick.outcome, PickOutcome::picked);
    EXPECT_NEAR(pick.light.kelvin, as_shot.kelvin, 10.0);
    EXPECT_NEAR(pick.light.tint, as_shot.tint, 1.0);
    const NeutralPick jpeg = pick_neutral(img, 0, 0, std::nullopt);
    EXPECT_NEAR(jpeg.light.kelvin, 6500.0, 10.0);
    EXPECT_NEAR(jpeg.light.tint, 0.0, 3.0);
}

TEST(NeutralPickLight, AsShotAdaptationMatters) {
    // The same colour names different lights on photos with different as-shot lights.
    const Image img = image_of(1, 1, {{0.4f, 0.3f, 0.2f}});
    const NeutralPick jpeg = pick_neutral(img, 0, 0, std::nullopt);
    const NeutralPick raw = pick_neutral(img, 0, 0, prism::white_of(WhiteBalance{3200, 0}));
    ASSERT_EQ(jpeg.outcome, PickOutcome::picked);
    ASSERT_EQ(raw.outcome, PickOutcome::picked);
    EXPECT_GT(std::fabs(jpeg.light.kelvin - raw.light.kelvin), 500.0);
}

TEST(NeutralPickLight, ThePatchIsAveragedNotTheCentrePixel) {
    // A neutral grey with a warm speck in the middle: the centre pixel alone would name another light.
    std::vector<std::array<float, 3>> pixels(25, {0.3f, 0.3f, 0.3f});
    pixels[12] = {0.6f, 0.3f, 0.1f};
    const Image img = image_of(5, 5, pixels);
    const NeutralPick patch = pick_neutral(img, 2, 2, std::nullopt);
    ASSERT_EQ(patch.outcome, PickOutcome::picked);
    const NeutralPick speck = pick_neutral(image_of(1, 1, {pixels[12]}), 0, 0, std::nullopt);
    ASSERT_EQ(speck.outcome, PickOutcome::picked);
    EXPECT_GT(std::fabs(patch.light.kelvin - speck.light.kelvin), 1000.0);
    // The mean of the patch is (0.3 + 0.3/25 * 1, ...): worked out by hand.
    const std::array<double, 3> mean = {0.3 + 0.3 / 25, 0.3, 0.3 - 0.2 / 25};
    const Chromaticity xy = *neutral_light_xy(mean, prism::white_of(prism::default_as_shot));
    const WhiteBalance want = prism::white_balance_of(xy);
    EXPECT_NEAR(patch.light.kelvin, want.kelvin, 5.0);
    EXPECT_NEAR(patch.light.tint, want.tint, 0.5);
}

TEST(NeutralPickLight, RepeatedPicksAreStable) {
    // The patch is of the decoded base, so picking again after the stage is on changes nothing.
    const Image img = image_of(1, 1, {colour_lit_by({4200, 12}, prism::white_of(prism::default_as_shot), 0.3)});
    const NeutralPick first = pick_neutral(img, 0, 0, std::nullopt);
    const NeutralPick second = pick_neutral(img, 0, 0, std::nullopt);
    EXPECT_EQ(first.light, second.light);
}

// --- Rejections -------------------------------------------------------------------------------

TEST(NeutralPickReject, TooDark) {
    for (const std::array<float, 3> c : {std::array<float, 3>{0.0f, 0.3f, 0.3f},
                                         {0.3f, 0.0f, 0.3f},
                                         {0.3f, 0.3f, -0.1f},
                                         {0.004f, 0.3f, 0.3f},
                                         {0.3f, 0.3f, 0.0049f},
                                         {0.0f, 0.0f, 0.0f}}) {
        const NeutralPick pick = pick_neutral(image_of(1, 1, {c}), 0, 0, std::nullopt);
        EXPECT_EQ(pick.outcome, PickOutcome::too_dark);
        EXPECT_EQ(pick_message(pick), "Too dark to judge; click a lighter grey");
    }
    // Just over 0.5% of white in every channel is judged.
    EXPECT_EQ(pick_neutral(image_of(1, 1, {{0.0051f, 0.0051f, 0.0051f}}), 0, 0, std::nullopt).outcome,
              PickOutcome::picked);
}

TEST(NeutralPickReject, ClippedAndAtTheCeiling) {
    for (const float v : {1.0f, 1.0f - 0.5e-6f, 1.2f, 40.0f}) {
        const NeutralPick pick = pick_neutral(image_of(1, 1, {{v, 0.5f, 0.5f}}), 0, 0, std::nullopt);
        EXPECT_EQ(pick.outcome, PickOutcome::clipped) << v;
        EXPECT_EQ(pick_message(pick), "Clipped; click a grey that isn't blown out");
    }
    // One clipped pixel among the patch's 25 is enough, though the mean is fine.
    std::vector<std::array<float, 3>> pixels(25, {0.4f, 0.4f, 0.4f});
    pixels[0] = {1.0f, 1.0f, 1.0f};
    EXPECT_EQ(pick_neutral(image_of(5, 5, pixels), 2, 2, std::nullopt).outcome, PickOutcome::clipped);
}

TEST(NeutralPickReject, OffThePictureHasNoWords) {
    const NeutralPick pick = pick_neutral(image_of(1, 1, {{0.3f, 0.3f, 0.3f}}), 1, 0, std::nullopt);
    EXPECT_EQ(pick.outcome, PickOutcome::unavailable);
    EXPECT_EQ(pick_message(pick), "");
}

TEST(NeutralPickReject, ApplyingARejectionChangesNothing) {
    EditSettings s;
    const EditSettings before = s;
    NeutralPick dark;
    dark.outcome = PickOutcome::too_dark;
    apply_pick(s, dark);
    NeutralPick clipped;
    clipped.outcome = PickOutcome::clipped;
    apply_pick(s, clipped);
    EXPECT_EQ(s, before);
}

// --- Clamping and the chip --------------------------------------------------------------------

TEST(NeutralPickClamp, LightsBeyondTheSlidersAreClampedAndSaid) {
    const Chromaticity base = prism::white_of(prism::default_as_shot);
    struct Case {
        WhiteBalance light;
        double kelvin;
        double tint;
    };
    // Too warm, too blue (the search ends at 15000 K), too green, too magenta.
    const std::vector<Case> cases = {{{1800, 0}, 2000, NAN},  // as a colour: this light is outside sRGB
                                     {{15000, 0}, 15000, 0},
                                     {{30000, 0}, 15000, NAN},
                                     {{5000, 150}, 5000, 100},
                                     {{5000, -150}, 5000, -100}};
    for (const Case& want : cases) {
        // The warm end as a very orange (but inside sRGB) colour; the rest as a grey lit by the light.
        const std::array<float, 3> c = want.light.kelvin == 1800
                                           ? std::array<float, 3>{0.8f, 0.2f, 0.02f}
                                           : colour_lit_by(want.light, base, 0.3);
        const NeutralPick pick = pick_neutral(image_of(1, 1, {c}), 0, 0, std::nullopt);
        ASSERT_EQ(pick.outcome, PickOutcome::picked) << want.light.kelvin << "/" << want.light.tint;
        EXPECT_EQ(pick.light.kelvin, want.kelvin) << want.light.kelvin << "/" << want.light.tint;
        // Past the end of the locus the tint is whatever is left of the offset; only its range is known.
        if (std::isnan(want.tint)) {
            EXPECT_LE(std::fabs(pick.light.tint), 100.0);
        } else {
            EXPECT_EQ(pick.light.tint, want.tint) << want.light.kelvin << "/" << want.light.tint;
        }
        const bool beyond = want.light.kelvin != 15000;  // exactly the end is not beyond it
        EXPECT_EQ(pick.limited, beyond) << want.light.kelvin << "/" << want.light.tint;
        EXPECT_EQ(pick_message(pick).find("(limited to the slider's range)") != std::string::npos,
                  beyond);
    }
}

TEST(NeutralPickChip, SaysTheLightInTheRowsWords) {
    NeutralPick pick;
    pick.outcome = PickOutcome::picked;
    pick.light = {5180, 4};
    EXPECT_EQ(pick_message(pick), "Neutral here: 5180 K, tint +4");
    pick.light = {5180, -10};
    EXPECT_EQ(pick_message(pick), "Neutral here: 5180 K, tint \xE2\x88\x92" "10");
    pick.light = {2000, 0};
    pick.limited = true;
    EXPECT_EQ(pick_message(pick), "Neutral here: 2000 K, tint 0 (limited to the slider's range)");
}

TEST(NeutralPickApply, ChoosesTheLightAndTurnsTheStageOn) {
    EditSettings s;
    s.white_balance_on = false;
    NeutralPick pick;
    pick.outcome = PickOutcome::picked;
    pick.light = {5180, 4};
    apply_pick(s, pick);
    ASSERT_TRUE(s.white_balance);
    EXPECT_EQ(*s.white_balance, (WhiteBalance{5180, 4}));
    EXPECT_TRUE(s.white_balance_on);
}
