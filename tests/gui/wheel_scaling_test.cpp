#include "prism/gui/wheel_scaling.hpp"

#include <gtest/gtest.h>

using prism::gui::WheelDelta;
using prism::gui::WheelScaler;

namespace {

constexpr float font = 15.0f;

// What Dear ImGui then scrolls vertically, in points, for a delta.
float scrolled_y(WheelDelta d) {
    return d.y * 5.0f * font;
}

TEST(WheelScaling, TrackpadScrollsOnePointPerPointOfTravel) {
    WheelScaler scaler;
    // 12.5 points of travel arrive as 1.25.
    const WheelDelta out = scaler.scale({0.0f, 1.25f}, font, 1000);
    EXPECT_NEAR(scrolled_y(out), 12.5f * prism::gui::trackpad_scroll_speed, 1e-3f);
}

TEST(WheelScaling, SidewaysUsesItsOwnStep) {
    WheelScaler scaler;
    const WheelDelta out = scaler.scale({0.5f, 0.0f}, font, 1000);
    EXPECT_NEAR(out.x * 2.0f * font, 5.0f * prism::gui::trackpad_scroll_speed, 1e-3f);
}

TEST(WheelScaling, WholeNumberMouseWheelPassesUnchanged) {
    WheelScaler scaler;
    const WheelDelta out = scaler.scale({0.0f, -1.0f}, font, 1000);
    EXPECT_EQ(out.x, 0.0f);
    EXPECT_EQ(out.y, -1.0f);
}

TEST(WheelScaling, WholeNumberSoonAfterFractionalIsStillTrackpad) {
    WheelScaler scaler;
    (void)scaler.scale({0.0f, 0.3f}, font, 1000);
    const WheelDelta out = scaler.scale({0.0f, 2.0f}, font, 1000 + 90);
    EXPECT_NEAR(scrolled_y(out), 20.0f * prism::gui::trackpad_scroll_speed, 1e-3f);
}

TEST(WheelScaling, WholeNumberLongAfterFractionalIsAWheel) {
    WheelScaler scaler;
    (void)scaler.scale({0.0f, 0.3f}, font, 1000);
    const WheelDelta out = scaler.scale({0.0f, 2.0f}, font, 1000 + 101);
    EXPECT_EQ(out.y, 2.0f);
}

TEST(WheelScaling, EachFractionalEventExtendsTheMemory) {
    WheelScaler scaler;
    (void)scaler.scale({0.0f, 0.3f}, font, 1000);
    (void)scaler.scale({0.0f, 0.4f}, font, 1080);
    const WheelDelta out = scaler.scale({0.0f, 1.0f}, font, 1160);  // 80 ms after the last one
    EXPECT_LT(out.y, 1.0f);
}

TEST(WheelScaling, ScalingFollowsTheFontSize) {
    // Same finger travel, same scroll in points, whatever the interface size.
    WheelScaler small;
    WheelScaler big;
    const WheelDelta a = small.scale({0.0f, 0.5f}, 15.0f, 1000);
    const WheelDelta b = big.scale({0.0f, 0.5f}, 30.0f, 1000);
    EXPECT_NEAR(a.y * 5.0f * 15.0f, b.y * 5.0f * 30.0f, 1e-3f);
}

TEST(WheelScaling, NoFontSizeYetPassesUnchanged) {
    WheelScaler scaler;
    const WheelDelta out = scaler.scale({0.0f, 0.5f}, 0.0f, 1000);
    EXPECT_EQ(out.y, 0.5f);
}

TEST(WheelScaling, AWheelBeforeAnyTrackpadEventStaysAWheel) {
    WheelScaler scaler;
    EXPECT_EQ(scaler.scale({0.0f, 3.0f}, font, 5).y, 3.0f);
}

}  // namespace
