#include <gtest/gtest.h>

#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/slider_gesture.hpp"

using prism::gui::apply_slider_frame;
using prism::gui::EditSettings;
using prism::gui::ErrorKind;
using prism::gui::MaybeError;
using prism::gui::Session;
using prism::gui::SliderFrame;

// The frames ImGui reports for a slider, replayed without a window. A Session without a file is
// enough: its settings and undo history work the same either way.

namespace {

EditSettings with_exposure(float stops) {
    EditSettings s;
    s.exposure_stops = stops;
    return s;
}

constexpr SliderFrame press_and_move{true, true, false};
constexpr SliderFrame press{true, false, false};
constexpr SliderFrame move{false, true, false};
constexpr SliderFrame release{false, false, true};

// A drag through `values`: pressed on the first, moved through the rest, released after.
void drag(Session& session, std::initializer_list<float> values) {
    bool first = true;
    for (float v : values) {
        EXPECT_EQ(apply_slider_frame(session, first ? press_and_move : move, with_exposure(v)),
                  std::nullopt);
        first = false;
    }
    EXPECT_EQ(apply_slider_frame(session, release, session.settings()), std::nullopt);
}

}  // namespace

TEST(SliderGesture, AWholeDragIsOneUndoStep) {
    Session session;
    drag(session, {0.1f, 0.2f, 0.5f, 0.9f, 1.3f});
    EXPECT_EQ(session.settings(), with_exposure(1.3f));
    EXPECT_EQ(session.history().undo_steps(), 1u);

    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{}) << "one undo goes back to before the drag";
    EXPECT_FALSE(session.history().can_undo());
    EXPECT_EQ(session.redo(), std::nullopt);
    EXPECT_EQ(session.settings(), with_exposure(1.3f));
}

TEST(SliderGesture, TheStepIsMadeOnRelease) {
    Session session;
    EXPECT_EQ(apply_slider_frame(session, press_and_move, with_exposure(0.5f)), std::nullopt);
    EXPECT_EQ(apply_slider_frame(session, move, with_exposure(0.7f)), std::nullopt);
    EXPECT_TRUE(session.history().in_gesture());
    EXPECT_EQ(session.history().undo_steps(), 0u) << "still dragging";
    EXPECT_EQ(session.settings(), with_exposure(0.7f)) << "the preview follows the drag";

    EXPECT_EQ(apply_slider_frame(session, release, session.settings()), std::nullopt);
    EXPECT_FALSE(session.history().in_gesture());
    EXPECT_EQ(session.history().undo_steps(), 1u);
}

TEST(SliderGesture, AClickThatChangesNothingLeavesNoStep) {
    Session session;
    EXPECT_EQ(apply_slider_frame(session, press, session.settings()), std::nullopt);
    EXPECT_EQ(apply_slider_frame(session, release, session.settings()), std::nullopt);
    EXPECT_FALSE(session.history().can_undo());
}

TEST(SliderGesture, ADragBackToTheStartLeavesNoStep) {
    Session session;
    drag(session, {0.4f, 0.8f, 0.0f});
    EXPECT_FALSE(session.history().can_undo());
}

TEST(SliderGesture, AClickWithinOneFrameIsOneStep) {
    // Pressed, moved and released before the next frame: all three flags at once.
    Session session;
    EXPECT_EQ(apply_slider_frame(session, {true, true, true}, with_exposure(1)), std::nullopt);
    EXPECT_FALSE(session.history().in_gesture());
    EXPECT_EQ(session.history().undo_steps(), 1u);
}

TEST(SliderGesture, ATypedValueIsOneStep) {
    // Ctrl/Cmd-click starts text entry (activated); Enter changes the value and deactivates.
    Session session;
    EXPECT_EQ(apply_slider_frame(session, press, session.settings()), std::nullopt);
    EXPECT_EQ(apply_slider_frame(session, {false, true, true}, with_exposure(1.5f)), std::nullopt);
    EXPECT_EQ(session.history().undo_steps(), 1u);
    EXPECT_EQ(session.settings(), with_exposure(1.5f));
}

TEST(SliderGesture, TwoDragsAreTwoStepsAndANewDragClearsRedo) {
    Session session;
    drag(session, {0.5f, 1.0f});
    drag(session, {1.5f, 2.0f});
    EXPECT_EQ(session.history().undo_steps(), 2u);

    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), with_exposure(1.0f));
    EXPECT_TRUE(session.history().can_redo());
    drag(session, {-1.0f});
    EXPECT_FALSE(session.history().can_redo());
    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), with_exposure(1.0f));
}

TEST(SliderGesture, AnInvalidValueIsRefusedAndTheGestureStillEnds) {
    Session session;
    EditSettings bad;
    bad.saturation = 12.0f;  // the CLI allows 0 to 10
    EXPECT_EQ(apply_slider_frame(session, press, session.settings()), std::nullopt);
    const MaybeError error = apply_slider_frame(session, {false, true, true}, bad);
    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, ErrorKind::invalid_settings);
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_FALSE(session.history().in_gesture());
    EXPECT_FALSE(session.history().can_undo());
}

TEST(SliderGesture, UndoDuringADragUndoesTheWholeDrag) {
    // Cmd/Ctrl+Z pressed while the mouse is still down: History ends the gesture and undoes it.
    Session session;
    EXPECT_EQ(apply_slider_frame(session, press_and_move, with_exposure(0.5f)), std::nullopt);
    EXPECT_EQ(apply_slider_frame(session, move, with_exposure(0.9f)), std::nullopt);
    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_FALSE(session.history().in_gesture());
    // The release that follows changes nothing.
    EXPECT_EQ(apply_slider_frame(session, release, session.settings()), std::nullopt);
    EXPECT_FALSE(session.history().can_undo());
}
