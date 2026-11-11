#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // Where the picture sits on the photo when it is the framed base (Session::preview_framing):
    // canvas pixels are then framed pixels at framing->scale, while the strokes stay on the whole
    // photo, framing->photo_width x photo_height. None: the picture is the whole photo.
    std::optional<PictureFrame> framing = std::nullopt;
};

// The canvas of `session`'s preview base: its size and its framing (Session::preview_framing). An
// empty canvas (0 x 0) with no photo open.
[[nodiscard]] BrushCanvas brush_canvas(const Session& session);

// The whole photo the strokes are on, in pixels: framing's photo size, or the canvas's own size.
// Every brush distance is measured on it (the field's aspect, the radius, the point spacing), never
// on the framed picture.
[[nodiscard]] PixelSize brush_photo_size(const BrushCanvas& canvas) noexcept;

// The whole photo's long side in canvas pixels: the long side of brush_photo_size over the
// framing's scale (rounded, at least 1), the canvas's own long side without framing, and 0 for an
// empty canvas. What min_point_spacing and the ring measure the radius on.
[[nodiscard]] int brush_long_side(const BrushCanvas& canvas) noexcept;

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
// the picture's top left corner, one unit per preview pixel). On a framed canvas the point is a
// framed position (times the framing's scale), and (u, v) is the whole-photo point under it
// (FrameMap::to_photo), so a stroke lands on the subject whatever the frame. (0, 0) for an empty
// canvas.
[[nodiscard]] FieldPoint brush_point(ViewPoint image, const BrushCanvas& canvas);

// The inverse: where the photo point `p` is on the canvas, in image coordinates, for drawing
// strokes and rings over the picture. Off the canvas for a point the frame leaves out. (0, 0) for
// an empty canvas.
[[nodiscard]] ViewPoint brush_canvas_point(FieldPoint p, const BrushCanvas& canvas);

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

// What a stroke is painted with: the brush as it is when the stroke starts.
struct BrushStyle {
    float radius = brush_radius_default;
    float softness = brush_softness_default;
    float strength = brush_strength_default;  // stops, without a sign: the tone gives it one
};

// The stroke's weight in stops (FieldStroke::weight): + to dodge, - to burn.
[[nodiscard]] float brush_weight(BrushTone tone, float strength) noexcept;

// Applies a BrushGesture's actions to the Session's strokes, one undo step a stroke:
//   begin_stroke   a gesture begins (Session::begin_gesture), with the brush of `style`; if the
//                  Dodge & burn switch is off it is switched on inside the gesture, so the stroke
//                  and the switch are one undo step (as the neutral picker does for White balance);
//   add_point      the first makes the stroke, after every stroke already there, and each one
//                  after that is added to its end: a new stroke object each time, with the old
//                  points first, so the preview's FieldCache only rasterises what is new;
//   commit_stroke  the gesture ends: one undo step;
//   cancel_stroke  the strokes go back to the very ones there were before (the same objects), and
//                  the gesture ends where it began (the switch back off if the stroke turned it
//                  on): no undo step, and redo is left as it was.
// leave_brush and passthrough are not the painter's. An action out of turn (a point without a
// begin) does nothing. On the first stroke of a photo the field's aspect is set from `canvas`:
// the whole photo's (brush_photo_size), never the framed picture's.
class StrokePainter {
public:
    [[nodiscard]] MaybeError apply(const BrushAction& action, const BrushStyle& style,
                                   BrushCanvas canvas, Session& session);
    // Between a begin_stroke and its commit or cancel.
    [[nodiscard]] bool painting() const noexcept { return before_.has_value(); }

private:
    MaybeError set_strokes(Session& session, Strokes strokes);

    std::optional<Strokes> before_;  // the strokes before this one
    bool switched_on_ = false;       // this stroke turned the Dodge & burn switch on
    FieldStroke stroke_;             // this one so far
};

// --- The brush's controls ----------------------------------------------------------------------

// The keys that change the brush while Brush mode is on.
enum class BrushKey {
    x,              // Dodge <-> Burn
    left_bracket,   // [ smaller; with Shift, softer
    right_bracket,  // ] bigger; with Shift, harder
};

// What `key` (with Shift or not) does to `brush`: X swaps the tone; ] makes the size
// brush_size_step times bigger and [ that much smaller; Shift+[ makes the edge a tenth softer and
// Shift+] a tenth harder, each within its range. Nothing while Brush mode is off. True if the brush
// changed (not at the end of a range).
bool brush_key(BrushMode& brush, BrushKey key, bool shift) noexcept;

// The two halves of the canvas's tone chip, which X also switches between.
inline constexpr std::string_view brush_dodge_label = "Dodge (lighter)";
inline constexpr std::string_view brush_burn_label = "Burn (darker)";

// The tone the canvas shows: the stroke's while one is under way, else the base tone, or the other
// one while Alt is held (the tone the next press would paint in).
[[nodiscard]] BrushTone brush_shown_tone(BrushTone base, bool alt_held,
                                         std::optional<BrushTone> stroke) noexcept;

// The status chip while brushing: the tone, the strength without a sign (brush_strength_text) and
// what it does under the enlarger. "Dodge 0.5 stop — lighter, like holding back light".
[[nodiscard]] std::string brush_status_text(BrushTone tone, float strength);

// The cursor's rings, in screen units, at the view's zoom: the brush's radius (outer) and the part
// of it at full strength (inner, radius * (1 - softness), as stroke_field.hpp draws it). The radius
// is a fraction of the long side of the picture in `g`.
struct BrushRing {
    float outer = 0.0f;
    float inner = 0.0f;
};
[[nodiscard]] BrushRing brush_ring(const ViewState& s, const ViewGeometry& g, float radius,
                                   float softness) noexcept;
// The same on `canvas`, whose picture is the one in `g`: the radius is a fraction of the WHOLE
// photo's long side (brush_long_side), framed or not, so the ring is the circle a stroke paints.
// Without framing it is the ring above.
[[nodiscard]] BrushRing brush_ring(const ViewState& s, const ViewGeometry& g,
                                   const BrushCanvas& canvas, float radius,
                                   float softness) noexcept;

// Whether the stroke mask shows: while M is held in Brush mode, and not while typing.
[[nodiscard]] bool stroke_mask_shown(bool brushing, bool m_held, bool typing) noexcept;

// One pixel of the stroke mask's overlay, from a byte of PreviewFrame::stroke_mask: transparent
// where no stroke reaches (128), and towards 255 the dodge colour, towards 1 the burn colour, its
// alpha growing with how far the byte is from 128 (full at 2 stops). Colours are ABGR (as SDL's
// ABGR8888 lays bytes out: R in the low byte, A in the high one).
[[nodiscard]] std::uint32_t stroke_mask_abgr(std::uint8_t byte, std::uint32_t dodge_abgr,
                                             std::uint32_t burn_abgr) noexcept;

}  // namespace prism::gui
