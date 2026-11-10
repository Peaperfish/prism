#include "prism/gui/brush.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace prism::gui {
namespace {

using Kind = BrushActionKind;

// A 1000 x 500 picture: the long side is 1000 px, so a spacing of 0.01 is 10 px.
constexpr BrushCanvas canvas{1000, 500};
constexpr float spacing = 0.01f;

std::vector<Kind> kinds(const std::vector<BrushAction>& actions) {
    std::vector<Kind> out;
    for (const BrushAction& a : actions) {
        out.push_back(a.kind);
    }
    return out;
}

std::vector<BrushAction> press(BrushGesture& g, ViewPoint at, BrushModifiers keys = {},
                               BrushTone base = BrushTone::dodge) {
    return g.press(at, keys, base, spacing, canvas);
}

TEST(BrushPoint, IsTheImagePointOverThePicturesSize) {
    EXPECT_EQ(brush_point({500.0f, 125.0f}, canvas), (FieldPoint{0.5f, 0.25f}));
    EXPECT_EQ(brush_point({0.0f, 500.0f}, canvas), (FieldPoint{0.0f, 1.0f}));
    EXPECT_EQ(brush_point({3.0f, 4.0f}, BrushCanvas{}), (FieldPoint{}));
}

TEST(BrushGesture, APressBeginsAStrokeAndPutsItsFirstPointDown) {
    BrushGesture g;
    const std::vector<BrushAction> a = press(g, {100.0f, 50.0f});
    ASSERT_EQ(kinds(a), (std::vector<Kind>{Kind::begin_stroke, Kind::add_point}));
    EXPECT_EQ(a[0].tone, BrushTone::dodge);
    EXPECT_EQ(a[1].point, (FieldPoint{0.1f, 0.1f}));
    EXPECT_TRUE(g.stroking());
}

TEST(BrushGesture, AMoveExactlyTheSpacingAwayAddsAPointAndJustShortOfItDoesNot) {
    BrushGesture g;
    (void)press(g, {0.0f, 100.0f});
    EXPECT_TRUE(g.move({9.99f, 100.0f}).empty());
    const std::vector<BrushAction> at = g.move({10.0f, 100.0f});
    ASSERT_EQ(kinds(at), (std::vector<Kind>{Kind::add_point}));
    EXPECT_EQ(at[0].point, (FieldPoint{0.01f, 0.2f}));
}

TEST(BrushGesture, TheSpacingIsMeasuredAlongTheDiagonalInLongSideUnits) {
    BrushGesture g;
    (void)press(g, {100.0f, 100.0f});
    // 6 px right and 6 px down: 8.49 px, short of 10. Down is in the short side's units (v runs
    // over 500 px), so this fails if they are not turned into the long side's: 6 / 500 of the
    // long side would be 12 px.
    EXPECT_TRUE(g.move({106.0f, 106.0f}).empty());
    // 6 right and a little over 8 down: just over 10 px.
    EXPECT_EQ(kinds(g.move({106.0f, 108.05f})), (std::vector<Kind>{Kind::add_point}));
}

TEST(BrushGesture, ManySmallMovesAddAPointEachTimeTheyHaveGoneTheSpacingInAll) {
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f});
    std::vector<float> kept;
    for (int i = 1; i <= 40; ++i) {
        const float x = 3.0f * static_cast<float>(i);  // under a third of the spacing a time
        for (const BrushAction& a : g.move({x, 0.0f})) {
            ASSERT_EQ(a.kind, Kind::add_point);
            kept.push_back(a.point.u * 1000.0f);
        }
    }
    // Every fourth move (12 px from the last point kept), not every move once 10 px from the press,
    // and not never (no single move is 10 px).
    ASSERT_EQ(kept.size(), 10u);
    for (std::size_t k = 0; k < kept.size(); ++k) {
        EXPECT_NEAR(kept[k], 12.0f * static_cast<float>(k + 1), 1e-3f) << k;
    }
}

TEST(BrushGesture, AReleaseAddsItsPointIfFarEnoughThenCommits) {
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f});
    const std::vector<BrushAction> far = g.release({30.0f, 0.0f});
    ASSERT_EQ(kinds(far), (std::vector<Kind>{Kind::add_point, Kind::commit_stroke}));
    EXPECT_EQ(far[0].point, (FieldPoint{0.03f, 0.0f}));
    EXPECT_FALSE(g.stroking());

    (void)press(g, {0.0f, 0.0f});
    EXPECT_EQ(kinds(g.release({5.0f, 0.0f})), (std::vector<Kind>{Kind::commit_stroke}));
}

TEST(BrushGesture, APressAndReleaseWithoutAMoveIsAOneDotStroke) {
    BrushGesture g;
    std::vector<BrushAction> all = press(g, {200.0f, 100.0f});
    for (const BrushAction& a : g.release({200.0f, 100.0f})) {
        all.push_back(a);
    }
    EXPECT_EQ(kinds(all),
              (std::vector<Kind>{Kind::begin_stroke, Kind::add_point, Kind::commit_stroke}));
}

TEST(BrushGesture, AltAtThePressFlipsThatStrokeOnly) {
    BrushGesture g;
    std::vector<BrushAction> a = press(g, {0.0f, 0.0f}, {.alt = true}, BrushTone::dodge);
    EXPECT_EQ(a[0].tone, BrushTone::burn);
    (void)g.release({0.0f, 0.0f});
    a = press(g, {0.0f, 0.0f}, {}, BrushTone::dodge);
    EXPECT_EQ(a[0].tone, BrushTone::dodge);
    (void)g.release({0.0f, 0.0f});
    a = press(g, {0.0f, 0.0f}, {.alt = true}, BrushTone::burn);
    EXPECT_EQ(a[0].tone, BrushTone::dodge);
}

TEST(BrushGesture, AltPressedOrLetGoDuringAStrokeChangesNothing) {
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f}, {}, BrushTone::dodge);
    const std::vector<BrushAction> pressed = g.move({50.0f, 0.0f}, {.alt = true});
    ASSERT_EQ(kinds(pressed), (std::vector<Kind>{Kind::add_point}));
    EXPECT_EQ(pressed[0].tone, BrushTone::dodge);
    EXPECT_EQ(g.release({100.0f, 0.0f}, {.alt = true}).back().tone, BrushTone::dodge);

    (void)press(g, {0.0f, 0.0f}, {.alt = true}, BrushTone::dodge);
    const std::vector<BrushAction> let_go = g.move({50.0f, 0.0f}, {});
    ASSERT_EQ(kinds(let_go), (std::vector<Kind>{Kind::add_point}));
    EXPECT_EQ(let_go[0].tone, BrushTone::burn);
    EXPECT_EQ(g.release({100.0f, 0.0f}, {}).back().tone, BrushTone::burn);
}

TEST(BrushGesture, SpaceAtThePressPansToTheRelease) {
    BrushGesture g;
    EXPECT_EQ(kinds(press(g, {0.0f, 0.0f}, {.space = true})),
              (std::vector<Kind>{Kind::passthrough}));
    EXPECT_FALSE(g.stroking());
    EXPECT_TRUE(g.panning());
    EXPECT_EQ(kinds(g.move({100.0f, 0.0f})), (std::vector<Kind>{Kind::passthrough}));
    EXPECT_EQ(kinds(g.release({100.0f, 0.0f})), (std::vector<Kind>{Kind::passthrough}));
    EXPECT_FALSE(g.panning());
    // The next press without Space paints.
    EXPECT_EQ(kinds(press(g, {0.0f, 0.0f})),
              (std::vector<Kind>{Kind::begin_stroke, Kind::add_point}));
}

TEST(BrushGesture, SpaceDuringAStrokeLeavesItAStroke) {
    // Space only counts at a press: held during the stroke, or with a second press while the
    // stroke is held, it changes nothing, and the stroke goes on.
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f});
    EXPECT_EQ(kinds(g.move({20.0f, 0.0f}, {.space = true})), (std::vector<Kind>{Kind::add_point}));
    EXPECT_TRUE(press(g, {20.0f, 0.0f}, {.space = true}).empty());
    EXPECT_TRUE(g.stroking());
    EXPECT_FALSE(g.panning());
    EXPECT_EQ(kinds(g.move({40.0f, 0.0f}, {.space = true})), (std::vector<Kind>{Kind::add_point}));
    EXPECT_EQ(kinds(g.release({40.0f, 0.0f}, {.space = true})),
              (std::vector<Kind>{Kind::commit_stroke}));
}

TEST(BrushGesture, EscDuringAStrokeCancelsItAndTheRestOfThePressDoesNothing) {
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f});
    EXPECT_EQ(kinds(g.escape()), (std::vector<Kind>{Kind::cancel_stroke}));
    EXPECT_FALSE(g.stroking());
    EXPECT_TRUE(g.move({100.0f, 0.0f}).empty());
    EXPECT_TRUE(g.escape().empty());  // not a leave: the button is still down
    EXPECT_TRUE(g.release({100.0f, 0.0f}).empty());
    // A new press paints again.
    EXPECT_EQ(kinds(press(g, {0.0f, 0.0f})),
              (std::vector<Kind>{Kind::begin_stroke, Kind::add_point}));
}

TEST(BrushGesture, EscWithNoStrokeLeavesTheBrush) {
    BrushGesture g;
    EXPECT_EQ(kinds(g.escape()), (std::vector<Kind>{Kind::leave_brush}));
    (void)press(g, {0.0f, 0.0f});
    (void)g.release({0.0f, 0.0f});
    EXPECT_EQ(kinds(g.escape()), (std::vector<Kind>{Kind::leave_brush}));
}

TEST(BrushGesture, AReleaseOrAMoveWithoutAPressDoesNothing) {
    BrushGesture g;
    EXPECT_TRUE(g.move({10.0f, 10.0f}).empty());
    EXPECT_TRUE(g.release({10.0f, 10.0f}).empty());
    EXPECT_FALSE(g.stroking());
}

TEST(BrushGesture, ASecondPressDuringAStrokeDoesNothing) {
    BrushGesture g;
    (void)press(g, {0.0f, 0.0f});
    EXPECT_TRUE(press(g, {500.0f, 250.0f}, {.alt = true}).empty());
    EXPECT_TRUE(g.stroking());
    EXPECT_EQ(g.tone(), BrushTone::dodge);
    // The stroke goes on from its own last point, not the second press's.
    EXPECT_TRUE(g.move({5.0f, 0.0f}).empty());
}

TEST(BrushGesture, FinishCommitsAStrokeUnderWayAndOtherwiseDoesNothing) {
    BrushGesture g;
    EXPECT_TRUE(g.finish().empty());
    (void)press(g, {0.0f, 0.0f});
    EXPECT_EQ(kinds(g.finish()), (std::vector<Kind>{Kind::commit_stroke}));
    EXPECT_FALSE(g.stroking());
    EXPECT_TRUE(g.release({0.0f, 0.0f}).empty());
}

}  // namespace
}  // namespace prism::gui
