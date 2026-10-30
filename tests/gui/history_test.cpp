#include <gtest/gtest.h>

#include "prism/gui/history.hpp"
#include "prism/gui/settings.hpp"

using prism::gui::EditSettings;
using prism::gui::History;

namespace {

EditSettings with_exposure(float stops) {
    EditSettings s;
    s.exposure_stops = stops;
    return s;
}

}  // namespace

TEST(History, StartsWithNothingToUndoOrRedo) {
    History h(with_exposure(1));
    EXPECT_EQ(h.current(), with_exposure(1));
    EXPECT_FALSE(h.can_undo());
    EXPECT_FALSE(h.can_redo());
    EXPECT_FALSE(h.undo());
    EXPECT_FALSE(h.redo());
    EXPECT_EQ(h.current(), with_exposure(1));
}

TEST(History, EachSetIsOneStep) {
    History h;
    h.set(with_exposure(1));
    h.set(with_exposure(2));
    EXPECT_EQ(h.undo_steps(), 2u);

    EXPECT_TRUE(h.undo());
    EXPECT_EQ(h.current(), with_exposure(1));
    EXPECT_TRUE(h.undo());
    EXPECT_EQ(h.current(), EditSettings{});
    EXPECT_FALSE(h.can_undo());

    EXPECT_TRUE(h.redo());
    EXPECT_TRUE(h.redo());
    EXPECT_EQ(h.current(), with_exposure(2));
    EXPECT_FALSE(h.can_redo());
}

TEST(History, SettingTheSameValueAddsNoStep) {
    History h;
    h.set(with_exposure(1));
    h.undo();
    h.set(EditSettings{});  // what is already current
    EXPECT_EQ(h.undo_steps(), 0u);
    EXPECT_TRUE(h.can_redo()) << "an edit that changes nothing must not throw away redo";
}

TEST(History, ANewEditClearsRedo) {
    History h;
    h.set(with_exposure(1));
    h.set(with_exposure(2));
    h.undo();
    h.set(with_exposure(5));
    EXPECT_FALSE(h.can_redo());
    h.undo();
    EXPECT_EQ(h.current(), with_exposure(1));
}

TEST(History, ADragIsOneStep) {
    History h(with_exposure(0.5f));
    h.begin_gesture();
    for (int frame = 1; frame <= 60; ++frame) {
        h.set(with_exposure(0.5f + frame * 0.05f));
    }
    EXPECT_TRUE(h.in_gesture());
    h.end_gesture();
    EXPECT_EQ(h.undo_steps(), 1u);
    EXPECT_EQ(h.current(), with_exposure(0.5f + 60 * 0.05f));

    h.undo();
    EXPECT_EQ(h.current(), with_exposure(0.5f)) << "back to before the drag, not one frame back";
    h.redo();
    EXPECT_EQ(h.current(), with_exposure(0.5f + 60 * 0.05f));
}

TEST(History, ADragBackToTheStartAddsNoStep) {
    History h;
    h.set(with_exposure(1));
    h.undo();  // so there is something to redo
    h.begin_gesture();
    h.set(with_exposure(0.3f));
    h.set(EditSettings{});
    h.end_gesture();
    EXPECT_EQ(h.undo_steps(), 0u);
    EXPECT_TRUE(h.can_redo());
}

TEST(History, UndoDuringADragUndoesTheWholeDrag) {
    History h;
    h.set(with_exposure(1));
    h.begin_gesture();
    h.set(with_exposure(1.5f));
    h.set(with_exposure(2));
    EXPECT_TRUE(h.can_undo());
    EXPECT_TRUE(h.undo());
    EXPECT_FALSE(h.in_gesture());
    EXPECT_EQ(h.current(), with_exposure(1));
    EXPECT_TRUE(h.redo());
    EXPECT_EQ(h.current(), with_exposure(2));
}

TEST(History, UnbalancedGestureCallsAreHarmless) {
    History h;
    h.end_gesture();  // without a begin: nothing happens
    EXPECT_FALSE(h.in_gesture());

    h.begin_gesture();
    h.set(with_exposure(1));
    h.begin_gesture();  // a second begin keeps the first start
    h.set(with_exposure(2));
    h.end_gesture();
    EXPECT_EQ(h.undo_steps(), 1u);
    h.undo();
    EXPECT_EQ(h.current(), EditSettings{});
}

TEST(History, KeepsAtMostCapacitySteps) {
    History h(with_exposure(0), 3);
    for (int i = 1; i <= 5; ++i) {
        h.set(with_exposure(static_cast<float>(i)));
    }
    EXPECT_EQ(h.undo_steps(), 3u);
    while (h.undo()) {
    }
    EXPECT_EQ(h.current(), with_exposure(2)) << "steps 0 and 1 were dropped, oldest first";
    while (h.redo()) {
    }
    EXPECT_EQ(h.current(), with_exposure(5));
    EXPECT_EQ(h.undo_steps(), 3u);
}

TEST(History, ZeroCapacityMeansNoUndo) {
    History h({}, 0);
    h.set(with_exposure(1));
    EXPECT_FALSE(h.undo());
    EXPECT_EQ(h.current(), with_exposure(1));
}

TEST(History, ResetStartsOver) {
    History h;
    h.set(with_exposure(1));
    h.set(with_exposure(2));
    h.undo();
    h.begin_gesture();
    h.reset(with_exposure(7));
    EXPECT_EQ(h.current(), with_exposure(7));
    EXPECT_FALSE(h.can_undo() || h.can_redo() || h.in_gesture());
}
