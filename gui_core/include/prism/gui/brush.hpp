#pragma once

#include <vector>

#include "prism/gui/view_transform.hpp"
#include "prism/stroke_field.hpp"

namespace prism::gui {

// The Brush's strokes, from the mouse to the stroke field (stroke_field.hpp), as pure state with no
// window involved. The app feeds the canvas's presses, moves and releases in; what comes out says
// what to do to the strokes.

// What a stroke does to the light: dodge holds light back (lighter), burn adds it (darker).
enum class BrushTone {
    dodge,
    burn,
};

// The other one: Alt at a press, and X.
[[nodiscard]] BrushTone other_tone(BrushTone tone) noexcept;

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
    [[nodiscard]] std::vector<BrushAction> move(ViewPoint at);
    [[nodiscard]] std::vector<BrushAction> release(ViewPoint at);
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

}  // namespace prism::gui
