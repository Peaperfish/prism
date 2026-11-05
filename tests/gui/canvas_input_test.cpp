#include "prism/gui/canvas_input.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace prism::gui {
namespace {

constexpr float unit = 10.0f;

TEST(CanvasScroll, ATrackpadPans) {
    EXPECT_EQ(canvas_scroll({0.0f, 1.5f}, true, false, unit).kind, CanvasScrollKind::pan);
}

// The numbers as SDL's macOS backend gives them: x = -scrollingDeltaX * 0.1, y = scrollingDeltaY * 0.1.
TEST(CanvasScroll, ThePictureFollowsTheFingers) {
    const float fingers_right_points = 30.0f;
    const float fingers_down_points = 20.0f;
    const CanvasScroll s =
        canvas_scroll({-fingers_right_points * 0.1f, fingers_down_points * 0.1f}, true, false, unit);
    EXPECT_FLOAT_EQ(s.dx, fingers_right_points);
    EXPECT_FLOAT_EQ(s.dy, fingers_down_points);
}

TEST(CanvasScroll, AWheelNotchZooms) {
    const CanvasScroll in = canvas_scroll({0.0f, 1.0f}, false, false, unit);
    EXPECT_EQ(in.kind, CanvasScrollKind::zoom);
    EXPECT_GT(in.factor, 1.0f);
    const CanvasScroll out = canvas_scroll({0.0f, -1.0f}, false, false, unit);
    EXPECT_EQ(out.kind, CanvasScrollKind::zoom);
    EXPECT_FLOAT_EQ(out.factor, 1.0f / in.factor);
}

TEST(CanvasScroll, TheZoomKeyZoomsWithATrackpadToo) {
    EXPECT_EQ(canvas_scroll({0.0f, 0.5f}, true, true, unit).kind, CanvasScrollKind::zoom);
    EXPECT_EQ(canvas_scroll({0.0f, 1.0f}, false, true, unit).kind, CanvasScrollKind::zoom);
}

TEST(CanvasScroll, ZoomingIsTheSameHoweverTheTravelIsCut) {
    const float once = canvas_scroll({0.0f, 1.0f}, true, true, unit).factor;
    const float piece = canvas_scroll({0.0f, 0.1f}, true, true, unit).factor;
    EXPECT_NEAR(std::pow(piece, 10.0f), once, 1e-4f);
}

TEST(CanvasScroll, SidewaysOnAWheelDoesNothing) {
    EXPECT_EQ(canvas_scroll({1.0f, 0.0f}, false, false, unit).kind, CanvasScrollKind::none);
}

TEST(CanvasScroll, SidewaysOnATrackpadPans) {
    const CanvasScroll s = canvas_scroll({0.5f, 0.0f}, true, false, unit);
    EXPECT_EQ(s.kind, CanvasScrollKind::pan);
    EXPECT_FLOAT_EQ(s.dx, -5.0f);
    EXPECT_FLOAT_EQ(s.dy, 0.0f);
}

TEST(CanvasScroll, NothingScrolledIsNothing) {
    EXPECT_EQ(canvas_scroll({0.0f, 0.0f}, true, false, unit).kind, CanvasScrollKind::none);
    EXPECT_EQ(canvas_scroll({0.0f, 0.0f}, false, true, unit).kind, CanvasScrollKind::none);
}

TEST(WheelPanUnit, FollowsTheFingersWhereSdlGivesADistance) {
    EXPECT_FLOAT_EQ(wheel_pan_unit(PointerPlatform::macos), 10.0f);
    EXPECT_FLOAT_EQ(wheel_pan_unit(PointerPlatform::wayland), 10.0f);
}

TEST(WheelPanUnit, IsSomeLinesOfTextWhereItIsANotch) {
    EXPECT_GT(wheel_pan_unit(PointerPlatform::windows), 10.0f);
    EXPECT_EQ(wheel_pan_unit(PointerPlatform::windows), wheel_pan_unit(PointerPlatform::other));
}

TEST(ScrollLatch, AScrollGoesToWhatIsUnderTheCursor) {
    ScrollLatch latch;
    EXPECT_TRUE(latch.on_canvas(true, 1000));
    ScrollLatch panel;
    EXPECT_FALSE(panel.on_canvas(false, 1000));
}

TEST(ScrollLatch, MomentumFromThePanelStaysOffTheCanvas) {
    ScrollLatch latch;
    EXPECT_FALSE(latch.on_canvas(false, 1000));
    EXPECT_FALSE(latch.on_canvas(true, 1000 + trackpad_memory_ms));  // drifted onto the picture
    EXPECT_FALSE(latch.on_canvas(true, 1000 + 2 * trackpad_memory_ms));  // each event renews it
}

TEST(ScrollLatch, APanStaysOnTheCanvasOverThePanel) {
    ScrollLatch latch;
    EXPECT_TRUE(latch.on_canvas(true, 1000));
    EXPECT_TRUE(latch.on_canvas(false, 1050));
}

TEST(ScrollLatch, APauseEndsTheGesture) {
    ScrollLatch latch;
    EXPECT_FALSE(latch.on_canvas(false, 1000));
    EXPECT_TRUE(latch.on_canvas(true, 1000 + trackpad_memory_ms + 1));
}

TEST(ScrollLatch, ATimeThatRunsBackwardsStartsOver) {
    ScrollLatch latch;
    EXPECT_FALSE(latch.on_canvas(false, 1000));
    EXPECT_TRUE(latch.on_canvas(true, 900));
}

}  // namespace
}  // namespace prism::gui
