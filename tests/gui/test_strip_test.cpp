#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "prism/gui/test_strip.hpp"

using namespace prism::gui;

namespace {

constexpr StripStep all_steps[] = {StripStep::third, StripStep::half, StripStep::whole};

}  // namespace

TEST(TestStrip, StepSizes) {
    EXPECT_EQ(strip_step_divisor(StripStep::third), 3);
    EXPECT_EQ(strip_step_divisor(StripStep::half), 2);
    EXPECT_EQ(strip_step_divisor(StripStep::whole), 1);
    EXPECT_FLOAT_EQ(strip_step_stops(StripStep::third), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(strip_step_stops(StripStep::half), 0.5f);
    EXPECT_FLOAT_EQ(strip_step_stops(StripStep::whole), 1.0f);
    EXPECT_EQ(strip_default_step, StripStep::half);
    EXPECT_EQ(strip_default_count, 5);
}

TEST(TestStrip, LandscapeBandsAreVerticalPortraitHorizontal) {
    const auto land = strip_bands(100, 40, 5);
    ASSERT_EQ(land.size(), 5u);
    EXPECT_EQ(land[0], (StripRect{0, 0, 20, 40}));
    EXPECT_EQ(land[4], (StripRect{80, 0, 20, 40}));
    const auto port = strip_bands(40, 100, 5);
    EXPECT_EQ(port[1], (StripRect{0, 20, 40, 20}));
    EXPECT_FALSE(strip_vertical(40, 100));
    EXPECT_TRUE(strip_vertical(50, 50));  // a square counts as landscape
    EXPECT_EQ(strip_bands(50, 50, 5)[2], (StripRect{20, 0, 10, 50}));
}

TEST(TestStrip, BandsCoverEveryPixelExactlyOnce) {
    const int sizes[][2] = {{1, 1},   {2, 1},   {7, 3},   {3, 7},    {101, 37}, {37, 101},
                            {2559, 1707}, {1707, 2559}, {4, 4}, {5, 1}, {1, 5}, {3, 2}};
    for (const auto& sz : sizes) {
        for (int count : {1, 2, 3, 5, 7, 9}) {
            const int w = sz[0];
            const int h = sz[1];
            const auto bands = strip_bands(w, h, count);
            ASSERT_EQ(static_cast<int>(bands.size()), count);
            std::vector<int> hits(static_cast<std::size_t>(w) * h, 0);
            for (const StripRect& b : bands) {
                ASSERT_GE(b.width, 0);
                ASSERT_GE(b.height, 0);
                ASSERT_GE(b.x, 0);
                ASSERT_GE(b.y, 0);
                ASSERT_LE(b.x + b.width, w);
                ASSERT_LE(b.y + b.height, h);
                for (int y = b.y; y < b.y + b.height; ++y) {
                    for (int x = b.x; x < b.x + b.width; ++x) {
                        ++hits[static_cast<std::size_t>(y) * w + x];
                    }
                }
            }
            for (int v : hits) {
                ASSERT_EQ(v, 1) << w << "x" << h << " bands " << count;
            }
            // Widths along the long side differ by at most one pixel.
            int lo = 1 << 30;
            int hi = 0;
            for (const StripRect& b : bands) {
                const int len = strip_vertical(w, h) ? b.width : b.height;
                lo = std::min(lo, len);
                hi = std::max(hi, len);
            }
            EXPECT_LE(hi - lo, 1);
        }
    }
}

TEST(TestStrip, DegenerateInputsGiveNoBands) {
    EXPECT_TRUE(strip_bands(0, 10, 5).empty());
    EXPECT_TRUE(strip_bands(10, 0, 5).empty());
    EXPECT_TRUE(strip_bands(10, 10, 0).empty());
    EXPECT_TRUE(strip_bands(10, 10, -1).empty());
}

TEST(TestStrip, OffsetsForEachStep) {
    EXPECT_EQ(strip_middle(5), 2);
    const float half[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    const float whole[] = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};
    const float third[] = {-2.0f / 3, -1.0f / 3, 0.0f, 1.0f / 3, 2.0f / 3};
    for (int i = 0; i < 5; ++i) {
        EXPECT_FLOAT_EQ(strip_offset(StripStep::half, 5, i), half[i]);
        EXPECT_FLOAT_EQ(strip_offset(StripStep::whole, 5, i), whole[i]);
        EXPECT_FLOAT_EQ(strip_offset(StripStep::third, 5, i), third[i]);
    }
    EXPECT_EQ(strip_offset(StripStep::third, 5, 2), 0.0f);
    EXPECT_FLOAT_EQ(strip_light_factor(1.0f), 2.0f);
    EXPECT_FLOAT_EQ(strip_light_factor(-1.0f), 0.5f);
    EXPECT_NEAR(strip_light_factor(0.5f), 1.41421f, 1e-4f);
}

TEST(TestStrip, LabelsForHalfStops) {
    EXPECT_EQ(strip_label(StripStep::half, 5, 0), "−1 stop · ½× light");
    EXPECT_EQ(strip_label(StripStep::half, 5, 1), "−½ · 0.71×");
    EXPECT_EQ(strip_label(StripStep::half, 5, 2), "0 · now");
    EXPECT_EQ(strip_label(StripStep::half, 5, 3), "+½ · 1.41×");
    EXPECT_EQ(strip_label(StripStep::half, 5, 4), "+1 · 2×");
}

TEST(TestStrip, LabelsForThirdAndWholeStops) {
    EXPECT_EQ(strip_label(StripStep::third, 5, 0), "−⅔ stop · 0.63× light");
    EXPECT_EQ(strip_label(StripStep::third, 5, 1), "−⅓ · 0.79×");
    EXPECT_EQ(strip_label(StripStep::third, 5, 3), "+⅓ · 1.26×");
    EXPECT_EQ(strip_label(StripStep::third, 5, 4), "+⅔ · 1.59×");
    EXPECT_EQ(strip_label(StripStep::whole, 5, 0), "−2 stops · ¼× light");
    EXPECT_EQ(strip_label(StripStep::whole, 5, 1), "−1 · ½×");
    EXPECT_EQ(strip_label(StripStep::whole, 5, 3), "+1 · 2×");
    EXPECT_EQ(strip_label(StripStep::whole, 5, 4), "+2 · 4×");
}

TEST(TestStrip, LabelsForOtherCounts) {
    // Seven bands of thirds reach 1 stop and 1⅓: mixed numbers.
    EXPECT_EQ(strip_label(StripStep::third, 9, 0), "−1⅓ stops · 0.40× light");
    EXPECT_EQ(strip_label(StripStep::third, 9, 1), "−1 · ½×");
    EXPECT_EQ(strip_label(StripStep::half, 7, 8), "");
    EXPECT_EQ(strip_label(StripStep::half, 5, -1), "");
    EXPECT_EQ(strip_label(StripStep::half, 5, 5), "");
    EXPECT_EQ(strip_label(StripStep::half, 1, 0), "0 · now");
}

TEST(TestStrip, EveryLabelIsDistinctPerStep) {
    for (StripStep step : all_steps) {
        std::vector<std::string> seen;
        for (int i = 0; i < 5; ++i) {
            const std::string label = strip_label(step, 5, i);
            EXPECT_FALSE(label.empty());
            for (const std::string& s : seen) {
                EXPECT_NE(label, s);
            }
            seen.push_back(label);
        }
    }
}

TEST(TestStrip, BandAtPoint) {
    EXPECT_EQ(strip_band_at(100, 40, 5, 0.0f, 0.0f), 0);
    EXPECT_EQ(strip_band_at(100, 40, 5, 19.9f, 39.0f), 0);
    EXPECT_EQ(strip_band_at(100, 40, 5, 20.0f, 10.0f), 1);
    EXPECT_EQ(strip_band_at(100, 40, 5, 50.0f, 10.0f), 2);
    EXPECT_EQ(strip_band_at(100, 40, 5, 99.9f, 10.0f), 4);
    EXPECT_EQ(strip_band_at(40, 100, 5, 10.0f, 45.0f), 2);  // portrait: by y
    EXPECT_EQ(strip_band_at(40, 100, 5, 39.0f, 99.0f), 4);
    EXPECT_FALSE(strip_band_at(100, 40, 5, 100.0f, 10.0f));
    EXPECT_FALSE(strip_band_at(100, 40, 5, -0.5f, 10.0f));
    EXPECT_FALSE(strip_band_at(100, 40, 5, 10.0f, 40.0f));
    EXPECT_FALSE(strip_band_at(100, 40, 5, std::nanf(""), 10.0f));
    EXPECT_FALSE(strip_band_at(0, 0, 5, 0.0f, 0.0f));
}

TEST(TestStrip, BandAtAgreesWithBandsOnOddSizes) {
    for (const auto& sz : {std::pair{101, 37}, std::pair{37, 101}, std::pair{7, 3}, std::pair{3, 2}}) {
        const auto bands = strip_bands(sz.first, sz.second, 5);
        for (int y = 0; y < sz.second; ++y) {
            for (int x = 0; x < sz.first; ++x) {
                const auto band = strip_band_at(sz.first, sz.second, 5, static_cast<float>(x) + 0.5f,
                                                static_cast<float>(y) + 0.5f);
                ASSERT_TRUE(band);
                const StripRect& b = bands[static_cast<std::size_t>(*band)];
                ASSERT_TRUE(x >= b.x && x < b.x + b.width && y >= b.y && y < b.y + b.height);
            }
        }
    }
}

TEST(TestStrip, ApplyBandSetsExposureAroundTheCurrentValue) {
    EditSettings s;
    s.exposure_stops = 0.8f;
    EXPECT_TRUE(apply_strip_band(s, StripStep::half, 5, 4));
    EXPECT_FLOAT_EQ(s.exposure_stops, 1.8f);
    EXPECT_TRUE(s.exposure_on);
    EXPECT_TRUE(apply_strip_band(s, StripStep::half, 5, 1));  // the strip is now centred on 1.8
    EXPECT_FLOAT_EQ(s.exposure_stops, 1.3f);
    EXPECT_TRUE(apply_strip_band(s, StripStep::third, 5, 0));
    EXPECT_FLOAT_EQ(s.exposure_stops, 1.3f - 2.0f / 3.0f);
}

TEST(TestStrip, ApplyMiddleBandChangesNothing) {
    EditSettings s;
    s.exposure_stops = 0.8f;
    const EditSettings before = s;
    EXPECT_FALSE(apply_strip_band(s, StripStep::half, 5, 2));
    EXPECT_EQ(s, before);
    s.exposure_on = false;  // even with the stage off, "now" does not turn it on
    const EditSettings off = s;
    EXPECT_FALSE(apply_strip_band(s, StripStep::half, 5, 2));
    EXPECT_EQ(s, off);
}

TEST(TestStrip, ApplyBandTurnsExposureOnFromZero) {
    EditSettings s;
    s.exposure_stops = 2.0f;  // the stored value is ignored while the stage is off
    s.exposure_on = false;
    EXPECT_FLOAT_EQ(strip_centre_stops(s), 0.0f);
    EXPECT_TRUE(apply_strip_band(s, StripStep::half, 5, 3));
    EXPECT_TRUE(s.exposure_on);
    EXPECT_FLOAT_EQ(s.exposure_stops, 0.5f);
}

TEST(TestStrip, ApplyBandLeavesOtherSettingsAlone) {
    EditSettings s;
    s.contrast = 1.4f;
    s.saturation = 0.7f;
    s.curve = CurveKind::gamma;
    EditSettings expected = s;
    expected.exposure_stops = -1.0f;
    EXPECT_TRUE(apply_strip_band(s, StripStep::half, 5, 0));
    EXPECT_EQ(s, expected);
}

TEST(TestStrip, ApplyBandKeepsValuesBeyondTheSlider) {
    EditSettings s;
    s.exposure_stops = 2.0f;  // the slider ends at +2
    EXPECT_TRUE(apply_strip_band(s, StripStep::whole, 5, 4));
    EXPECT_FLOAT_EQ(s.exposure_stops, 4.0f);  // not clamped back to 2
    EXPECT_TRUE(apply_strip_band(s, StripStep::half, 5, 4));
    EXPECT_FLOAT_EQ(s.exposure_stops, 5.0f);
    s.exposure_stops = -2.0f;
    EXPECT_TRUE(apply_strip_band(s, StripStep::whole, 5, 0));
    EXPECT_FLOAT_EQ(s.exposure_stops, -4.0f);
}

TEST(TestStrip, ApplyBandRejectsBandsOutsideTheStrip) {
    EditSettings s;
    EXPECT_FALSE(apply_strip_band(s, StripStep::half, 5, -1));
    EXPECT_FALSE(apply_strip_band(s, StripStep::half, 5, 5));
    EXPECT_EQ(s, EditSettings{});
}

TEST(TestStrip, BandSettingsDifferOnlyInExposure) {
    EditSettings s;
    s.exposure_stops = 0.25f;
    s.contrast = 2.0f;
    for (int i = 0; i < 5; ++i) {
        EditSettings expected = s;
        expected.exposure_stops = 0.25f + strip_offset(StripStep::half, 5, i);
        EXPECT_EQ(strip_band_settings(s, StripStep::half, 5, i), expected);
    }
    EXPECT_EQ(strip_band_settings(s, StripStep::half, 5, 2), s);  // the middle band is "now"
}

TEST(TestStrip, AvailableFromExposureOn) {
    EXPECT_FALSE(strip_available(StageId::decode));
    for (StageId s : {StageId::exposure, StageId::matrix, StageId::saturation, StageId::contrast,
                      StageId::curve, StageId::encode}) {
        EXPECT_TRUE(strip_available(s));
    }
}

TEST(TestStrip, ClickingBandsInTurnReCentresTheStrip) {
    // The strip stays on and is centred on the new value, so the same band offset moves again.
    EditSettings s;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(apply_strip_band(s, StripStep::half, 5, 4));
    }
    EXPECT_FLOAT_EQ(s.exposure_stops, 3.0f);
    EXPECT_EQ(strip_label(StripStep::half, 5, strip_middle(5)), "0 · now");
    EXPECT_FLOAT_EQ(strip_centre_stops(s), 3.0f);
}
