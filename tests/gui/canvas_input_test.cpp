#include "prism/gui/canvas_input.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

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

TEST(PinchScaleOf, MacosSendsTheChangeAndLinuxTheTotal) {
    EXPECT_EQ(pinch_scale_of(PointerPlatform::macos), PinchScale::per_event);
    EXPECT_EQ(pinch_scale_of(PointerPlatform::wayland), PinchScale::running_total);
    EXPECT_EQ(pinch_scale_of(PointerPlatform::other), PinchScale::running_total);
}

TEST(PinchTracker, APerEventScaleIsTheFactor) {
    PinchTracker pinch(PinchScale::per_event);
    pinch.begin();
    EXPECT_FLOAT_EQ(pinch.update(1.1f), 1.1f);
    EXPECT_FLOAT_EQ(pinch.update(0.9f), 0.9f);
}

TEST(PinchTracker, ARunningTotalBecomesTheSameFactors) {
    PinchTracker pinch(PinchScale::running_total);
    pinch.begin();
    EXPECT_FLOAT_EQ(pinch.update(1.1f), 1.1f);
    EXPECT_FLOAT_EQ(pinch.update(1.21f), 1.1f);
    EXPECT_FLOAT_EQ(pinch.update(1.21f), 1.0f);
    EXPECT_FLOAT_EQ(pinch.update(0.605f), 0.5f);
}

TEST(PinchTracker, BothKindsReachTheSameZoom) {
    const float changes[] = {1.05f, 1.2f, 0.9f, 1.3f, 0.75f};
    PinchTracker per_event(PinchScale::per_event);
    PinchTracker total(PinchScale::running_total);
    per_event.begin();
    total.begin();
    float zoom_a = 1.0f;
    float zoom_b = 1.0f;
    float running = 1.0f;
    for (const float change : changes) {
        running *= change;
        zoom_a *= per_event.update(change);
        zoom_b *= total.update(running);
    }
    EXPECT_NEAR(zoom_a, zoom_b, 1e-5f);
}

TEST(PinchTracker, ANewGestureStartsItsTotalOver) {
    PinchTracker pinch(PinchScale::running_total);
    pinch.begin();
    (void)pinch.update(2.0f);
    pinch.begin();
    EXPECT_FLOAT_EQ(pinch.update(1.5f), 1.5f);
}

TEST(PinchTracker, AScaleThatCantBeOneChangesNothing) {
    for (const PinchScale kind : {PinchScale::per_event, PinchScale::running_total}) {
        PinchTracker pinch(kind);
        pinch.begin();
        EXPECT_FLOAT_EQ(pinch.update(0.0f), 1.0f);
        EXPECT_FLOAT_EQ(pinch.update(-1.0f), 1.0f);
        EXPECT_FLOAT_EQ(pinch.update(std::nanf("")), 1.0f);
        EXPECT_FLOAT_EQ(pinch.update(std::numeric_limits<float>::infinity()), 1.0f);
        // And the total it was running stays usable.
        EXPECT_FLOAT_EQ(pinch.update(2.0f), 2.0f);
    }
}

}  // namespace
}  // namespace prism::gui
