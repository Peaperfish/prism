#include "prism/gui/brush.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <ostream>
#include <vector>

#include "prism/gui/field_cache.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

// So that a failure reads "add_point", not the enum's bytes.
void PrintTo(BrushActionKind kind, std::ostream* os) {
    constexpr const char* names[] = {"begin_stroke",  "add_point",   "commit_stroke",
                                     "cancel_stroke", "leave_brush", "passthrough"};
    *os << names[static_cast<int>(kind)];
}

void PrintTo(BrushTone tone, std::ostream* os) {
    *os << (tone == BrushTone::dodge ? "dodge" : "burn");
}

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

// --- StrokePainter ------------------------------------------------------------------------------

BrushAction begin(BrushTone tone = BrushTone::dodge) {
    return {BrushActionKind::begin_stroke, tone, {}};
}
BrushAction point(float u, float v) {
    return {BrushActionKind::add_point, BrushTone::dodge, {u, v}};
}
BrushAction commit() {
    return {BrushActionKind::commit_stroke, BrushTone::dodge, {}};
}
BrushAction cancel() {
    return {BrushActionKind::cancel_stroke, BrushTone::dodge, {}};
}

// Paints one stroke through `points` and commits it.
void paint(StrokePainter& painter, Session& session, std::vector<FieldPoint> points,
           BrushTone tone = BrushTone::dodge, const BrushStyle& style = {}) {
    ASSERT_EQ(painter.apply(begin(tone), style, canvas, session), std::nullopt);
    for (const FieldPoint& p : points) {
        ASSERT_EQ(painter.apply(point(p.u, p.v), style, canvas, session), std::nullopt);
    }
    ASSERT_EQ(painter.apply(commit(), style, canvas, session), std::nullopt);
}

TEST(StrokePainter, AStrokeIsOneUndoStepWithTheBrushItWasStartedWith) {
    Session session;
    StrokePainter painter;
    BrushStyle style;
    style.radius = 0.02f;
    style.softness = 0.25f;
    style.strength = 1.5f;
    paint(painter, session, {{0.1f, 0.1f}, {0.2f, 0.1f}, {0.3f, 0.2f}}, BrushTone::burn, style);
    EXPECT_FALSE(painter.painting());
    EXPECT_EQ(session.history().undo_steps(), 1u);
    const Strokes& strokes = session.settings().strokes;
    ASSERT_FALSE(strokes.empty());
    EXPECT_EQ(strokes.field()->aspect, aspect_of(canvas.width, canvas.height));
    ASSERT_EQ(strokes.field()->strokes.size(), 1u);
    const FieldStroke& s = *strokes.field()->strokes[0];
    EXPECT_EQ(s.points, (std::vector<FieldPoint>{{0.1f, 0.1f}, {0.2f, 0.1f}, {0.3f, 0.2f}}));
    EXPECT_EQ(s.radius, 0.02f);
    EXPECT_EQ(s.softness, 0.25f);
    EXPECT_EQ(s.weight, -1.5f);  // burn darkens
    ASSERT_TRUE(session.history().can_undo());
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_TRUE(session.settings().strokes.empty());
}

TEST(StrokePainter, ADodgeStrokeLightens) {
    EXPECT_EQ(brush_weight(BrushTone::dodge, 0.5f), 0.5f);
    EXPECT_EQ(brush_weight(BrushTone::burn, 0.5f), -0.5f);
}

TEST(StrokePainter, ACancelLeavesTheUndoAndRedoStepsAndTheSettingsExactlyAsBefore) {
    Session session;
    StrokePainter painter;
    paint(painter, session, {{0.1f, 0.1f}, {0.2f, 0.2f}});
    paint(painter, session, {{0.5f, 0.5f}});
    ASSERT_EQ(session.undo(), std::nullopt);  // so that there is a redo step to keep
    const EditSettings before = session.settings();
    const std::size_t undo_before = session.history().undo_steps();
    const std::size_t redo_before = session.history().redo_steps();
    ASSERT_EQ(undo_before, 1u);
    ASSERT_EQ(redo_before, 1u);

    ASSERT_EQ(painter.apply(begin(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.7f, 0.7f), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.8f, 0.7f), {}, canvas, session), std::nullopt);
    EXPECT_NE(session.settings(), before);  // the stroke shows while it is drawn
    ASSERT_EQ(painter.apply(cancel(), {}, canvas, session), std::nullopt);

    EXPECT_FALSE(painter.painting());
    EXPECT_FALSE(session.history().in_gesture());
    EXPECT_EQ(session.history().undo_steps(), undo_before);
    EXPECT_EQ(session.history().redo_steps(), redo_before);
    EXPECT_EQ(session.settings(), before);
    EXPECT_EQ(recipe_from(session.settings()), recipe_from(before));
    // The very strokes there were, not a copy: the preview's field is kept, not rasterised again.
    EXPECT_EQ(session.settings().strokes.field(), before.strokes.field());
    // And redo still brings back the stroke undone before.
    ASSERT_EQ(session.redo(), std::nullopt);
    EXPECT_EQ(session.settings().strokes.field()->strokes.size(), 2u);
}

TEST(StrokePainter, ACancelOfTheFirstStrokeLeavesNoStrokesAndNoStep) {
    Session session;
    StrokePainter painter;
    ASSERT_EQ(painter.apply(begin(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.5f, 0.5f), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(cancel(), {}, canvas, session), std::nullopt);
    EXPECT_TRUE(session.settings().strokes.empty());
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_EQ(session.history().undo_steps(), 0u);
    EXPECT_FALSE(session.history().can_undo());
}

TEST(StrokePainter, EachPointGrowsTheLastStrokeSoThePreviewOnlyRasterisesWhatIsNew) {
    Session session;
    StrokePainter painter;
    paint(painter, session, {{0.1f, 0.1f}});
    const std::shared_ptr<const FieldStroke> first = session.settings().strokes.field()->strokes[0];
    FieldCache cache;
    const PixelGrid grid = stage_grid(canvas.width / 10, canvas.height / 10);
    EXPECT_EQ(cache.update(session.settings().strokes, grid, 1), FieldPath::fresh);

    ASSERT_EQ(painter.apply(begin(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.4f, 0.4f), {}, canvas, session), std::nullopt);
    EXPECT_EQ(cache.update(session.settings().strokes, grid, 1), FieldPath::append);
    ASSERT_EQ(painter.apply(point(0.5f, 0.4f), {}, canvas, session), std::nullopt);
    EXPECT_EQ(cache.update(session.settings().strokes, grid, 1), FieldPath::grow);
    ASSERT_EQ(painter.apply(point(0.6f, 0.4f), {}, canvas, session), std::nullopt);
    EXPECT_EQ(cache.update(session.settings().strokes, grid, 1), FieldPath::grow);
    // The stroke before it is still the same object.
    EXPECT_EQ(session.settings().strokes.field()->strokes[0], first);
    // A cancel goes back to the field from before the stroke, which the cache still holds.
    ASSERT_EQ(painter.apply(cancel(), {}, canvas, session), std::nullopt);
    EXPECT_EQ(cache.update(session.settings().strokes, grid, 1), FieldPath::undo);
}

TEST(StrokePainter, ActionsOutOfTurnDoNothing) {
    Session session;
    StrokePainter painter;
    ASSERT_EQ(painter.apply(point(0.5f, 0.5f), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(commit(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(cancel(), {}, canvas, session), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_FALSE(session.history().in_gesture());
    // A second begin during a stroke doesn't start another.
    ASSERT_EQ(painter.apply(begin(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.5f, 0.5f), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(begin(BrushTone::burn), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(point(0.6f, 0.5f), {}, canvas, session), std::nullopt);
    ASSERT_EQ(painter.apply(commit(), {}, canvas, session), std::nullopt);
    ASSERT_EQ(session.settings().strokes.field()->strokes.size(), 1u);
    EXPECT_GT(session.settings().strokes.field()->strokes[0]->weight, 0.0f);
    EXPECT_EQ(session.history().undo_steps(), 1u);
}

// --- The brush's controls ----------------------------------------------------------------------

BrushMode brush_on() {
    BrushMode b;
    b.toggle();
    return b;
}

TEST(BrushKeys, XSwapsDodgeAndBurn) {
    BrushMode b = brush_on();
    EXPECT_EQ(b.tone(), BrushTone::dodge);
    EXPECT_TRUE(brush_key(b, BrushKey::x, false));
    EXPECT_EQ(b.tone(), BrushTone::burn);
    EXPECT_TRUE(brush_key(b, BrushKey::x, false));
    EXPECT_EQ(b.tone(), BrushTone::dodge);
}

TEST(BrushKeys, BracketsMakeTheBrushBiggerOrSmallerByTwelveTenths) {
    BrushMode b = brush_on();
    ASSERT_EQ(b.radius(), brush_radius_default);
    EXPECT_TRUE(brush_key(b, BrushKey::right_bracket, false));
    EXPECT_FLOAT_EQ(b.radius(), brush_radius_default * 1.2f);
    EXPECT_TRUE(brush_key(b, BrushKey::left_bracket, false));
    EXPECT_TRUE(brush_key(b, BrushKey::left_bracket, false));
    EXPECT_FLOAT_EQ(b.radius(), brush_radius_default / 1.2f);
    EXPECT_EQ(b.softness(), brush_softness_default);  // the size keys leave the edge
}

TEST(BrushKeys, TheSizeStopsAtTheEndsOfItsRange) {
    BrushMode b = brush_on();
    for (int i = 0; i < 100; ++i) {
        (void)brush_key(b, BrushKey::right_bracket, false);
    }
    EXPECT_EQ(b.radius(), brush_radius_max);
    EXPECT_FALSE(brush_key(b, BrushKey::right_bracket, false));
    for (int i = 0; i < 100; ++i) {
        (void)brush_key(b, BrushKey::left_bracket, false);
    }
    EXPECT_EQ(b.radius(), brush_radius_min);
    EXPECT_FALSE(brush_key(b, BrushKey::left_bracket, false));
}

TEST(BrushKeys, ShiftBracketsMakeTheEdgeATenthSofterOrHarderInExactTenths) {
    BrushMode b = brush_on();
    ASSERT_EQ(b.softness(), 0.5f);
    EXPECT_TRUE(brush_key(b, BrushKey::left_bracket, true));
    EXPECT_FLOAT_EQ(b.softness(), 0.6f);
    EXPECT_TRUE(brush_key(b, BrushKey::right_bracket, true));
    EXPECT_TRUE(brush_key(b, BrushKey::right_bracket, true));
    EXPECT_FLOAT_EQ(b.softness(), 0.4f);
    EXPECT_EQ(b.radius(), brush_radius_default);  // the edge keys leave the size
    for (int i = 0; i < 20; ++i) {
        (void)brush_key(b, BrushKey::left_bracket, true);
    }
    EXPECT_EQ(b.softness(), brush_softness_max);
    EXPECT_FALSE(brush_key(b, BrushKey::left_bracket, true));
    for (int i = 0; i < 20; ++i) {
        (void)brush_key(b, BrushKey::right_bracket, true);
    }
    EXPECT_EQ(b.softness(), brush_softness_min);
    EXPECT_FALSE(brush_key(b, BrushKey::right_bracket, true));
    // Back up through every tenth without drifting: each is the tenth itself (0.7 is 0.7f, not
    // 0.1f added seven times).
    for (int k = 1; k <= 10; ++k) {
        (void)brush_key(b, BrushKey::left_bracket, true);
        EXPECT_EQ(b.softness(), static_cast<float>(k) / 10.0f) << k;
    }
}

TEST(BrushKeys, DoNothingWhileBrushModeIsOff) {
    BrushMode b;
    EXPECT_FALSE(brush_key(b, BrushKey::x, false));
    EXPECT_FALSE(brush_key(b, BrushKey::right_bracket, false));
    EXPECT_FALSE(brush_key(b, BrushKey::left_bracket, true));
    EXPECT_EQ(b.tone(), BrushTone::dodge);
    EXPECT_EQ(b.radius(), brush_radius_default);
    EXPECT_EQ(b.softness(), brush_softness_default);
}

TEST(BrushKeys, TheSizeAndEdgeKeepTheirRangesWhateverTheyAreGiven) {
    EXPECT_EQ(clamp_brush_radius(10.0f), brush_radius_max);
    EXPECT_EQ(clamp_brush_radius(0.0f), brush_radius_min);
    EXPECT_EQ(clamp_brush_radius(std::nanf("")), brush_radius_default);
    EXPECT_EQ(snap_brush_softness(-1.0f), 0.0f);
    EXPECT_EQ(snap_brush_softness(2.0f), 1.0f);
    EXPECT_EQ(snap_brush_softness(0.44f), snap_brush_softness(0.4f));
    EXPECT_EQ(snap_brush_softness(std::nanf("")), brush_softness_default);
}

TEST(BrushStatus, SaysTheToneTheStrengthAndWhatItIsUnderTheEnlarger) {
    EXPECT_EQ(brush_status_text(BrushTone::burn, 0.5f),
              "Burn 0.5 stop — darker, like more time under the enlarger");
    EXPECT_EQ(brush_status_text(BrushTone::dodge, 0.5f),
              "Dodge 0.5 stop — lighter, like holding back light");
    EXPECT_EQ(brush_status_text(BrushTone::dodge, 1.0f),
              "Dodge 1.0 stop — lighter, like holding back light");
    EXPECT_EQ(brush_status_text(BrushTone::burn, 1.5f),
              "Burn 1.5 stops — darker, like more time under the enlarger");
}

TEST(BrushStatus, ShowsTheStrokesToneElseTheOneTheNextPressWouldPaint) {
    EXPECT_EQ(brush_shown_tone(BrushTone::dodge, false, std::nullopt), BrushTone::dodge);
    EXPECT_EQ(brush_shown_tone(BrushTone::dodge, true, std::nullopt), BrushTone::burn);
    EXPECT_EQ(brush_shown_tone(BrushTone::burn, true, std::nullopt), BrushTone::dodge);
    // Alt let go or pressed during a stroke: the stroke's tone.
    EXPECT_EQ(brush_shown_tone(BrushTone::dodge, false, BrushTone::burn), BrushTone::burn);
    EXPECT_EQ(brush_shown_tone(BrushTone::dodge, true, BrushTone::dodge), BrushTone::dodge);
}

TEST(BrushRingRadii, AreTheBrushOnTheScreenAtTheCurrentZoom) {
    // A Retina canvas (2 device pixels per screen unit) and a 2560 x 1707 preview.
    const ViewGeometry g{10.0f, 20.0f, 800.0f, 600.0f, 2.0f, 2560, 1707};
    for (const ViewState& s : {ViewState{}, view_zoomed_to(ViewState{}, g, 1.0f),
                               view_zoomed_to(ViewState{}, g, 3.0f)}) {
        const float radius = 0.05f;  // 128 preview pixels
        const ViewPoint a = view_image_to_screen(s, g, {1000.0f, 800.0f});
        const ViewPoint b = view_image_to_screen(s, g, {1000.0f + 128.0f, 800.0f});
        const BrushRing ring = brush_ring(s, g, radius, 0.25f);
        EXPECT_NEAR(ring.outer, b.x - a.x, 1e-3f);
        EXPECT_NEAR(ring.inner, (b.x - a.x) * 0.75f, 1e-3f);
    }
    const ViewState one = view_zoomed_to(ViewState{}, g, 1.0f);
    EXPECT_FLOAT_EQ(brush_ring(one, g, 0.05f, 0.25f).outer, 64.0f);  // 128 px over 2 per unit
}

TEST(BrushRingRadii, TheInnerRingFollowsTheSoftness) {
    const ViewGeometry g{0.0f, 0.0f, 1000.0f, 1000.0f, 1.0f, 1000, 500};
    const ViewState s = view_zoomed_to(ViewState{}, g, 1.0f);
    EXPECT_FLOAT_EQ(brush_ring(s, g, 0.1f, 0.0f).inner, 100.0f);  // hard: one ring
    EXPECT_FLOAT_EQ(brush_ring(s, g, 0.1f, 1.0f).inner, 0.0f);    // all soft
    EXPECT_FLOAT_EQ(brush_ring(s, g, 0.1f, 0.5f).inner, 50.0f);
    EXPECT_FLOAT_EQ(brush_ring(s, g, 0.1f, 0.5f).outer, 100.0f);
    EXPECT_EQ(brush_ring(s, ViewGeometry{}, 0.1f, 0.5f).outer, 0.0f);  // no picture
}

TEST(StrokeMask, ShowsWhileMIsHeldInBrushModeAndNotWhileTyping) {
    EXPECT_TRUE(stroke_mask_shown(true, true, false));
    EXPECT_FALSE(stroke_mask_shown(true, false, false));  // M let go
    EXPECT_FALSE(stroke_mask_shown(false, true, false));
    EXPECT_FALSE(stroke_mask_shown(true, true, true));
}

TEST(StrokeMask, IsClearWithoutStrokesAndTowardsEachColourWithTheStops) {
    const std::uint32_t dodge = 0xC0'10'20'30u;
    const std::uint32_t burn = 0xC0'A0'B0'C0u;
    EXPECT_EQ(stroke_mask_abgr(128, dodge, burn), 0u);
    EXPECT_EQ(stroke_mask_abgr(255, dodge, burn), dodge);  // 2 stops dodged or more
    EXPECT_EQ(stroke_mask_abgr(1, dodge, burn), burn);     // 2 stops burned or more
    EXPECT_EQ(stroke_mask_abgr(stroke_mask_byte(1.0f), dodge, burn) & 0x00FFFFFFu,
              dodge & 0x00FFFFFFu);
    EXPECT_EQ(stroke_mask_abgr(stroke_mask_byte(-1.0f), dodge, burn) & 0x00FFFFFFu,
              burn & 0x00FFFFFFu);
    // The alpha grows with the stops.
    const auto alpha = [&](float stops) {
        return stroke_mask_abgr(stroke_mask_byte(stops), dodge, burn) >> 24;
    };
    EXPECT_LT(alpha(0.1f), alpha(0.5f));
    EXPECT_LT(alpha(0.5f), alpha(1.0f));
    EXPECT_LT(alpha(-0.5f), alpha(-1.5f));
    EXPECT_EQ(alpha(1.0f), 97u);  // 0xC0 (192) × 64 / 127 at one stop (byte 192), rounded
}

// --- Leaving Brush mode --------------------------------------------------------------------------

TEST(BrushLeaves, WhenTheViewMovesBeforeDodgeAndBurn) {
    EXPECT_TRUE(brush_leaves(true, BrushEvent::view_changed, StageId::decode));
    EXPECT_TRUE(brush_leaves(true, BrushEvent::view_changed, StageId::exposure));
    EXPECT_FALSE(brush_leaves(true, BrushEvent::view_changed, StageId::dodge_burn));
    EXPECT_FALSE(brush_leaves(true, BrushEvent::view_changed, StageId::white_balance));
    EXPECT_FALSE(brush_leaves(true, BrushEvent::view_changed, final_stage));
}

TEST(BrushLeaves, WhenAnotherPhotoIsOpenedWhateverIsViewed) {
    for (const StageId viewed : {StageId::decode, StageId::dodge_burn, final_stage}) {
        EXPECT_TRUE(brush_leaves(true, BrushEvent::photo_opened, viewed));
    }
    // The brush's own settings stay with it, as the strength does.
    BrushMode b = brush_on();
    b.set_tone(BrushTone::burn);
    b.set_radius(0.1f);
    b.set_softness(0.2f);
    b.opened();
    EXPECT_FALSE(b.on());
    EXPECT_EQ(b.tone(), BrushTone::burn);
    EXPECT_EQ(b.radius(), 0.1f);
    EXPECT_EQ(b.softness(), 0.2f);
}

TEST(BrushLeaves, OnEscWithNoStrokeUnderWay) {
    // Esc reaches the brush (escape_action), the brush has no stroke to cancel (BrushGesture),
    // and so Brush mode ends.
    EscapeContext c;
    c.brushing = true;
    c.viewing_earlier = true;
    ASSERT_EQ(escape_action(c), EscapeAction::leave_brush);
    BrushGesture g;
    ASSERT_EQ(kinds(g.escape()), (std::vector<Kind>{Kind::leave_brush}));
    EXPECT_TRUE(brush_leaves(true, BrushEvent::escape, final_stage));
    // With a stroke under way the same Esc cancels the stroke and Brush mode stays.
    (void)press(g, {0.0f, 0.0f});
    EXPECT_EQ(kinds(g.escape()), (std::vector<Kind>{Kind::cancel_stroke}));
}

TEST(BrushLeaves, NeverWhenItIsOffAlready) {
    for (const BrushEvent event :
         {BrushEvent::view_changed, BrushEvent::photo_opened, BrushEvent::escape}) {
        EXPECT_FALSE(brush_leaves(false, event, StageId::decode));
    }
}

TEST(BrushMenu, IsCheckedWhileOnAndTurnsOnOnlyWhereTheBrushWorks) {
    // On: checked, and it can always be turned off, even where the brush isn't available.
    EXPECT_EQ(brush_menu_item(true, true, final_stage), (BrushMenuItem{true, true, false}));
    EXPECT_EQ(brush_menu_item(true, true, StageId::exposure), (BrushMenuItem{true, true, false}));
    // Off with a photo: on where it works, else greyed out with the reason.
    EXPECT_EQ(brush_menu_item(false, true, final_stage), (BrushMenuItem{false, true, false}));
    EXPECT_EQ(brush_menu_item(false, true, StageId::dodge_burn),
              (BrushMenuItem{false, true, false}));
    EXPECT_EQ(brush_menu_item(false, true, StageId::exposure), (BrushMenuItem{false, false, true}));
    // No photo: greyed out, with nothing to explain.
    EXPECT_EQ(brush_menu_item(false, false, final_stage), (BrushMenuItem{false, false, false}));
    EXPECT_EQ(brush_menu_item(false, false, StageId::exposure),
              (BrushMenuItem{false, false, false}));
}

TEST(BrushMenu, ChoosingItTogglesBrushModeAsBDoes) {
    // The item calls the same toggle as B and the row's button: tools_after_key with the menu's
    // own availability.
    const CanvasTools off{};
    const CanvasTools on{false, false, false, true};
    EXPECT_EQ(tools_after_key(off, ToolKey::brush, true, true,
                              brush_menu_item(false, true, final_stage).enabled),
              on);
    EXPECT_EQ(tools_after_key(on, ToolKey::brush, true, true,
                              brush_menu_item(true, true, final_stage).enabled),
              off);
    EXPECT_EQ(tools_after_key(off, ToolKey::brush, true, true,
                              brush_menu_item(false, true, StageId::exposure).enabled),
              off);
}

// --- The stroke mask, bit for bit -------------------------------------------------------------

// 1501 x 701 is just over parallel_min_pixels (2^20), so the mask and the field are split over
// several threads; odd sizes so that no chunk of rows lines up with anything.
constexpr int mask_w = 1501;
constexpr int mask_h = 701;

std::shared_ptr<const Image> mask_base() {
    auto img = std::make_shared<Image>();
    img->width = mask_w;
    img->height = mask_h;
    img->pixels.assign(static_cast<std::size_t>(mask_w) * mask_h * 3, 0.18f);
    return img;
}

// Strokes painted the way the app paints them: a press, moves past the spacing, a release, each
// point through StrokePainter (one new stroke object per point). Dodges and burns, hard and soft
// edges, overlapping and running off the picture's edge.
Session& painted_session(Session& session, FieldCache* grow_cache = nullptr) {
    const BrushCanvas picture{mask_w, mask_h};
    StrokePainter painter;
    struct Plan {
        BrushTone tone;
        float radius;
        float softness;
        float strength;
        std::vector<ViewPoint> path;
    };
    const std::vector<Plan> plans{
        {BrushTone::dodge, 0.05f, 0.5f, 0.5f, {{100, 100}, {400, 180}, {700, 150}, {1000, 400}}},
        {BrushTone::burn,
         0.08f,
         0.0f,
         1.5f,
         {{1400, 50}, {1300, 600}, {1550, 690}}},             // off the edge
        {BrushTone::dodge, 0.02f, 1.0f, 2.0f, {{750, 350}}},  // one dot
        {BrushTone::burn, 0.03f, 0.3f, 0.7f, {{200, 650}, {600, 300}, {900, 380}, {1100, 100}}},
    };
    for (const Plan& plan : plans) {
        BrushStyle style;
        style.radius = plan.radius;
        style.softness = plan.softness;
        style.strength = plan.strength;
        BrushGesture g;
        const float spacing = min_point_spacing(plan.radius, mask_w);
        std::vector<BrushAction> actions =
            g.press(plan.path.front(), {}, plan.tone, spacing, picture);
        for (std::size_t i = 1; i < plan.path.size(); ++i) {
            // Several moves between each point of the plan, as the mouse sends them.
            const ViewPoint a = plan.path[i - 1];
            const ViewPoint b = plan.path[i];
            for (int step = 1; step <= 12; ++step) {
                const float t = static_cast<float>(step) / 12.0f;
                for (const BrushAction& m :
                     g.move({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) {
                    actions.push_back(m);
                }
            }
        }
        for (const BrushAction& r : g.release(plan.path.back())) {
            actions.push_back(r);
        }
        for (const BrushAction& action : actions) {
            EXPECT_EQ(painter.apply(action, style, picture, session), std::nullopt);
            if (grow_cache != nullptr) {
                (void)grow_cache->update(session.settings().strokes, stage_grid(mask_w, mask_h), 1);
            }
        }
    }
    return session;
}

std::vector<std::uint8_t> render_mask(const EditSettings& settings, int threads,
                                      FieldCache* cache = nullptr) {
    PreviewOptions options;
    options.stroke_mask = true;
    const std::optional<PreviewFrame> frame =
        render_preview(*mask_base(), settings, threads, {}, {}, final_stage, options, cache);
    EXPECT_TRUE(frame.has_value());
    return frame ? frame->stroke_mask : std::vector<std::uint8_t>{};
}

TEST(StrokeMaskRender, IsTheSameOnOneThreadAndManyAndAsTheFieldItselfSays) {
    Session session;
    const EditSettings& settings = painted_session(session).settings();
    ASSERT_EQ(settings.strokes.field()->strokes.size(), 4u);

    // The reference, straight from the stroke field: rasterise_field on the whole picture, then
    // one byte per pixel.
    std::vector<float> field(static_cast<std::size_t>(mask_w) * mask_h);
    rasterise_field(*settings.strokes.field(), PixelGrid::whole(mask_w, mask_h), 0, mask_h, field);
    std::vector<std::uint8_t> reference(field.size());
    for (std::size_t p = 0; p < field.size(); ++p) {
        reference[p] = stroke_mask_byte(field[p]);
    }
    // Something to compare: dodged, burned and untouched pixels.
    EXPECT_GT(
        std::count_if(reference.begin(), reference.end(), [](std::uint8_t b) { return b > 128; }),
        1000);
    EXPECT_GT(
        std::count_if(reference.begin(), reference.end(), [](std::uint8_t b) { return b < 128; }),
        1000);
    EXPECT_GT(std::count(reference.begin(), reference.end(), std::uint8_t{128}), 1000);

    const std::vector<std::uint8_t> one = render_mask(settings, 1);
    const std::vector<std::uint8_t> many = render_mask(settings, 7);
    const std::vector<std::uint8_t> all = render_mask(settings, 0);
    ASSERT_EQ(one.size(), reference.size());
    ASSERT_EQ(many.size(), reference.size());
    EXPECT_TRUE(one == reference);
    EXPECT_TRUE(many == reference);
    EXPECT_TRUE(all == reference);
    // The first pixel that differs, if any, for the failure message.
    for (std::size_t p = 0; p < reference.size(); ++p) {
        if (many[p] != reference[p]) {
            ADD_FAILURE() << "pixel " << p % mask_w << ", " << p / mask_w << ": " << int(many[p])
                          << " on 7 threads, " << int(reference[p]) << " from the field";
            break;
        }
    }
}

TEST(StrokeMaskRender, IsTheSameFromTheFieldTheBrushGrewPointByPoint) {
    // The preview's cache follows the strokes as they are painted (FieldPath::grow and append);
    // its mask must be the one a fresh render gives.
    Session session;
    FieldCache grown;
    const EditSettings& settings = painted_session(session, &grown).settings();
    const std::vector<std::uint8_t> fresh = render_mask(settings, 1);
    const std::vector<std::uint8_t> from_grown = render_mask(settings, 7, &grown);
    EXPECT_EQ(grown.last_path(), FieldPath::reuse);  // the render used the grown field as it was
    EXPECT_TRUE(from_grown == fresh);
}

}  // namespace
}  // namespace prism::gui
