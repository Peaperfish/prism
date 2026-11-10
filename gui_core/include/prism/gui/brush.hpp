#pragma once

#include <optional>
#include <vector>

#include "prism/gui/canvas_input.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/stroke_field.hpp"

namespace prism::gui {

// The Brush's strokes, from the mouse to the stroke field (stroke_field.hpp), as pure state with no
// window involved. The app feeds the canvas's presses, moves and releases in; what comes out says
// what to do to the strokes.

// The keys held at a press.
struct BrushModifiers {
    bool alt = false;    // Alt / Option: this stroke uses the other tone
    bool space = false;  // the hand: the press pans the picture instead
};

// The picture a stroke is drawn on: the preview base, in pixels.
struct BrushCanvas {
    int width = 0;
    int height = 0;
};

enum class BrushActionKind {
    begin_stroke,   // a stroke starts, in `tone`; one undo step from here
    add_point,      // `point` goes on the end of the stroke
    commit_stroke,  // the stroke is finished: the undo step ends
    cancel_stroke,  // the stroke goes, as if never drawn, and leaves no undo step
    leave_brush,    // Esc with no stroke under way: Brush mode ends
    passthrough,    // not a stroke: the press, move or release belongs to the hand (Space)
};

struct BrushAction {
    BrushActionKind kind = BrushActionKind::passthrough;
    BrushTone tone = BrushTone::dodge;  // begin_stroke only
    FieldPoint point;                   // add_point only

    friend bool operator==(const BrushAction&, const BrushAction&) = default;
};

// The (u, v) of stroke_field.hpp for a point in image coordinates (view_transform.hpp's: (0, 0) at
// the picture's top left corner, one unit per preview pixel). (0, 0) for an empty canvas.
[[nodiscard]] FieldPoint brush_point(ViewPoint image, BrushCanvas canvas) noexcept;

// One press of the mouse on the canvas in Brush mode, from press to release.
//
// A press starts a stroke and puts its first point down; a move adds a point only once it is at
// least `spacing` (min_point_spacing, in long-side units) from the last point kept, so many small
// moves add a point each time they have gone that far in all. A release adds its own point on the
// same rule, so a press and release without a move is still a stroke: one dot.
//
// Alt held at the press paints that one stroke in the other tone; Alt pressed or let go during the
// stroke changes nothing, and the next stroke is in the base tone again. Space held at the press
// makes the whole press the hand's (passthrough, to its release); Space pressed during a stroke
// leaves it a stroke. Esc during a stroke cancels it, and the rest of that press does nothing; Esc
// with no stroke under way leaves Brush mode. A release with no press, and a second press during a
// stroke, do nothing.
class BrushGesture {
public:
    [[nodiscard]] std::vector<BrushAction> press(ViewPoint at, BrushModifiers keys, BrushTone base,
                                                 float spacing, BrushCanvas canvas);
    // `keys` are those held now: they change nothing once the press has been made.
    [[nodiscard]] std::vector<BrushAction> move(ViewPoint at, BrushModifiers keys = {});
    [[nodiscard]] std::vector<BrushAction> release(ViewPoint at, BrushModifiers keys = {});
    // Esc.
    [[nodiscard]] std::vector<BrushAction> escape();
    // The press is over without a release (Brush mode left, the window lost the mouse): a stroke
    // under way is committed as it is, so what was painted stays.
    [[nodiscard]] std::vector<BrushAction> finish();

    // A stroke is under way: the button is down and it hasn't been cancelled.
    [[nodiscard]] bool stroking() const noexcept { return state_ == State::stroking; }
    // The press belongs to the hand.
    [[nodiscard]] bool panning() const noexcept { return state_ == State::panning; }
    // The tone of the stroke under way (or of the last one).
    [[nodiscard]] BrushTone tone() const noexcept { return tone_; }

private:
    enum class State {
        idle,
        stroking,
        panning,    // Space at the press: the hand's, until the release
        cancelled,  // Esc during the stroke: nothing until the release
    };
    // An add_point for `at` if it is far enough from the last point kept.
    void add_if_far(ViewPoint at, std::vector<BrushAction>& out);

    State state_ = State::idle;
    BrushTone tone_ = BrushTone::dodge;
    float spacing_ = 0.0f;
    BrushCanvas canvas_;
    FieldPoint last_;
};

// --- Painting the strokes ----------------------------------------------------------------------

// The brush's size and edge where it starts: a radius of 5% of the picture's long side, with the
// outer half of it fading (FieldStroke::radius and ::softness).
inline constexpr float brush_radius_default = 0.05f;
inline constexpr float brush_softness_default = 0.5f;

// What a stroke is painted with: the brush as it is when the stroke starts.
struct BrushStyle {
    float radius = brush_radius_default;
    float softness = brush_softness_default;
    float strength = brush_strength_default;  // stops, without a sign: the tone gives it one
};

// The stroke's weight in stops (FieldStroke::weight): + to dodge, - to burn.
[[nodiscard]] float brush_weight(BrushTone tone, float strength) noexcept;

// Applies a BrushGesture's actions to the Session's strokes, one undo step a stroke:
//   begin_stroke   a gesture begins (Session::begin_gesture), with the brush of `style`;
//   add_point      the first makes the stroke, after every stroke already there, and each one
//                  after that is added to its end: a new stroke object each time, with the old
//                  points first, so the preview's FieldCache only rasterises what is new;
//   commit_stroke  the gesture ends: one undo step;
//   cancel_stroke  the strokes go back to the very ones there were before (the same objects), and
//                  the gesture ends where it began: no undo step, and redo is left as it was.
// leave_brush and passthrough are not the painter's. An action out of turn (a point without a
// begin) does nothing. On the first stroke of a photo the field's aspect is set from `canvas`.
class StrokePainter {
public:
    [[nodiscard]] MaybeError apply(const BrushAction& action, const BrushStyle& style,
                                   BrushCanvas canvas, Session& session);
    // Between a begin_stroke and its commit or cancel.
    [[nodiscard]] bool painting() const noexcept { return before_.has_value(); }

private:
    MaybeError set_strokes(Session& session, Strokes strokes);

    std::optional<Strokes> before_;  // the strokes before this one
    FieldStroke stroke_;             // this one so far
};

}  // namespace prism::gui
