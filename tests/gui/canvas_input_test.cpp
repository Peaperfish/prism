#include "prism/gui/canvas_input.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "prism/gui/stage_controls.hpp"

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

TEST(PlainDrag, PansOnlyWhenZoomedInWithNoStripAndNoControl) {
    EXPECT_TRUE(plain_drag_pans(false, false, false));
    EXPECT_FALSE(plain_drag_pans(true, false, false));   // at fit there is nowhere to go
    EXPECT_FALSE(plain_drag_pans(false, true, false));   // the strip's click sets the exposure
    EXPECT_FALSE(plain_drag_pans(false, false, true));   // the compare divider wins
}

TEST(DoubleClick, ZoomsExceptOnTheStripAControlOrWithTheHand) {
    EXPECT_TRUE(double_click_zooms(false, false, false));
    EXPECT_FALSE(double_click_zooms(true, false, false));
    EXPECT_FALSE(double_click_zooms(false, true, false));
    EXPECT_FALSE(double_click_zooms(false, false, true));
}

TEST(PlainDrag, PaintsInsteadOfPanningInBrushMode) {
    EXPECT_FALSE(plain_drag_pans(false, false, false, true));
    EXPECT_TRUE(plain_drag_pans(false, false, false, false));
}

TEST(DoubleClick, DoesNotZoomInBrushMode) {
    EXPECT_FALSE(double_click_zooms(false, false, false, true));
    EXPECT_TRUE(double_click_zooms(false, false, false, false));
}

TEST(BrushPress, IsTheBrushsOnlyInBrushModeAndOffTheDividerAndTheChips) {
    EXPECT_TRUE(brush_takes_press(true, true, false, false));
    EXPECT_FALSE(brush_takes_press(false, true, false, false));  // not in Brush mode
    EXPECT_FALSE(brush_takes_press(true, false, false, false));  // the Brush isn't available
    EXPECT_FALSE(brush_takes_press(true, true, true, false));    // on the compare divider
    EXPECT_FALSE(brush_takes_press(true, true, false, true));    // on a chip
}

TEST(BrushPress, AChipIsAnyOfTheCanvasChipsEdgesIncluded) {
    const std::vector<ViewRect> chips{{{10.0f, 10.0f}, {50.0f, 30.0f}},
                                      {{100.0f, 0.0f}, {120.0f, 20.0f}}};
    EXPECT_TRUE(view_point_in_any({10.0f, 10.0f}, chips));
    EXPECT_TRUE(view_point_in_any({50.0f, 30.0f}, chips));
    EXPECT_TRUE(view_point_in_any({110.0f, 5.0f}, chips));
    EXPECT_FALSE(view_point_in_any({60.0f, 20.0f}, chips));
    EXPECT_FALSE(view_point_in_any({50.1f, 20.0f}, chips));
    EXPECT_FALSE(view_point_in_any({20.0f, 20.0f}, {}));
}

TEST(SpaceKey, IsClaimedOnlyOnAnIdleCanvas) {
    EXPECT_TRUE(canvas_claims_space(true, false, false));
    EXPECT_FALSE(canvas_claims_space(false, false, false));  // the cursor is elsewhere
    EXPECT_FALSE(canvas_claims_space(true, true, false));    // typing a value
    EXPECT_FALSE(canvas_claims_space(true, false, true));    // dragging a slider
}

TEST(HandDrag, NothingMovesUntilPressed) {
    HandDrag drag;
    EXPECT_FALSE(drag.pressed());
    EXPECT_FALSE(drag.move({10.0f, 10.0f}).has_value());
}

TEST(HandDrag, WithNoThresholdItFollowsAtOnce) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 0.0f);
    EXPECT_TRUE(drag.dragging());
    const std::optional<ViewPoint> d = drag.move({101.0f, 98.0f});
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->x, 1.0f);
    EXPECT_FLOAT_EQ(d->y, -2.0f);
}

TEST(HandDrag, BelowTheThresholdNothingMoves) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 6.0f);
    EXPECT_FALSE(drag.move({103.0f, 103.0f}).has_value());  // 4.2 from the press
    EXPECT_FALSE(drag.dragging());
    EXPECT_TRUE(drag.pressed());
}

TEST(HandDrag, PastTheThresholdItCatchesUpWithTheHand) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 6.0f);
    (void)drag.move({103.0f, 103.0f});
    const std::optional<ViewPoint> d = drag.move({110.0f, 100.0f});
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->x, 10.0f);  // from the press, not from the last position
    EXPECT_FLOAT_EQ(d->y, 0.0f);
    EXPECT_TRUE(drag.dragging());
    const std::optional<ViewPoint> next = drag.move({111.0f, 100.0f});
    ASSERT_TRUE(next.has_value());
    EXPECT_FLOAT_EQ(next->x, 1.0f);  // and from then on, the change since the last
}

TEST(HandDrag, OnceDraggingItStaysSoNearThePress) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 6.0f);
    (void)drag.move({110.0f, 100.0f});
    const std::optional<ViewPoint> d = drag.move({101.0f, 100.0f});
    ASSERT_TRUE(d.has_value());
    EXPECT_FLOAT_EQ(d->x, -9.0f);
}

TEST(HandDrag, ACursorThatStaysStillMovesNothing) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 0.0f);
    EXPECT_FALSE(drag.move({100.0f, 100.0f}).has_value());
}

TEST(HandDrag, ReleaseEndsIt) {
    HandDrag drag;
    drag.press({100.0f, 100.0f}, 0.0f);
    drag.release();
    EXPECT_FALSE(drag.pressed());
    EXPECT_FALSE(drag.dragging());
    EXPECT_FALSE(drag.move({120.0f, 100.0f}).has_value());
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

namespace {

// A 100x100 canvas at the origin, showing a 40x30 picture, on a display of one device pixel per
// screen unit.
ViewGeometry sample_geometry() {
    return {0.0f, 0.0f, 100.0f, 100.0f, 1.0f, 40, 30};
}

}  // namespace

TEST(SampleMarkerPlace, StaysOnItsPixelThroughZoomAndPan) {
    const ViewGeometry g = sample_geometry();
    ViewState s;
    const auto pixel_under = [&](ViewPoint at) { return view_pixel_at(s, g, at); };
    const SamplePin pin{12, 7};
    SampleMarker m = sample_marker(s, g, pin);
    EXPECT_TRUE(m.on_canvas);
    EXPECT_EQ(pixel_under(m.centre), (ViewPixel{12, 7}));

    s = view_zoomed_to(s, g, 4.0f, m.centre);
    m = sample_marker(s, g, pin);
    EXPECT_TRUE(m.on_canvas);
    EXPECT_EQ(pixel_under(m.centre), (ViewPixel{12, 7}));
    EXPECT_FLOAT_EQ(m.square.max.x - m.square.min.x, 4.0f);
    EXPECT_FLOAT_EQ(m.square.max.y - m.square.min.y, 4.0f);

    s = view_panned_by(s, g, 8.0f, -4.0f);
    m = sample_marker(s, g, pin);
    EXPECT_EQ(pixel_under(m.centre), (ViewPixel{12, 7}));
}

TEST(SampleMarkerPlace, AnOutlineOnlyAbove200Percent) {
    // A picture bigger than the canvas, so that its fit is below 100%.
    const ViewGeometry g{0.0f, 0.0f, 100.0f, 100.0f, 1.0f, 400, 300};
    ViewState s;
    EXPECT_FALSE(sample_marker(s, g, {200, 150}).shows_square);
    s = view_zoomed_to(s, g, 2.0f);
    EXPECT_FALSE(sample_marker(s, g, {200, 150}).shows_square);
    s = view_zoomed_to(s, g, 2.5f);
    EXPECT_TRUE(sample_marker(s, g, {200, 150}).shows_square);
}

TEST(SampleMarkerPlace, IsOffTheCanvasWhenPannedAway) {
    const ViewGeometry g = sample_geometry();
    ViewState s = view_zoomed_to(ViewState{}, g, 8.0f, ViewPoint{50.0f, 50.0f});
    EXPECT_TRUE(sample_marker(s, g, {20, 15}).on_canvas);
    EXPECT_FALSE(sample_marker(s, g, {0, 0}).on_canvas);
    EXPECT_FALSE(sample_marker(s, g, {39, 29}).on_canvas);
}

TEST(SamplePress, CanPinOnlyInSampleModeOffTheStripAndControlsWithoutTheHand) {
    EXPECT_TRUE(sample_press_can_pin(true, false, false, false));
    EXPECT_FALSE(sample_press_can_pin(false, false, false, false));
    EXPECT_FALSE(sample_press_can_pin(true, true, false, false));   // the strip's click: exposure
    EXPECT_FALSE(sample_press_can_pin(true, false, true, false));   // the compare divider
    EXPECT_FALSE(sample_press_can_pin(true, false, false, true));   // the hand
}

TEST(SampleClick, APressAndReleaseInPlaceIsAClick) {
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    EXPECT_TRUE(click.release({10.0f, 10.0f}));
}

TEST(SampleClick, ATremorBelowTheThresholdIsStillAClick) {
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    click.move({13.0f, 10.0f});
    click.move({10.0f, 13.0f});
    EXPECT_TRUE(click.release({11.0f, 11.0f}));
}

TEST(SampleClick, ADragIsNotAClickEvenIfItComesBack) {
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    click.move({30.0f, 10.0f});
    EXPECT_FALSE(click.release({10.0f, 10.0f}));
}

TEST(SampleClick, ADragFirstSeenAtTheReleaseIsNotAClick) {
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    EXPECT_FALSE(click.release({30.0f, 10.0f}));
}

TEST(SampleClick, TheThresholdIsTheHandsOwn) {
    // Both tell a click from a drag at the same distance.
    SampleClick click;
    HandDrag hand;
    click.press({0.0f, 0.0f}, 6.0f);
    hand.press({0.0f, 0.0f}, 6.0f);
    EXPECT_FALSE(hand.move({5.9f, 0.0f}).has_value());
    EXPECT_TRUE(click.release({5.9f, 0.0f}));
    click.press({0.0f, 0.0f}, 6.0f);
    EXPECT_TRUE(hand.move({6.0f, 0.0f}).has_value());
    EXPECT_FALSE(click.release({6.0f, 0.0f}));
}

TEST(SampleClick, AReleaseWithoutAPressIsNotAClick) {
    SampleClick click;
    EXPECT_FALSE(click.release({10.0f, 10.0f}));
    click.press({10.0f, 10.0f}, 6.0f);
    EXPECT_TRUE(click.release({10.0f, 10.0f}));
    EXPECT_FALSE(click.release({10.0f, 10.0f}));  // only once
}

TEST(SampleClick, TheSecondPressOfADoubleClickDoesNotPinAgain) {
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    EXPECT_TRUE(click.release({10.0f, 10.0f}));
    click.press({10.0f, 10.0f}, 6.0f);
    click.cancel();  // it zooms
    EXPECT_FALSE(click.release({10.0f, 10.0f}));
}

TEST(SampleClick, AClickAtFitPinsThoughADragThereMovesNothing) {
    // At fit the hand isn't pressed, so the click can't be read from it.
    EXPECT_FALSE(plain_drag_pans(true, false, false));
    EXPECT_TRUE(sample_press_can_pin(true, false, false, false));
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    EXPECT_TRUE(click.release({10.0f, 10.0f}));
}

TEST(SampleModeRules, DragAndDoubleClickKeepTheirOwnRules) {
    // Nothing in those two rules knows about Sample mode: it can't take them away.
    EXPECT_TRUE(plain_drag_pans(false, false, false));
    EXPECT_TRUE(double_click_zooms(false, false, false));
}

TEST(SampleModeRules, TogglingLeavesAndClearsThePin) {
    SampleMode mode;
    EXPECT_FALSE(mode.on());
    mode.toggle();
    EXPECT_TRUE(mode.on());
    mode.place({3, 4}, 40, 30);
    EXPECT_EQ(mode.pin(), (SamplePin{3, 4}));
    mode.toggle();
    EXPECT_FALSE(mode.on());
    EXPECT_FALSE(mode.pin());
    mode.toggle();
    EXPECT_FALSE(mode.pin());  // it doesn't come back
}

TEST(SampleModeRules, OneEscLeavesTheModeAndClearsThePin) {
    SampleMode mode;
    mode.toggle();
    mode.place({3, 4}, 40, 30);
    mode.leave();
    EXPECT_FALSE(mode.on());
    EXPECT_FALSE(mode.pin());
}

TEST(SampleModeRules, ThereIsOnePin) {
    SampleMode mode;
    mode.toggle();
    mode.place({3, 4}, 40, 30);
    mode.place({5, 6}, 40, 30);
    EXPECT_EQ(mode.pin(), (SamplePin{5, 6}));
}

TEST(SampleModeRules, APinNeedsTheModeAndAPixelOnThePicture) {
    SampleMode mode;
    mode.place({3, 4}, 40, 30);
    EXPECT_FALSE(mode.pin());  // the mode is off
    mode.toggle();
    mode.place({40, 4}, 40, 30);
    mode.place({3, 30}, 40, 30);
    mode.place({-1, 4}, 40, 30);
    EXPECT_FALSE(mode.pin());
}

TEST(SampleModeRules, OpeningAFileClearsThePinEvenAtTheSameSize) {
    SampleMode mode;
    mode.toggle();
    mode.place({3, 4}, 40, 30);
    mode.opened();
    mode.base_changed(40, 30);
    EXPECT_FALSE(mode.pin());
    EXPECT_TRUE(mode.on());
}

TEST(SampleModeRules, ANewSizeOfBaseClearsThePinAndARedecodeKeepsIt) {
    SampleMode mode;
    mode.toggle();
    mode.place({3, 4}, 40, 30);
    mode.base_changed(40, 30);  // a new camera matrix: the same pixels, re-decoded
    EXPECT_EQ(mode.pin(), (SamplePin{3, 4}));
    mode.base_changed(41, 30);
    EXPECT_FALSE(mode.pin());
    mode.place({3, 4}, 41, 30);
    mode.base_changed(41, 31);
    EXPECT_FALSE(mode.pin());
    mode.place({3, 4}, 41, 31);
    mode.base_changed(41, 31);
    EXPECT_EQ(mode.pin(), (SamplePin{3, 4}));
}

TEST(SampleModeRules, TheLastReadStaysUntilAnotherPictureOrLeaving) {
    SampleMode mode;
    mode.read({1, 2}, 40, 30);
    EXPECT_FALSE(mode.last_read());  // the mode is off
    mode.toggle();
    mode.read({40, 2}, 40, 30);
    EXPECT_FALSE(mode.last_read());  // off the picture
    mode.read({1, 2}, 40, 30);
    EXPECT_EQ(mode.last_read(), (SamplePin{1, 2}));
    mode.base_changed(40, 30);
    EXPECT_EQ(mode.last_read(), (SamplePin{1, 2}));
    mode.base_changed(41, 30);
    EXPECT_FALSE(mode.last_read());
    mode.read({1, 2}, 41, 30);
    mode.opened();
    EXPECT_FALSE(mode.last_read());
    mode.read({1, 2}, 41, 30);
    mode.leave();
    EXPECT_FALSE(mode.last_read());
}

// --- Pick mode ---------------------------------------------------------------------------------

TEST(PickPress, PicksOnlyInPickModeWhereWhiteBalanceShowsOffTheStripAndControlsWithoutTheHand) {
    EXPECT_TRUE(pick_press_can_pick(true, true, false, false, false));
    EXPECT_FALSE(pick_press_can_pick(false, true, false, false, false));  // not in Pick mode
    EXPECT_FALSE(pick_press_can_pick(true, false, false, false, false));  // viewing Decode
    EXPECT_FALSE(pick_press_can_pick(true, true, true, false, false));    // the strip's click
    EXPECT_FALSE(pick_press_can_pick(true, true, false, true, false));    // a control
    EXPECT_FALSE(pick_press_can_pick(true, true, false, false, true));    // Space: the hand
}

TEST(PickPress, ADragPansAndAClickPicksWithTheSameRulesAsTheSampler) {
    // Zoomed in, a press is both a possible pick and a possible pan; which it was is known at the release.
    ASSERT_TRUE(plain_drag_pans(false, false, false));
    {
        SampleClick click;
        HandDrag hand;
        click.press({10.0f, 10.0f}, 6.0f);
        hand.press({10.0f, 10.0f}, 6.0f);
        click.move({10.0f, 10.0f});
        EXPECT_FALSE(hand.move({10.0f, 10.0f}));
        EXPECT_TRUE(click.release({10.0f, 10.0f}));  // a click: pick, nothing moved
    }
    {
        SampleClick click;
        HandDrag hand;
        click.press({10.0f, 10.0f}, 6.0f);
        hand.press({10.0f, 10.0f}, 6.0f);
        click.move({30.0f, 10.0f});
        EXPECT_TRUE(hand.move({30.0f, 10.0f}));       // the picture moves with the hand
        EXPECT_FALSE(click.release({30.0f, 10.0f}));  // and nothing is picked
    }
    // At fit a drag moves nothing, and a click still picks.
    EXPECT_FALSE(plain_drag_pans(true, false, false));
    EXPECT_TRUE(pick_press_can_pick(true, true, false, false, false));
    // The second press of a double-click zooms and does not pick.
    SampleClick click;
    click.press({10.0f, 10.0f}, 6.0f);
    click.cancel();
    EXPECT_FALSE(click.release({10.0f, 10.0f}));
}

TEST(PickMode, ToggleAndLeave) {
    PickMode mode;
    EXPECT_FALSE(mode.on());
    mode.toggle();
    EXPECT_TRUE(mode.on());
    mode.toggle();
    EXPECT_FALSE(mode.on());
    mode.toggle();
    mode.leave();
    EXPECT_FALSE(mode.on());
}

TEST(PickTools, WEntersPickModeAndLeavesTheOthers) {
    const CanvasTools sampling{true, false, false};
    EXPECT_EQ(tools_after_key(sampling, ToolKey::pick, true, true), (CanvasTools{false, true, false}));
    const CanvasTools strip{false, false, true};
    EXPECT_EQ(tools_after_key(strip, ToolKey::pick, true, true), (CanvasTools{false, true, false}));
    const CanvasTools both{true, false, true};
    EXPECT_EQ(tools_after_key(both, ToolKey::pick, true, true), (CanvasTools{false, true, false}));
    EXPECT_EQ(tools_after_key({}, ToolKey::pick, true, true), (CanvasTools{false, true, false}));
}

TEST(PickTools, WLeavesPickMode) {
    const CanvasTools picking{false, true, false};
    EXPECT_EQ(tools_after_key(picking, ToolKey::pick, true, true), CanvasTools{});
    // Also where it could not be entered now (the view moved to Decode): leaving is always allowed.
    EXPECT_EQ(tools_after_key(picking, ToolKey::pick, false, true), CanvasTools{});
}

TEST(PickTools, IAndTLeavePickMode) {
    const CanvasTools picking{false, true, false};
    EXPECT_EQ(tools_after_key(picking, ToolKey::sample, true, true), (CanvasTools{true, false, false}));
    EXPECT_EQ(tools_after_key(picking, ToolKey::strip, true, true), (CanvasTools{false, false, true}));
    // Sample mode and the strip do not exclude each other, as before.
    EXPECT_EQ(tools_after_key(CanvasTools{true, false, false}, ToolKey::strip, true, true),
              (CanvasTools{true, false, true}));
    EXPECT_EQ(tools_after_key(CanvasTools{false, false, true}, ToolKey::sample, true, true),
              (CanvasTools{true, false, true}));
    // I turns Sample mode off again, and T the strip.
    EXPECT_EQ(tools_after_key(CanvasTools{true, false, false}, ToolKey::sample, true, true),
              CanvasTools{});
    EXPECT_EQ(tools_after_key(CanvasTools{false, false, true}, ToolKey::strip, true, true),
              CanvasTools{});
}

TEST(PickTools, AnUnavailableToolChangesNothing) {
    // Viewing Decode: W does nothing, even with the others on.
    const CanvasTools sampling{true, false, true};
    EXPECT_EQ(tools_after_key(sampling, ToolKey::pick, false, true), sampling);
    EXPECT_EQ(tools_after_key({}, ToolKey::pick, false, true), CanvasTools{});
    // A strip that can't start leaves Pick mode alone.
    const CanvasTools picking{false, true, false};
    EXPECT_EQ(tools_after_key(picking, ToolKey::strip, true, false), picking);
}

TEST(PickAvailable, NotWhileViewingDecodeAndWhereWhiteBalanceIsShown) {
    EXPECT_FALSE(pick_available(StageId::decode));
    EXPECT_TRUE(pick_available(StageId::white_balance));
    EXPECT_FALSE(pick_available(StageId::exposure));  // White balance comes after Exposure
    EXPECT_TRUE(pick_available(StageId::matrix));
    EXPECT_TRUE(pick_available(final_stage));
    // The same rule as the stage's own visibility.
    for (const StageInfo& stage : pipeline_stages()) {
        EXPECT_EQ(pick_available(stage.id), !stage_after_view(StageId::white_balance, stage.id))
            << stage.name;
    }
    // Comparing Decode with a later stage shows the later one, so it is available.
    EXPECT_TRUE(pick_available(StageId::decode, StageId::matrix));
    EXPECT_FALSE(pick_available(StageId::decode, StageId::exposure));
    EXPECT_FALSE(pick_available(StageId::decode, StageId::decode));
}

TEST(PickMarkerPlace, IsThePatchAroundThePixelCutAtTheEdges) {
    const ViewGeometry g = sample_geometry();
    ViewState s = view_zoomed_to(ViewState{}, g, 4.0f);
    PickMarker m = pick_marker(s, g, {20, 15}, 40, 30);
    EXPECT_TRUE(m.on_canvas);
    EXPECT_FLOAT_EQ(m.patch.max.x - m.patch.min.x, 20.0f);  // 5 pixels of 4 screen units
    EXPECT_FLOAT_EQ(m.patch.max.y - m.patch.min.y, 20.0f);
    const SampleMarker pixel = sample_marker(s, g, {20, 15});
    EXPECT_FLOAT_EQ(m.patch.min.x + 8.0f, pixel.square.min.x);  // two pixels in from the left
    EXPECT_FLOAT_EQ(m.patch.min.y + 8.0f, pixel.square.min.y);
    // In a corner it is 3 across, and the pixel is its corner.
    s = view_zoomed_to(ViewState{}, g, 1.0f);
    m = pick_marker(s, g, {0, 0}, 40, 30);
    EXPECT_FLOAT_EQ(m.patch.max.x - m.patch.min.x, 3.0f * 2.5f);
    EXPECT_FLOAT_EQ(m.patch.max.y - m.patch.min.y, 3.0f * 2.5f);
    EXPECT_FALSE(pick_marker(s, g, {40, 0}, 40, 30).on_canvas);
}

}  // namespace
}  // namespace prism::gui

// --- Brush mode --------------------------------------------------------------------------------

using namespace prism::gui;

TEST(BrushMode, IsAvailableOnlyFromDodgeAndBurnOn) {
    EXPECT_FALSE(brush_available(StageId::decode));
    EXPECT_FALSE(brush_available(StageId::exposure));
    EXPECT_TRUE(brush_available(StageId::dodge_burn));
    for (const StageId later : {StageId::white_balance, StageId::matrix, StageId::saturation,
                                StageId::contrast, StageId::curve, StageId::encode}) {
        EXPECT_TRUE(brush_available(later));
    }
}

TEST(BrushMode, TurnsOnAndOffAndLeavesWhenAsked) {
    BrushMode mode;
    EXPECT_FALSE(mode.on());
    mode.toggle();
    EXPECT_TRUE(mode.on());
    mode.toggle();
    EXPECT_FALSE(mode.on());
    mode.toggle();
    mode.leave();
    EXPECT_FALSE(mode.on());
    mode.toggle();
    mode.opened();  // another photo
    EXPECT_FALSE(mode.on());
}

TEST(BrushMode, StrengthStartsAtHalfAStopAndSnapsToTenths) {
    BrushMode mode;
    EXPECT_FLOAT_EQ(mode.strength(), 0.5f);
    mode.set_strength(1.04f);
    EXPECT_FLOAT_EQ(mode.strength(), 1.0f);
    mode.set_strength(0.0f);
    EXPECT_FLOAT_EQ(mode.strength(), 0.1f);
    mode.set_strength(9.0f);
    EXPECT_FLOAT_EQ(mode.strength(), 2.0f);
    mode.set_strength(std::nanf(""));
    EXPECT_FLOAT_EQ(mode.strength(), 0.5f);
    // It is a tool setting: another photo does not reset it, and leaving the mode keeps it.
    mode.set_strength(1.5f);
    mode.toggle();
    mode.opened();
    EXPECT_FLOAT_EQ(mode.strength(), 1.5f);
}

TEST(BrushMode, StrengthReadsInStopsSingularUpToOne) {
    EXPECT_EQ(brush_strength_text(0.1f), "0.1 stop");
    EXPECT_EQ(brush_strength_text(0.5f), "0.5 stop");
    EXPECT_EQ(brush_strength_text(1.0f), "1.0 stop");
    EXPECT_EQ(brush_strength_text(1.1f), "1.1 stops");
    EXPECT_EQ(brush_strength_text(1.5f), "1.5 stops");
    EXPECT_EQ(brush_strength_text(2.0f), "2.0 stops");
}

TEST(BrushTools, TheButtonEntersBrushModeAndLeavesTheOthers) {
    EXPECT_EQ(tools_after_key({}, ToolKey::brush, true, true, true),
              (CanvasTools{false, false, false, true}));
    EXPECT_EQ(tools_after_key(CanvasTools{true, false, true, false}, ToolKey::brush, true, true, true),
              (CanvasTools{false, false, false, true}));
    EXPECT_EQ(tools_after_key(CanvasTools{false, true, false, false}, ToolKey::brush, true, true, true),
              (CanvasTools{false, false, false, true}));
    // Again leaves it.
    EXPECT_EQ(tools_after_key(CanvasTools{false, false, false, true}, ToolKey::brush, true, true, true),
              CanvasTools{});
}

TEST(BrushTools, IAndTAndWLeaveBrushMode) {
    const CanvasTools brushing{false, false, false, true};
    EXPECT_EQ(tools_after_key(brushing, ToolKey::sample, true, true, true),
              (CanvasTools{true, false, false, false}));
    EXPECT_EQ(tools_after_key(brushing, ToolKey::strip, true, true, true),
              (CanvasTools{false, false, true, false}));
    EXPECT_EQ(tools_after_key(brushing, ToolKey::pick, true, true, true),
              (CanvasTools{false, true, false, false}));
}

TEST(BrushTools, AnUnavailableBrushChangesNothingButCanBeLeft) {
    const CanvasTools sampling{true, false, true, false};
    EXPECT_EQ(tools_after_key(sampling, ToolKey::brush, true, true, false), sampling);
    EXPECT_EQ(tools_after_key({}, ToolKey::brush, true, true, false), CanvasTools{});
    // The default says unavailable too.
    EXPECT_EQ(tools_after_key({}, ToolKey::brush, true, true), CanvasTools{});
    // The view moved before Dodge & burn with the Brush on: the key still turns it off.
    EXPECT_EQ(tools_after_key(CanvasTools{false, false, false, true}, ToolKey::brush, true, true, false),
              CanvasTools{});
    // A tool that can't start leaves the Brush on.
    const CanvasTools brushing{false, false, false, true};
    EXPECT_EQ(tools_after_key(brushing, ToolKey::pick, false, true, true), brushing);
    EXPECT_EQ(tools_after_key(brushing, ToolKey::strip, true, false, true), brushing);
}
