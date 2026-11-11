#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/frame.hpp"
#include "prism/gui/framing.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/image.hpp"

// Framing on the easel (#58), with no window: the view of the turned photo, the handles and what
// dragging them does, turning about the frame's centre, the aspect, the draft's undo steps, the
// keys, the scopes' source and the words.

namespace fs = std::filesystem;

namespace {

using prism::Aspect;
using prism::Frame;
using prism::PixelSize;
using prism::gui::CanvasPoint;
using prism::gui::CanvasRect;
using prism::gui::FrameEdit;
using prism::gui::FrameHandle;
using prism::gui::FramingState;
using prism::gui::FramingView;
using prism::gui::Session;

constexpr int photo_w = 300;
constexpr int photo_h = 200;

Frame frame_of(float angle, float left, float top, float right, float bottom,
               Aspect aspect = Aspect::free, bool portrait = false) {
    Frame f;
    f.angle = angle;
    f.left = left;
    f.top = top;
    f.right = right;
    f.bottom = bottom;
    f.aspect = aspect;
    f.portrait = portrait;
    return f;
}

// The photo position of each corner of `f`'s rectangle (on a W x H photo), as session 1's tests
// work it out: straight from prism/frame.hpp's formula, not from the code under test.
std::vector<CanvasPoint> corners_on_photo(const Frame& f, int w, int h) {
    const double a = static_cast<double>(f.angle) * (std::acos(-1.0) / 180.0);
    const double c = std::cos(a);
    const double s = std::sin(a);
    std::vector<CanvasPoint> out;
    for (const float x : {f.left, f.right}) {
        for (const float y : {f.top, f.bottom}) {
            const double dx = (static_cast<double>(x) - 0.5) * w;
            const double dy = (static_cast<double>(y) - 0.5) * h;
            out.push_back({w / 2.0 + c * dx + s * dy, h / 2.0 - s * dx + c * dy});
        }
    }
    return out;
}

// The photo point under the frame's centre, by the same formula.
CanvasPoint centre_on_photo(const Frame& f, int w, int h) {
    const double a = static_cast<double>(f.angle) * (std::acos(-1.0) / 180.0);
    const double dx = ((static_cast<double>(f.left) + f.right) / 2.0 - 0.5) * w;
    const double dy = ((static_cast<double>(f.top) + f.bottom) / 2.0 - 0.5) * h;
    return {w / 2.0 + std::cos(a) * dx + std::sin(a) * dy,
            h / 2.0 - std::sin(a) * dx + std::cos(a) * dy};
}

void expect_on_photo(const Frame& f, int w, int h) {
    const double tol = 1e-6 * std::max(w, h);
    for (const CanvasPoint& p : corners_on_photo(f, w, h)) {
        EXPECT_GE(p.x, -tol);
        EXPECT_LE(p.x, w + tol);
        EXPECT_GE(p.y, -tol);
        EXPECT_LE(p.y, h + tol);
    }
}

double width_px(const Frame& f, int w) {
    return (static_cast<double>(f.right) - f.left) * w;
}
double height_px(const Frame& f, int h) {
    return (static_cast<double>(f.bottom) - f.top) * h;
}

bool same_bits(const Frame& a, const Frame& b) {
    const auto bits = [](float v) { return std::bit_cast<std::uint32_t>(v); };
    return bits(a.angle) == bits(b.angle) && bits(a.left) == bits(b.left) &&
           bits(a.top) == bits(b.top) && bits(a.right) == bits(b.right) &&
           bits(a.bottom) == bits(b.bottom) && a.aspect == b.aspect && a.portrait == b.portrait;
}

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_framing_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// A Session on a photo_w x photo_h PNG, opened at its own size (the preview base is the photo).
struct Opened {
    fs::path png;
    Session session{{.preview_long_side = 4096, .threads = 0}};

    Opened() : png(temp_file("in.png")) {
        prism::Image img;
        img.width = photo_w;
        img.height = photo_h;
        img.pixels.assign(static_cast<std::size_t>(photo_w) * photo_h * 3, 0.25f);
        prism::save_image(png, img);
        // At its own size: no reduction, so the whole base is the photo.
        EXPECT_EQ(session.open_file(png), std::nullopt);
    }
    ~Opened() { fs::remove(png); }
    Opened(const Opened&) = delete;
    Opened& operator=(const Opened&) = delete;
};

// A 900 x 700 canvas with a 20 unit margin on `f`'s angle.
FramingView view_of(const Frame& f) {
    return prism::gui::framing_view(f.angle, photo_w, photo_h, 900.0, 700.0, 20.0);
}

CanvasPoint offset(CanvasPoint p, double dx, double dy) {
    return {p.x + dx, p.y + dy};
}

}  // namespace

// --- Undo steps, Esc and leaving -----------------------------------------------------------------

namespace {

// The middle of `handle`'s edge, or its corner, of `f` drawn in `view`.
CanvasPoint handle_point(const FramingView& view, const Frame& f, FrameHandle handle) {
    const CanvasRect r = prism::gui::frame_rect(view, f);
    const double mx = (r.min.x + r.max.x) / 2.0;
    const double my = (r.min.y + r.max.y) / 2.0;
    switch (handle) {
        case FrameHandle::left: return {r.min.x, my};
        case FrameHandle::right: return {r.max.x, my};
        case FrameHandle::top: return {mx, r.min.y};
        case FrameHandle::bottom: return {mx, r.max.y};
        case FrameHandle::top_left: return r.min;
        case FrameHandle::top_right: return {r.max.x, r.min.y};
        case FrameHandle::bottom_right: return r.max;
        case FrameHandle::bottom_left: return {r.min.x, r.max.y};
        case FrameHandle::inside:
        case FrameHandle::none: break;
    }
    return {mx, my};
}

// Presses `handle` of the draft, moves by (dx, dy) in `steps` steps, and lets go.
prism::gui::MaybeError drag(FramingState& f, Session& session, FrameHandle handle, double dx,
                            double dy, int steps = 5) {
    const FramingView view = view_of(f.draft());
    const CanvasPoint at = handle_point(view, f.draft(), handle);
    EXPECT_TRUE(f.press(at, false, view, 6.0));
    EXPECT_EQ(f.handle(), handle);
    for (int i = 1; i <= steps; ++i) {
        f.move(offset(at, dx * i / steps, dy * i / steps));
    }
    return f.release(offset(at, dx, dy), session);
}

}  // namespace

TEST(FramingUndo, EachGestureIsOneUndoStepAndOneThatChangesNothingNone) {
    Opened o;
    FramingState f;
    ASSERT_TRUE(f.begin(o.session));
    const std::size_t steps = o.session.history().undo_steps();
    // Pressing inside the whole photo and letting go where it was: nothing, and no step.
    const FramingView view = view_of(f.draft());
    const CanvasPoint middle = handle_point(view, f.draft(), FrameHandle::inside);
    ASSERT_TRUE(f.press(middle, false, view, 6.0));
    ASSERT_EQ(f.release(middle, o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps);
    EXPECT_FALSE(o.session.settings().frame.has_value());
    // Free on the uncropped photo moves no pixel either.
    ASSERT_EQ(f.set_aspect(Aspect::free, o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps);

    // A drag of many moves: one step, and the settings hold the draft.
    ASSERT_EQ(drag(f, o.session, FrameHandle::right, -60.0, 0.0, 12), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps + 1);
    ASSERT_TRUE(o.session.settings().frame.has_value());
    EXPECT_TRUE(same_bits(*o.session.settings().frame, f.draft()));
    EXPECT_LT(f.draft().right, 1.0f);
    // The slider through several angles: one step.
    f.begin_turn();
    for (const float a : {1.0f, 2.5f, 4.0f, 3.0f}) {
        f.turn_to(a);
    }
    ASSERT_EQ(f.commit(o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps + 2);
    EXPECT_EQ(o.session.settings().frame->angle, 3.0f);
    // An aspect pick and X: one step each; picking the same aspect again, none.
    ASSERT_EQ(f.set_aspect(Aspect::four_five, o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps + 3);
    ASSERT_EQ(f.set_aspect(Aspect::four_five, o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps + 3);
    ASSERT_EQ(f.flip_aspect(o.session), std::nullopt);
    EXPECT_EQ(o.session.history().undo_steps(), steps + 4);
    EXPECT_TRUE(o.session.settings().frame->portrait);
    // Undo takes back one gesture at a time, and the draft follows.
    ASSERT_EQ(o.session.undo(), std::nullopt);
    f.follow(o.session.settings().frame);
    EXPECT_FALSE(f.draft().portrait);
    EXPECT_EQ(f.draft().aspect, Aspect::four_five);
}

TEST(FramingUndo, EscDuringADragPutsTheFrameBackExactlyAndFramingStaysOn) {
    Opened o;
    FramingState f;
    ASSERT_TRUE(f.begin(o.session));
    f.begin_turn();
    f.turn_to(-3.5f);
    ASSERT_EQ(f.commit(o.session), std::nullopt);
    const Frame before = f.draft();
    const std::size_t steps = o.session.history().undo_steps();
    const FramingView view = view_of(before);
    const CanvasPoint at = handle_point(view, before, FrameHandle::top_left);
    ASSERT_TRUE(f.press(at, false, view, 6.0));
    f.move(offset(at, 40.0, 25.0));
    ASSERT_FALSE(same_bits(f.draft(), before));
    EXPECT_TRUE(f.cancel());
    EXPECT_TRUE(same_bits(f.draft(), before));
    EXPECT_TRUE(f.on());
    EXPECT_EQ(f.gesture(), prism::gui::FramingGesture::none);
    // The release after it does nothing.
    ASSERT_EQ(f.release(offset(at, 40.0, 25.0), o.session), std::nullopt);
    EXPECT_TRUE(same_bits(*o.session.settings().frame, before));
    EXPECT_EQ(o.session.history().undo_steps(), steps);
    EXPECT_FALSE(f.cancel());  // nothing left to cancel
}

TEST(FramingUndo, EveryWayOutOfFramingKeepsTheFrame) {
    Opened o;
    FramingState f;
    ASSERT_TRUE(f.begin(o.session));
    ASSERT_EQ(drag(f, o.session, FrameHandle::left, 50.0, 0.0), std::nullopt);
    const Frame kept = f.draft();
    ASSERT_EQ(f.leave(o.session), std::nullopt);
    EXPECT_FALSE(f.on());
    EXPECT_TRUE(same_bits(*o.session.settings().frame, kept));
    // Back in, and out again in the middle of a drag: the drag is kept too.
    ASSERT_TRUE(f.begin(o.session));
    EXPECT_TRUE(same_bits(f.draft(), kept));
    const FramingView view = view_of(f.draft());
    const CanvasPoint at = handle_point(view, f.draft(), FrameHandle::bottom);
    ASSERT_TRUE(f.press(at, false, view, 6.0));
    f.move(offset(at, 0.0, -40.0));
    const Frame dragged = f.draft();
    ASSERT_EQ(f.leave(o.session), std::nullopt);
    EXPECT_TRUE(same_bits(*o.session.settings().frame, dragged));
    // C, Return and Esc are all ways out, and none of them is a cancel.
    prism::gui::FramingKeyContext c;
    c.has_file = true;
    c.framing = true;
    EXPECT_EQ(prism::gui::framing_key(prism::gui::FramingKey::c, c),
              prism::gui::FramingKeyAction::leave_framing);
    EXPECT_EQ(prism::gui::framing_key(prism::gui::FramingKey::return_key, c),
              prism::gui::FramingKeyAction::leave_framing);
    prism::gui::EscapeContext esc;
    esc.framing = true;
    esc.comparing = true;
    EXPECT_EQ(prism::gui::escape_action(esc), prism::gui::EscapeAction::leave_framing);
}

// --- The aspect lock
// -------------------------------------------------------------------------------

TEST(FramingLock, ALockChosenOnTheUncroppedPhotoSurvivesEveryGestureAndLeaving) {
    Opened o;
    FramingState f;
    ASSERT_TRUE(f.begin(o.session));
    ASSERT_EQ(f.set_aspect(Aspect::free, o.session), std::nullopt);
    EXPECT_FALSE(o.session.settings().frame.has_value());  // still the whole photo
    EXPECT_EQ(o.session.frame_lock(), (prism::gui::FrameLock{Aspect::free, false}));
    ASSERT_EQ(f.leave(o.session), std::nullopt);
    ASSERT_TRUE(f.begin(o.session));
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    // A turn, a move, Reset and undo keep it.
    ASSERT_EQ(f.set_angle(2.0f, o.session), std::nullopt);
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    ASSERT_EQ(drag(f, o.session, FrameHandle::inside, 10.0, 5.0), std::nullopt);
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    ASSERT_EQ(f.reset(o.session), std::nullopt);
    EXPECT_FALSE(o.session.settings().frame.has_value());
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    ASSERT_EQ(o.session.undo(), std::nullopt);
    ASSERT_EQ(o.session.undo(), std::nullopt);
    f.follow(o.session.settings().frame);
    ASSERT_EQ(o.session.undo(), std::nullopt);
    f.follow(o.session.settings().frame);
    EXPECT_FALSE(o.session.settings().frame.has_value());
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    ASSERT_EQ(f.leave(o.session), std::nullopt);
    ASSERT_TRUE(f.begin(o.session));
    EXPECT_EQ(f.draft().aspect, Aspect::free);
    // The lock is the photo's: another photo starts at Original.
    ASSERT_EQ(o.session.open_file(o.png), std::nullopt);
    EXPECT_EQ(o.session.frame_lock(), prism::gui::FrameLock{});
}

// --- The view ------------------------------------------------------------------------------------

TEST(FramingView, PhotoAndCanvasRoundTripAndTheTurnedPhotoFitsTheCanvas) {
    for (const float angle : {0.0f, 7.0f, -7.0f, 45.0f, -45.0f, 0.3f}) {
        const FramingView view =
            prism::gui::framing_view(angle, photo_w, photo_h, 900.0, 700.0, 20.0);
        for (const CanvasPoint p : {CanvasPoint{0.0, 0.0}, CanvasPoint{17.25, 133.5},
                                    CanvasPoint{300.0, 200.0}, CanvasPoint{-40.0, 250.0}}) {
            const CanvasPoint back =
                prism::gui::photo_from_canvas(view, prism::gui::canvas_from_photo(view, p));
            EXPECT_NEAR(back.x, p.x, 1e-6) << angle;
            EXPECT_NEAR(back.y, p.y, 1e-6) << angle;
        }
        // Positive turns clockwise on screen: a point right of the photo's centre goes down.
        const CanvasPoint right = prism::gui::canvas_from_photo(view, {250.0, 100.0});
        if (angle > 0.0f) {
            EXPECT_GT(right.y, view.centre.y) << angle;
        } else if (angle < 0.0f) {
            EXPECT_LT(right.y, view.centre.y) << angle;
        }
        // The turned photo's box is inside the canvas less its margin, and fills it one way.
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const CanvasPoint& c : prism::gui::photo_corners(view)) {
            x0 = std::min(x0, c.x);
            y0 = std::min(y0, c.y);
            x1 = std::max(x1, c.x);
            y1 = std::max(y1, c.y);
        }
        EXPECT_GE(x0, 20.0 - 1e-9);
        EXPECT_GE(y0, 20.0 - 1e-9);
        EXPECT_LE(x1, 880.0 + 1e-9);
        EXPECT_LE(y1, 680.0 + 1e-9);
        EXPECT_TRUE(std::abs(x1 - x0 - 860.0) < 1e-6 || std::abs(y1 - y0 - 660.0) < 1e-6);
        // The frame's rectangle covers the photo positions of its corners.
        const Frame f = prism::fit_frame(frame_of(angle, 0.2f, 0.3f, 0.7f, 0.8f), photo_w, photo_h);
        const CanvasRect r = prism::gui::frame_rect(view, f);
        const std::vector<CanvasPoint> on_photo = corners_on_photo(f, photo_w, photo_h);
        const CanvasPoint tl = prism::gui::photo_from_canvas(view, r.min);
        EXPECT_NEAR(tl.x, on_photo[0].x, 1e-6);
        EXPECT_NEAR(tl.y, on_photo[0].y, 1e-6);
    }
}

// --- Handles and drags
// -----------------------------------------------------------------------------

TEST(FramingDrag, HandlesAreFoundWhereTheyAreDrawn) {
    const Frame f = prism::fit_frame(frame_of(3.0f, 0.2f, 0.2f, 0.8f, 0.8f), photo_w, photo_h);
    const FramingView view = view_of(f);
    for (const FrameHandle h : {FrameHandle::left, FrameHandle::top, FrameHandle::right,
                                FrameHandle::bottom, FrameHandle::top_left, FrameHandle::top_right,
                                FrameHandle::bottom_right, FrameHandle::bottom_left}) {
        EXPECT_EQ(prism::gui::frame_handle_at(view, f, handle_point(view, f, h), 6.0), h);
        // Within reach of it, too.
        EXPECT_EQ(
            prism::gui::frame_handle_at(view, f, offset(handle_point(view, f, h), 3.0, -3.0), 6.0),
            h == FrameHandle::top_left || h == FrameHandle::top_right ||
                    h == FrameHandle::bottom_right || h == FrameHandle::bottom_left
                ? h
                : prism::gui::frame_handle_at(view, f, handle_point(view, f, h), 6.0));
    }
    const CanvasRect r = prism::gui::frame_rect(view, f);
    const CanvasPoint middle{(r.min.x + r.max.x) / 2, (r.min.y + r.max.y) / 2};
    EXPECT_EQ(prism::gui::frame_handle_at(view, f, middle, 6.0), FrameHandle::inside);
    EXPECT_EQ(prism::gui::frame_handle_at(view, f, offset(r.min, -30.0, -30.0), 6.0),
              FrameHandle::none);
}

TEST(FramingDrag, EachHandleMovesItsOwnEdgesAndEveryResultFits) {
    const Frame start =
        prism::fit_frame(frame_of(3.0f, 0.25f, 0.25f, 0.75f, 0.75f), photo_w, photo_h);
    const FramingView view = view_of(start);
    const auto dragged = [&](FrameHandle h, double dx, double dy) {
        const CanvasPoint at = handle_point(view, start, h);
        const std::optional<FrameEdit> e =
            prism::gui::drag_frame(start, h, at, offset(at, dx, dy), view, photo_w, photo_h);
        EXPECT_TRUE(e.has_value());
        return e ? e->frame : start;
    };
    // Inwards, nothing needs fitting: only the dragged edges move.
    Frame f = dragged(FrameHandle::right, -20.0, 0.0);
    EXPECT_LT(f.right, start.right);
    EXPECT_EQ(f.left, start.left);
    EXPECT_EQ(f.top, start.top);
    EXPECT_EQ(f.bottom, start.bottom);
    EXPECT_EQ(f.aspect, Aspect::free);
    f = dragged(FrameHandle::top, 0.0, 20.0);
    EXPECT_GT(f.top, start.top);
    EXPECT_EQ(f.left, start.left);
    EXPECT_EQ(f.right, start.right);
    EXPECT_EQ(f.bottom, start.bottom);
    f = dragged(FrameHandle::bottom_left, 15.0, -10.0);
    EXPECT_GT(f.left, start.left);
    EXPECT_LT(f.bottom, start.bottom);
    EXPECT_EQ(f.right, start.right);
    EXPECT_EQ(f.top, start.top);
    // Outwards, every handle as far as it goes: every corner stays on the turned photo.
    for (const FrameHandle h :
         {FrameHandle::left, FrameHandle::top, FrameHandle::right, FrameHandle::bottom,
          FrameHandle::top_left, FrameHandle::top_right, FrameHandle::bottom_right,
          FrameHandle::bottom_left, FrameHandle::inside}) {
        const CanvasPoint at = handle_point(view, start, h);
        const CanvasPoint c = handle_point(view, start, FrameHandle::inside);
        const double dx = at.x == c.x ? 0.0 : (at.x > c.x ? 500.0 : -500.0);
        const double dy = at.y == c.y ? 0.0 : (at.y > c.y ? 500.0 : -500.0);
        expect_on_photo(dragged(h, dx == 0 && dy == 0 ? 500.0 : dx, dy), photo_w, photo_h);
    }
    // The inside keeps the size, and stops at the photo's edge.
    const Frame moved = dragged(FrameHandle::inside, 500.0, 0.0);
    EXPECT_NEAR(width_px(moved, photo_w), width_px(start, photo_w), 1e-3);
    EXPECT_NEAR(height_px(moved, photo_h), height_px(start, photo_h), 1e-3);
    EXPECT_GT(moved.left, start.left);
    // Never below the smallest side.
    const double least = prism::gui::framing_min_side(photo_w, photo_h);
    f = dragged(FrameHandle::left, 2000.0, 0.0);
    EXPECT_GE(width_px(f, photo_w), least - 1e-3);
    f = dragged(FrameHandle::bottom_right, -2000.0, -2000.0);
    EXPECT_GE(width_px(f, photo_w), least - 1e-3);
    EXPECT_GE(height_px(f, photo_h), least - 1e-3);
}

TEST(FramingDrag, ACornerKeepsALockedShape) {
    const FrameEdit locked = prism::gui::choose_aspect(
        prism::fit_frame(frame_of(-4.0f, 0.2f, 0.2f, 0.8f, 0.8f), photo_w, photo_h),
        Aspect::four_five, photo_w, photo_h);
    const Frame start = locked.frame;
    const double ratio = *prism::aspect_ratio(Aspect::four_five, false, photo_w, photo_h);
    ASSERT_NEAR(width_px(start, photo_w) / height_px(start, photo_h), ratio, 1e-4);
    const FramingView view = view_of(start);
    for (const FrameHandle h : {FrameHandle::top_left, FrameHandle::bottom_right,
                                FrameHandle::top_right, FrameHandle::bottom_left}) {
        for (const auto& [dx, dy] :
             {std::pair{-30.0, 7.0}, std::pair{25.0, 40.0}, std::pair{400.0, -300.0}}) {
            const CanvasPoint at = handle_point(view, start, h);
            const std::optional<FrameEdit> e =
                prism::gui::drag_frame(start, h, at, offset(at, dx, dy), view, photo_w, photo_h);
            ASSERT_TRUE(e.has_value());
            EXPECT_EQ(e->frame.aspect, Aspect::four_five);
            EXPECT_NEAR(width_px(e->frame, photo_w) / height_px(e->frame, photo_h), ratio,
                        1e-4 * ratio);
            expect_on_photo(e->frame, photo_w, photo_h);
        }
    }
}

// --- Turning
// ---------------------------------------------------------------------------------------

TEST(FramingTurn, TurningKeepsTheFramesCentreOnTheSamePhotoPoint) {
    const double tol = 1e-6 * std::max(photo_w, photo_h);
    // A small frame off the middle: no turn ever has to move it.
    const Frame start =
        prism::fit_frame(frame_of(5.0f, 0.55f, 0.3f, 0.7f, 0.45f), photo_w, photo_h);
    const CanvasPoint c0 = centre_on_photo(start, photo_w, photo_h);
    for (const float a : {-45.0f, -12.5f, 0.0f, 0.4f, 17.0f, 45.0f}) {
        const FrameEdit e = prism::gui::turn_frame(start, a, photo_w, photo_h);
        EXPECT_EQ(e.frame.angle, a);
        const CanvasPoint c1 = centre_on_photo(e.frame, photo_w, photo_h);
        EXPECT_NEAR(c1.x, c0.x, tol) << a;
        EXPECT_NEAR(c1.y, c0.y, tol) << a;
        expect_on_photo(e.frame, photo_w, photo_h);
    }
    // Back to its own angle: exactly the start, so a gesture worked out from its start never
    // shrinks for good. And the range is -45 to 45.
    EXPECT_TRUE(same_bits(prism::gui::turn_frame(start, 5.0f, photo_w, photo_h).frame, start));
    EXPECT_EQ(prism::gui::turn_frame(start, 60.0f, photo_w, photo_h).frame.angle, 45.0f);
    // A frame near a corner turned far: if its centre had to move, it moved straight towards the
    // photo's centre.
    const Frame corner =
        prism::fit_frame(frame_of(0.0f, 0.9f, 0.88f, 0.99f, 0.99f), photo_w, photo_h);
    const CanvasPoint p0 = centre_on_photo(corner, photo_w, photo_h);
    const FrameEdit far = prism::gui::turn_frame(corner, 40.0f, photo_w, photo_h);
    const CanvasPoint p1 = centre_on_photo(far.frame, photo_w, photo_h);
    const double cross = (p0.x - photo_w / 2.0) * (p1.y - photo_h / 2.0) -
                         (p0.y - photo_h / 2.0) * (p1.x - photo_w / 2.0);
    EXPECT_NEAR(cross, 0.0, 1e-3 * photo_w * photo_h);
    EXPECT_LE(std::hypot(p1.x - photo_w / 2.0, p1.y - photo_h / 2.0),
              std::hypot(p0.x - photo_w / 2.0, p0.y - photo_h / 2.0) + tol);
    expect_on_photo(far.frame, photo_w, photo_h);
}

TEST(FramingTurn, AHorizonLineIsMadeLevelOrUpright) {
    const FramingView view = view_of(frame_of(2.0f, 0.0f, 0.0f, 1.0f, 1.0f));
    const auto line_at = [](double degrees) {
        const double r = degrees * std::acos(-1.0) / 180.0;
        return CanvasPoint{100.0 * std::cos(r), 100.0 * std::sin(r)};
    };
    // A line 5 degrees clockwise of level on screen: the picture turns 5 degrees less.
    EXPECT_NEAR(*prism::gui::straighten_angle(view, {0.0, 0.0}, line_at(5.0)), -3.0f, 1e-4f);
    EXPECT_NEAR(*prism::gui::straighten_angle(view, line_at(5.0), {0.0, 0.0}), -3.0f, 1e-4f);
    // Nearly upright: made upright.
    EXPECT_NEAR(*prism::gui::straighten_angle(view, {0.0, 0.0}, line_at(80.0)), 12.0f, 1e-4f);
    EXPECT_NEAR(*prism::gui::straighten_angle(view, {0.0, 0.0}, line_at(-85.0)), -3.0f, 1e-4f);
    EXPECT_FALSE(prism::gui::straighten_angle(view, {0.0, 0.0}, {2.0, 1.0}).has_value());
    // The state's horizon gesture turns the draft on release, as one step.
    Opened o;
    FramingState f;
    ASSERT_TRUE(f.begin(o.session));
    const FramingView v = view_of(f.draft());
    const CanvasPoint a{200.0, 300.0};
    ASSERT_TRUE(f.press(a, true, v, 6.0));
    EXPECT_EQ(f.gesture(), prism::gui::FramingGesture::horizon);
    const CanvasPoint b = offset(a, line_at(-3.0).x, line_at(-3.0).y);
    f.move(b);
    ASSERT_TRUE(f.horizon().has_value());
    ASSERT_EQ(f.release(b, o.session), std::nullopt);
    EXPECT_NEAR(o.session.settings().frame->angle, 3.0f, 1e-4f);
}

// --- The aspect
// --------------------------------------------------------------------------------------

TEST(FramingAspect, ChoosingOrTurningTheAspectKeepsTheCentre) {
    const double tol = 1e-6 * std::max(photo_w, photo_h);
    const Frame start =
        prism::fit_frame(frame_of(2.0f, 0.3f, 0.25f, 0.7f, 0.65f), photo_w, photo_h);
    const CanvasPoint c0 = centre_on_photo(start, photo_w, photo_h);
    const FrameEdit picked = prism::gui::choose_aspect(start, Aspect::four_five, photo_w, photo_h);
    EXPECT_EQ(picked.frame.aspect, Aspect::four_five);
    EXPECT_NEAR(width_px(picked.frame, photo_w) / height_px(picked.frame, photo_h),
                *prism::aspect_ratio(Aspect::four_five, false, photo_w, photo_h), 1e-4);
    const CanvasPoint c1 = centre_on_photo(picked.frame, photo_w, photo_h);
    EXPECT_NEAR(c1.x, c0.x, tol);
    EXPECT_NEAR(c1.y, c0.y, tol);
    const FrameEdit turned = prism::gui::turn_aspect(picked.frame, photo_w, photo_h);
    EXPECT_TRUE(turned.frame.portrait);
    EXPECT_NEAR(width_px(turned.frame, photo_w) / height_px(turned.frame, photo_h),
                *prism::aspect_ratio(Aspect::four_five, true, photo_w, photo_h), 1e-4);
    const CanvasPoint c2 = centre_on_photo(turned.frame, photo_w, photo_h);
    EXPECT_NEAR(c2.x, c0.x, tol);
    EXPECT_NEAR(c2.y, c0.y, tol);
    expect_on_photo(turned.frame, photo_w, photo_h);
    // Free only unlocks.
    const FrameEdit free = prism::gui::choose_aspect(picked.frame, Aspect::free, photo_w, photo_h);
    EXPECT_EQ(free.frame.aspect, Aspect::free);
    EXPECT_EQ(free.frame.left, picked.frame.left);
    EXPECT_EQ(free.frame.right, picked.frame.right);
    EXPECT_FALSE(free.fitted);
}

// --- Words
// -------------------------------------------------------------------------------------------

TEST(FramingWords, TheAngleSizeAspectAndFitInWords) {
    using prism::gui::framing_angle_text;
    EXPECT_EQ(framing_angle_text(0.0f), "Upright");
    EXPECT_EQ(framing_angle_text(2.5f), "turned 2.5° clockwise");
    EXPECT_EQ(framing_angle_text(-0.5f), "turned 0.5° anticlockwise");
    EXPECT_EQ(framing_angle_text(12.0f), "turned 12.0° clockwise");
    EXPECT_EQ(framing_angle_text(0.04f), "turned under 0.1° clockwise");
    EXPECT_EQ(framing_angle_text(-45.0f), "turned 45.0° anticlockwise");
    using prism::gui::framing_size_text;
    EXPECT_EQ(framing_size_text({5120, 3413}, {6000, 4000}),
              "Frame 5120 × 3413 of 6000 × 4000 (73% of the photo)");
    EXPECT_EQ(framing_size_text({3000, 2000}, {6000, 4000}),
              "Frame 3000 × 2000 of 6000 × 4000 (25% of the photo)");
    EXPECT_EQ(framing_size_text({3000, 2000}, {6000, 4000}, false),
              "Frame about 3000 × 2000 of about 6000 × 4000 (25% of the photo)");
    EXPECT_EQ(framing_size_text({40, 30}, {6000, 4000}),
              "Frame 40 × 30 of 6000 × 4000 (under 1% of the photo)");
    EXPECT_EQ(prism::gui::framing_live_line({5120, 3413}, {6000, 4000}, 1.2f),
              "Frame 5120 × 3413 of 6000 × 4000 (73% of the photo), turned 1.2° clockwise.");
    EXPECT_EQ(prism::gui::framing_live_line({6000, 4000}, {6000, 4000}, 0.0f),
              "Frame 6000 × 4000 of 6000 × 4000 (100% of the photo), upright.");
    EXPECT_EQ(prism::gui::framing_aspect_name(Aspect::four_five), "4:5");
    EXPECT_EQ(prism::gui::framing_aspect_name(Aspect::original), "Original");
    EXPECT_EQ(prism::gui::framing_aspect_menu_text(Aspect::four_five), "4:5 — an 8×10 inch sheet");
    EXPECT_EQ(prism::gui::framing_aspect_menu_text(Aspect::three_two), "3:2 — a 35 mm negative");
    EXPECT_EQ(prism::gui::framing_aspect_menu_text(Aspect::original),
              "Original — the photo's own shape");
    EXPECT_EQ(prism::gui::framing_fit_note(true),
              "The blades closed in so no corner runs past the photo's edge.");
    EXPECT_EQ(prism::gui::framing_fit_note(false), "");
    EXPECT_EQ(prism::gui::framing_hint(true),
              "Drag the blades to frame the print — drag inside to move them, ⌥-drag along the "
              "horizon to level it.");
}

// --- Keys, tools, guides and scopes
// ------------------------------------------------------------------

TEST(FramingKeys, KeysActOnlyWhenNothingElseHasThem) {
    using prism::gui::framing_key;
    using prism::gui::FramingKey;
    using prism::gui::FramingKeyAction;
    prism::gui::FramingKeyContext off;
    off.has_file = true;
    EXPECT_EQ(framing_key(FramingKey::c, off), FramingKeyAction::begin_framing);
    EXPECT_EQ(framing_key(FramingKey::return_key, off), FramingKeyAction::none);
    EXPECT_EQ(framing_key(FramingKey::escape, off), FramingKeyAction::none);
    EXPECT_EQ(framing_key(FramingKey::x, off), FramingKeyAction::none);
    prism::gui::FramingKeyContext no_photo;
    EXPECT_EQ(framing_key(FramingKey::c, no_photo), FramingKeyAction::none);
    prism::gui::FramingKeyContext on = off;
    on.framing = true;
    EXPECT_EQ(framing_key(FramingKey::x, on), FramingKeyAction::turn_aspect);
    EXPECT_EQ(framing_key(FramingKey::escape, on), FramingKeyAction::none);  // escape_action's
    on.gesture = true;
    EXPECT_EQ(framing_key(FramingKey::escape, on), FramingKeyAction::cancel_gesture);
    // Blocked behind a modal sheet (whose Return is its own), in a text field, during a stroke.
    for (const FramingKey key :
         {FramingKey::c, FramingKey::return_key, FramingKey::escape, FramingKey::x}) {
        for (const bool framing : {false, true}) {
            prism::gui::FramingKeyContext c = framing ? on : off;
            c.sheets.export_sheet = true;
            EXPECT_EQ(framing_key(key, c), FramingKeyAction::none);
            c = framing ? on : off;
            c.text_editing = true;
            EXPECT_EQ(framing_key(key, c), FramingKeyAction::none);
            c = framing ? on : off;
            c.stroking = true;
            EXPECT_EQ(framing_key(key, c), FramingKeyAction::none);
        }
    }
    // Entering framing turns the other tools off.
    EXPECT_EQ(prism::gui::tools_when_framing({true, false, true, false}),
              prism::gui::CanvasTools{});
    EXPECT_EQ(prism::gui::tools_when_framing({false, true, false, true}),
              prism::gui::CanvasTools{});
}

TEST(FramingGuides, ThirdsWhileDraggingAndAFinerGridWhileTurning) {
    using prism::gui::FramingGesture;
    using prism::gui::FramingGuides;
    EXPECT_EQ(prism::gui::framing_guides(FramingGesture::handle), FramingGuides::thirds);
    EXPECT_EQ(prism::gui::framing_guides(FramingGesture::move), FramingGuides::thirds);
    EXPECT_EQ(prism::gui::framing_guides(FramingGesture::turn), FramingGuides::grid);
    EXPECT_EQ(prism::gui::framing_guides(FramingGesture::horizon), FramingGuides::grid);
    EXPECT_EQ(prism::gui::framing_guides(FramingGesture::none), FramingGuides::none);
    const CanvasRect r{{100.0, 50.0}, {400.0, 260.0}};
    const std::vector<prism::gui::CanvasLine> thirds =
        prism::gui::framing_guide_lines(r, FramingGuides::thirds);
    ASSERT_EQ(thirds.size(), 4u);
    EXPECT_EQ(thirds[0], (prism::gui::CanvasLine{{200.0, 50.0}, {200.0, 260.0}}));
    EXPECT_EQ(thirds[3], (prism::gui::CanvasLine{{100.0, 190.0}, {400.0, 190.0}}));
    EXPECT_EQ(prism::gui::framing_guide_lines(r, FramingGuides::grid).size(),
              2u * (prism::gui::framing_grid_cells - 1));
    EXPECT_TRUE(prism::gui::framing_guide_lines(r, FramingGuides::none).empty());
}

TEST(FramingScopes, TheScopesReadTheWholePhotoWhileFraming) {
    EXPECT_EQ(prism::gui::scope_source(true), prism::gui::ScopeSource::whole_base);
    EXPECT_EQ(prism::gui::scope_source(false), prism::gui::ScopeSource::framed_base);
    EXPECT_EQ(prism::gui::scope_source_detail(prism::gui::scope_source(true)),
              "whole photo, while framing");
    EXPECT_EQ(prism::gui::scope_source_detail(prism::gui::scope_source(false)), "");
}
