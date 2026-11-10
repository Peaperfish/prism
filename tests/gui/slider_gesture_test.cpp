#include <gtest/gtest.h>

#include <initializer_list>
#include <optional>

#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/slider_gesture.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/typed_value.hpp"
#include "prism/white_balance.hpp"

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

// --- White balance: the same gesture, through set_white_balance as the window calls it -----------

namespace {

using prism::WhiteBalance;
using prism::gui::WhiteBalanceControl;

// One frame of a White balance slider, as App's callback applies it.
void white_balance_frame(Session& session, const SliderFrame& frame, WhiteBalanceControl control,
                         std::optional<double> value,
                         const std::optional<prism::Chromaticity>& as_shot) {
    EditSettings edited = session.settings();
    if (value) {
        prism::gui::set_white_balance(edited, control, *value, as_shot);
    }
    EXPECT_EQ(apply_slider_frame(session, frame, edited), std::nullopt);
}

}  // namespace

TEST(SliderGesture, AWhiteBalanceDragFromAsShotIsOneUndoStepBackToAsShot) {
    const std::optional<prism::Chromaticity> as_shot = prism::white_of(WhiteBalance{5328, 26.7});
    Session session;
    white_balance_frame(session, press, WhiteBalanceControl::kelvin, std::nullopt, as_shot);
    EXPECT_FALSE(session.settings().white_balance) << "a press alone chooses nothing";
    for (const double kelvin : {5001.0, 4803.0, 4498.0, 4204.0}) {
        white_balance_frame(session, move, WhiteBalanceControl::kelvin, kelvin, as_shot);
    }
    EXPECT_EQ(session.history().undo_steps(), 0u) << "still dragging";
    white_balance_frame(session, release, WhiteBalanceControl::kelvin, std::nullopt, as_shot);
    EXPECT_EQ(session.settings().white_balance, (WhiteBalance{4200, 27}));
    EXPECT_EQ(session.history().undo_steps(), 1u);

    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{}) << "As shot again, exactly";
    EXPECT_FALSE(session.history().can_undo());
}

TEST(SliderGesture, ATypedWhiteBalanceValueAndAResetAreOneStepEach) {
    const std::optional<prism::Chromaticity> as_shot = prism::white_of(WhiteBalance{5328, 26.7});
    Session session;
    const std::optional<double> typed =
        prism::gui::typed_white_balance_value(WhiteBalanceControl::kelvin, "5 200 K");
    ASSERT_TRUE(typed);
    white_balance_frame(session, SliderFrame{true, true, true}, WhiteBalanceControl::kelvin, typed,
                        as_shot);
    EXPECT_EQ(session.settings().white_balance, (WhiteBalance{5200, 27}));
    EXPECT_EQ(session.history().undo_steps(), 1u);

    white_balance_frame(session, SliderFrame{true, true, true}, WhiteBalanceControl::tint,
                        prism::gui::typed_white_balance_value(WhiteBalanceControl::tint, "-10"),
                        as_shot);
    EXPECT_EQ(session.settings().white_balance, (WhiteBalance{5200, -10}));
    EXPECT_EQ(session.history().undo_steps(), 2u);

    EditSettings reset = session.settings();
    prism::gui::reset_white_balance(reset);
    EXPECT_EQ(session.set_settings(reset), std::nullopt);
    EXPECT_EQ(session.settings().white_balance, std::nullopt);
    EXPECT_EQ(session.history().undo_steps(), 3u);
    EXPECT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings().white_balance, (WhiteBalance{5200, -10}));
}

TEST(SliderGesture, ATypedWhiteBalanceValueThatIsWhereAsShotSitsLeavesNoStep) {
    const std::optional<prism::Chromaticity> as_shot = prism::white_of(WhiteBalance{5328, 26.7});
    Session session;
    white_balance_frame(session, SliderFrame{true, true, true}, WhiteBalanceControl::kelvin, 5330.0,
                        as_shot);
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_EQ(session.history().undo_steps(), 0u);
}
