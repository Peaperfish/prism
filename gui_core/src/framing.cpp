#include "prism/gui/framing.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

#include "prism/gui/export_size.hpp"
#include "prism/gui/settings.hpp"
#include "prism/ranges.hpp"

namespace prism::gui {

namespace {

struct Turn {
    double c = 1.0;
    double s = 0.0;
};

Turn turn_of(float angle) noexcept {
    const double a = static_cast<double>(angle) * (std::numbers::pi / 180.0);
    return {std::cos(a), std::sin(a)};
}

// The frame's rectangle in photo pixels along the straightened axes, from the photo's centre:
// centre (cx, cy), half sizes (ax, ay). The same quantities fit_frame works in.
struct Box {
    double cx = 0.0;
    double cy = 0.0;
    double ax = 0.0;
    double ay = 0.0;
};

Box box_of(const Frame& f, double w, double h) noexcept {
    return {((static_cast<double>(f.left) + f.right) / 2.0 - 0.5) * w,
            ((static_cast<double>(f.top) + f.bottom) / 2.0 - 0.5) * h,
            (static_cast<double>(f.right) - f.left) / 2.0 * w,
            (static_cast<double>(f.bottom) - f.top) / 2.0 * h};
}

// `f` with its rectangle set to `b`, each edge clamped to 0..1.
Frame with_box(Frame f, const Box& b, double w, double h) noexcept {
    const auto fraction = [](double pixels_from_centre, double n) {
        return static_cast<float>(std::clamp(pixels_from_centre / n + 0.5, 0.0, 1.0));
    };
    f.left = fraction(b.cx - b.ax, w);
    f.right = fraction(b.cx + b.ax, w);
    f.top = fraction(b.cy - b.ay, h);
    f.bottom = fraction(b.cy + b.ay, h);
    // A side that rounds to nothing as a float is kept one float step wide, as fit_frame does.
    const auto keep_apart = [](float& low, float& high) {
        if (low < high) {
            return;
        }
        if (high < 1.0f) {
            high = std::nextafter(low, 2.0f);
        } else {
            low = std::nextafter(1.0f, 0.0f);
        }
    };
    keep_apart(f.left, f.right);
    keep_apart(f.top, f.bottom);
    return f;
}

// `b` with its half sizes cut, about its centre, so the rectangle stays inside the straightened
// photo [-w/2, w/2] x [-h/2, h/2] (where a Frame's edges must lie).
Box inside_bounds(Box b, double w, double h) noexcept {
    b.ax = std::min(b.ax, std::max(0.0, std::min(w / 2.0 - b.cx, w / 2.0 + b.cx)));
    b.ay = std::min(b.ay, std::max(0.0, std::min(h / 2.0 - b.cy, h / 2.0 + b.cy)));
    return b;
}

// Whether every corner of `b` lies on the photo turned by the angle `t`, and the rectangle inside
// the straightened bounds, to within `tol` pixels.
bool on_photo(const Box& b, const Turn& t, double w, double h, double tol) noexcept {
    if (b.cx - b.ax < -w / 2.0 - tol || b.cx + b.ax > w / 2.0 + tol ||
        b.cy - b.ay < -h / 2.0 - tol || b.cy + b.ay > h / 2.0 + tol) {
        return false;
    }
    for (const double sx : {-1.0, 1.0}) {
        for (const double sy : {-1.0, 1.0}) {
            const double x = b.cx + sx * b.ax;
            const double y = b.cy + sy * b.ay;
            const double u = t.c * x + t.s * y;
            const double v = -t.s * x + t.c * y;
            if (std::abs(u) > w / 2.0 + tol || std::abs(v) > h / 2.0 + tol) {
                return false;
            }
        }
    }
    return true;
}

// A frame's shape lock as a width / height in pixels, or nothing for Free.
std::optional<double> ratio_of(const Frame& f, int w, int h) {
    return aspect_ratio(f.aspect, f.portrait, w, h);
}

// fit_frame of `asked`, and whether it moved an edge by more than a hundredth of a pixel (less is
// the float rounding of a frame that already fitted, not a change anyone could see).
FrameEdit fitted(const Frame& asked, int w, int h) {
    const Frame out = fit_frame(asked, w, h);
    const auto moved = [](float a, float b, int n) {
        return std::abs(static_cast<double>(a) - b) * n > 0.01;
    };
    return {out, moved(out.left, asked.left, w) || moved(out.right, asked.right, w) ||
                     moved(out.top, asked.top, h) || moved(out.bottom, asked.bottom, h)};
}

// The width / height of a Box clamped to at least `least` each, keeping `ratio` if there is one.
void at_least(double& width, double& height, double least, const std::optional<double>& ratio) {
    if (ratio) {
        if (width < least) {
            width = least;
            height = width / *ratio;
        }
        if (height < least) {
            height = least;
            width = height * *ratio;
        }
    } else {
        width = std::max(width, least);
        height = std::max(height, least);
    }
}

// The frame of `b` is at least `least` pixels on each side (to float rounding of the fractions).
bool big_enough(const Frame& f, double w, double h, double least) noexcept {
    const double slack = 1e-4;  // fractions are floats: a side can come back a hair short
    return (static_cast<double>(f.right) - f.left) * w >= least - slack &&
           (static_cast<double>(f.bottom) - f.top) * h >= least - slack;
}

std::string times(PixelSize size) {
    return std::to_string(size.width) + " × " + std::to_string(size.height);
}

}  // namespace

// --- The view
// ---------------------------------------------------------------------------------------

FramingView framing_view(float angle, int photo_width, int photo_height, double canvas_width,
                         double canvas_height, double margin) {
    const Turn t = turn_of(angle);
    const double w = std::max(photo_width, 1);
    const double h = std::max(photo_height, 1);
    // The turned photo's bounding box along the canvas's axes.
    const double box_w = std::abs(t.c) * w + std::abs(t.s) * h;
    const double box_h = std::abs(t.s) * w + std::abs(t.c) * h;
    const double room_w = std::max(canvas_width - 2.0 * margin, 1.0);
    const double room_h = std::max(canvas_height - 2.0 * margin, 1.0);
    FramingView view;
    view.angle = angle;
    view.photo_width = photo_width;
    view.photo_height = photo_height;
    view.scale = std::min(room_w / box_w, room_h / box_h);
    view.centre = {canvas_width / 2.0, canvas_height / 2.0};
    return view;
}

CanvasPoint canvas_from_photo(const FramingView& view, CanvasPoint photo) noexcept {
    const Turn t = turn_of(view.angle);
    const double dx = photo.x - view.photo_width / 2.0;
    const double dy = photo.y - view.photo_height / 2.0;
    // Straightened position: the inverse of prism/frame.hpp's photo = C + R (s - C).
    const double sx = t.c * dx - t.s * dy;
    const double sy = t.s * dx + t.c * dy;
    return {view.centre.x + view.scale * sx, view.centre.y + view.scale * sy};
}

CanvasPoint photo_from_canvas(const FramingView& view, CanvasPoint canvas) noexcept {
    const Turn t = turn_of(view.angle);
    const double sx = (canvas.x - view.centre.x) / view.scale;
    const double sy = (canvas.y - view.centre.y) / view.scale;
    return {view.photo_width / 2.0 + t.c * sx + t.s * sy,
            view.photo_height / 2.0 - t.s * sx + t.c * sy};
}

std::array<CanvasPoint, 4> photo_corners(const FramingView& view) noexcept {
    const double w = view.photo_width;
    const double h = view.photo_height;
    return {canvas_from_photo(view, {0.0, 0.0}), canvas_from_photo(view, {w, 0.0}),
            canvas_from_photo(view, {w, h}), canvas_from_photo(view, {0.0, h})};
}

CanvasRect frame_rect(const FramingView& view, const Frame& frame) noexcept {
    const double w = view.photo_width;
    const double h = view.photo_height;
    const auto x = [&](float f) {
        return view.centre.x + view.scale * (static_cast<double>(f) - 0.5) * w;
    };
    const auto y = [&](float f) {
        return view.centre.y + view.scale * (static_cast<double>(f) - 0.5) * h;
    };
    return {{x(frame.left), y(frame.top)}, {x(frame.right), y(frame.bottom)}};
}

// --- Handles and drags
// ------------------------------------------------------------------------------

FrameHandle frame_handle_at(const FramingView& view, const Frame& frame, CanvasPoint at,
                            double reach) noexcept {
    const CanvasRect r = frame_rect(view, frame);
    const auto distance = [&](CanvasPoint p) { return std::hypot(at.x - p.x, at.y - p.y); };
    // Corners first, the nearest one within reach.
    const std::array<std::pair<FrameHandle, CanvasPoint>, 4> corners = {
        {{FrameHandle::top_left, r.min},
         {FrameHandle::top_right, {r.max.x, r.min.y}},
         {FrameHandle::bottom_right, r.max},
         {FrameHandle::bottom_left, {r.min.x, r.max.y}}}};
    FrameHandle best = FrameHandle::none;
    double best_distance = reach;
    for (const auto& [handle, p] : corners) {
        if (const double d = distance(p); d <= best_distance) {
            best = handle;
            best_distance = d;
        }
    }
    if (best != FrameHandle::none) {
        return best;
    }
    // Then the edges, between their corners.
    const bool across = at.x >= r.min.x && at.x <= r.max.x;
    const bool down = at.y >= r.min.y && at.y <= r.max.y;
    const std::array<std::pair<FrameHandle, double>, 4> edges = {
        {{FrameHandle::left, down ? std::abs(at.x - r.min.x) : reach + 1.0},
         {FrameHandle::top, across ? std::abs(at.y - r.min.y) : reach + 1.0},
         {FrameHandle::right, down ? std::abs(at.x - r.max.x) : reach + 1.0},
         {FrameHandle::bottom, across ? std::abs(at.y - r.max.y) : reach + 1.0}}};
    for (const auto& [handle, d] : edges) {
        if (d <= best_distance) {
            best = handle;
            best_distance = d;
        }
    }
    if (best != FrameHandle::none) {
        return best;
    }
    return across && down ? FrameHandle::inside : FrameHandle::none;
}

double framing_min_side(int photo_width, int photo_height) noexcept {
    return std::max(1.0, 0.01 * static_cast<double>(std::min(photo_width, photo_height)));
}

std::optional<FrameEdit> drag_frame(const Frame& start, FrameHandle handle, CanvasPoint from,
                                    CanvasPoint to, const FramingView& view, int photo_width,
                                    int photo_height) {
    if (!validate_frame(start).empty() || photo_width < 1 || photo_height < 1) {
        (void)fit_frame(start, photo_width, photo_height);  // throws with its messages
    }
    if (handle == FrameHandle::none || !(view.scale > 0.0)) {
        return std::nullopt;
    }
    const double w = photo_width;
    const double h = photo_height;
    const double least = framing_min_side(photo_width, photo_height);
    const double dx = (to.x - from.x) / view.scale;
    const double dy = (to.y - from.y) / view.scale;
    const Box b = box_of(start, w, h);
    double left = b.cx - b.ax;
    double right = b.cx + b.ax;
    double top = b.cy - b.ay;
    double bottom = b.cy + b.ay;
    Frame asked = start;

    const auto corner = [&](bool moves_left, bool moves_top) {
        // The opposite corner holds; the pointer's corner sets the size, within the photo.
        const double ox = moves_left ? right : left;
        const double oy = moves_top ? bottom : top;
        const double px = std::clamp((moves_left ? left : right) + dx, -w / 2.0, w / 2.0);
        const double py = std::clamp((moves_top ? top : bottom) + dy, -h / 2.0, h / 2.0);
        double width = std::max(moves_left ? ox - px : px - ox, 0.0);
        double height = std::max(moves_top ? oy - py : py - oy, 0.0);
        const std::optional<double> ratio = ratio_of(start, photo_width, photo_height);
        if (ratio) {
            // The largest rectangle of the shape inside the one the pointer spans.
            if (width > height * *ratio) {
                width = height * *ratio;
            } else {
                height = width / *ratio;
            }
        }
        at_least(width, height, least, ratio);
        // As far as the photo's straightened bounds allow from the held corner, keeping the shape.
        const double room_w = moves_left ? ox + w / 2.0 : w / 2.0 - ox;
        const double room_h = moves_top ? oy + h / 2.0 : h / 2.0 - oy;
        const double k =
            std::min({1.0, room_w / std::max(width, 1e-12), room_h / std::max(height, 1e-12)});
        width *= k;
        height *= k;
        left = moves_left ? ox - width : ox;
        right = moves_left ? ox : ox + width;
        top = moves_top ? oy - height : oy;
        bottom = moves_top ? oy : oy + height;
    };

    switch (handle) {
        case FrameHandle::none: return std::nullopt;
        case FrameHandle::left:
            left = std::clamp(left + dx, -w / 2.0, std::max(right - least, -w / 2.0));
            asked.aspect = Aspect::free;
            break;
        case FrameHandle::right:
            right = std::clamp(right + dx, std::min(left + least, w / 2.0), w / 2.0);
            asked.aspect = Aspect::free;
            break;
        case FrameHandle::top:
            top = std::clamp(top + dy, -h / 2.0, std::max(bottom - least, -h / 2.0));
            asked.aspect = Aspect::free;
            break;
        case FrameHandle::bottom:
            bottom = std::clamp(bottom + dy, std::min(top + least, h / 2.0), h / 2.0);
            asked.aspect = Aspect::free;
            break;
        case FrameHandle::top_left: corner(true, true); break;
        case FrameHandle::top_right: corner(false, true); break;
        case FrameHandle::bottom_right: corner(false, false); break;
        case FrameHandle::bottom_left: corner(true, false); break;
        case FrameHandle::inside: {
            // Along the drag as far as every corner stays on the photo, then the rest of each axis
            // on its own, so the frame slides along an edge it meets.
            const Turn t = turn_of(start.angle);
            const double tol = 1e-6 * std::max(w, h);
            const auto fits = [&](double mx, double my) {
                Box moved = b;
                moved.cx += mx;
                moved.cy += my;
                return on_photo(moved, t, w, h, tol);
            };
            // The largest share s in [0, 1] of a step with fits(s); the set of such s is an
            // interval from 0, as the positions a frame of this size fits at are convex.
            const auto furthest = [&](double bx, double by, double sx, double sy) {
                if (fits(bx + sx, by + sy)) {
                    return 1.0;
                }
                if (!fits(bx, by)) {
                    return 0.0;
                }
                double lo = 0.0;
                double hi = 1.0;
                for (int i = 0; i < 60; ++i) {
                    const double mid = (lo + hi) / 2.0;
                    (fits(bx + mid * sx, by + mid * sy) ? lo : hi) = mid;
                }
                return lo;
            };
            const double s = furthest(0.0, 0.0, dx, dy);
            double mx = s * dx;
            double my = s * dy;
            mx += furthest(mx, my, dx - mx, 0.0) * (dx - mx);
            my += furthest(mx, my, 0.0, dy - my) * (dy - my);
            left += mx;
            right += mx;
            top += my;
            bottom += my;
            break;
        }
    }
    const Box out{(left + right) / 2.0, (top + bottom) / 2.0, (right - left) / 2.0,
                  (bottom - top) / 2.0};
    asked = with_box(asked, out, w, h);
    const FrameEdit edit = fitted(asked, photo_width, photo_height);
    if (!big_enough(edit.frame, w, h, least)) {
        return std::nullopt;
    }
    return edit;
}

FrameEdit turn_frame(const Frame& start, float angle, int photo_width, int photo_height) {
    if (!validate_frame(start).empty() || photo_width < 1 || photo_height < 1) {
        (void)fit_frame(start, photo_width, photo_height);  // throws with its messages
    }
    const float limit = static_cast<float>(frame_angle_range.max);
    const float wanted = std::isfinite(angle) ? std::clamp(angle, -limit, limit) : start.angle;
    if (wanted == start.angle) {
        return {start, false};
    }
    const double w = photo_width;
    const double h = photo_height;
    const Box b = box_of(start, w, h);
    // The photo point under the frame's centre (from the photo's centre, in pixels), as fit_frame
    // works it out, then the straightened position over it at the new angle.
    const Turn was = turn_of(start.angle);
    const double pu = was.c * b.cx + was.s * b.cy;
    const double pv = -was.s * b.cx + was.c * b.cy;
    const Turn now = turn_of(wanted);
    Box turned{now.c * pu - now.s * pv, now.s * pu + now.c * pv, b.ax, b.ay};
    // A centre past the straightened bounds (a point near a corner, turned far) moves straight
    // towards the photo's centre until it is half a pixel inside them, as fit_frame moves one that
    // is off the photo, then the rectangle shrinks about it to fit them.
    const double reach_x = std::max(w / 2.0 - 0.5, 0.0);
    const double reach_y = std::max(h / 2.0 - 0.5, 0.0);
    double m = 1.0;
    if (std::abs(turned.cx) > reach_x) {
        m = std::min(m, reach_x / std::abs(turned.cx));
    }
    if (std::abs(turned.cy) > reach_y) {
        m = std::min(m, reach_y / std::abs(turned.cy));
    }
    turned.cx *= m;
    turned.cy *= m;
    turned = inside_bounds(turned, w, h);
    Frame asked = with_box(start, turned, w, h);
    asked.angle = wanted;
    if (ratio_of(asked, photo_width, photo_height)) {
        asked = apply_aspect(asked, photo_width, photo_height);  // the bounds may have cut one side
    }
    return fitted(asked, photo_width, photo_height);
}

std::optional<float> straighten_angle(const FramingView& view, CanvasPoint a,
                                      CanvasPoint b) noexcept {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (!(std::hypot(dx, dy) >= 4.0)) {
        return std::nullopt;
    }
    // The line's angle on screen, clockwise from level (y is down), as an undirected line.
    double on_screen = std::atan2(dy, dx) * (180.0 / std::numbers::pi);
    if (on_screen > 90.0) {
        on_screen -= 180.0;
    } else if (on_screen <= -90.0) {
        on_screen += 180.0;
    }
    // Within 45 degrees of level it is made level, otherwise upright.
    const double off = std::abs(on_screen) <= 45.0 ? on_screen
                       : on_screen > 0.0           ? on_screen - 90.0
                                                   : on_screen + 90.0;
    // The view turns the photo by its angle; turning it by `off` less brings the line to level.
    const double limit = frame_angle_range.max;
    return static_cast<float>(std::clamp(static_cast<double>(view.angle) - off, -limit, limit));
}

FrameEdit choose_aspect(const Frame& f, Aspect aspect, int photo_width, int photo_height) {
    if (aspect == f.aspect) {
        (void)fit_frame(f, photo_width, photo_height);  // throws for an invalid frame
        return {f, false};                              // the lock it has already: nothing changes
    }
    Frame asked = f;
    asked.aspect = aspect;
    if (aspect == Aspect::free) {
        (void)fit_frame(asked, photo_width, photo_height);  // throws for an invalid frame
        return {asked, false};
    }
    asked = apply_aspect(asked, photo_width, photo_height);
    return fitted(asked, photo_width, photo_height);
}

FrameEdit turn_aspect(const Frame& f, int photo_width, int photo_height) {
    if (!validate_frame(f).empty() || photo_width < 1 || photo_height < 1) {
        (void)fit_frame(f, photo_width, photo_height);  // throws with its messages
    }
    const double w = photo_width;
    const double h = photo_height;
    Box b = box_of(f, w, h);
    std::swap(b.ax, b.ay);
    b = inside_bounds(b, w, h);
    Frame asked = with_box(f, b, w, h);
    asked.portrait = !f.portrait;
    if (ratio_of(asked, photo_width, photo_height)) {
        asked = apply_aspect(asked, photo_width, photo_height);
    }
    return fitted(asked, photo_width, photo_height);
}

// --- The framing state
// ------------------------------------------------------------------------------

namespace {

Frame whole_photo(const FrameLock& lock) noexcept {
    Frame f;
    f.aspect = lock.aspect;
    f.portrait = lock.portrait;
    return f;
}

}  // namespace

void FramingState::begin(const std::optional<Frame>& current, const FrameLock& lock,
                         int photo_width, int photo_height) {
    if (photo_width < 1 || photo_height < 1) {
        throw std::invalid_argument("FramingState::begin: the photo has no pixels");
    }
    on_ = true;
    photo_width_ = photo_width;
    photo_height_ = photo_height;
    draft_ = current ? *current : whole_photo(lock);
    start_ = draft_;
    gesture_ = FramingGesture::none;
    handle_ = FrameHandle::none;
    fitted_ = false;
}

bool FramingState::begin(const Session& session) {
    const std::shared_ptr<const Image> whole = session.whole_base();
    if (!whole || whole->width < 1 || whole->height < 1) {
        return false;
    }
    begin(session.settings().frame, session.frame_lock(), whole->width, whole->height);
    return true;
}

MaybeError FramingState::leave(Session& session) {
    MaybeError error;
    if (gesture_ != FramingGesture::none) {
        error = commit(session);
    }
    on_ = false;
    gesture_ = FramingGesture::none;
    handle_ = FrameHandle::none;
    return error;
}

std::optional<CanvasLine> FramingState::horizon() const noexcept {
    if (gesture_ != FramingGesture::horizon) {
        return std::nullopt;
    }
    return CanvasLine{from_, to_};
}

void FramingState::start(FramingGesture gesture) {
    gesture_ = gesture;
    start_ = draft_;
    fitted_ = false;
}

void FramingState::apply(const FrameEdit& edit) {
    draft_ = edit.frame;
    fitted_ = edit.fitted;
}

bool FramingState::press(CanvasPoint at, bool alt, const FramingView& view, double reach) {
    if (!on_ || gesture_ != FramingGesture::none) {
        return false;
    }
    FrameHandle handle = FrameHandle::none;
    if (!alt) {
        handle = frame_handle_at(view, draft_, at, reach);
        if (handle == FrameHandle::none) {
            return false;
        }
    }
    start(alt                             ? FramingGesture::horizon
          : handle == FrameHandle::inside ? FramingGesture::move
                                          : FramingGesture::handle);
    handle_ = handle;
    view_ = view;
    from_ = at;
    to_ = at;
    return true;
}

void FramingState::move(CanvasPoint at) {
    if (gesture_ != FramingGesture::handle && gesture_ != FramingGesture::move &&
        gesture_ != FramingGesture::horizon) {
        return;
    }
    to_ = at;
    if (gesture_ == FramingGesture::horizon) {
        return;  // the line turns the picture when it is let go
    }
    if (const std::optional<FrameEdit> edit =
            drag_frame(start_, handle_, from_, at, view_, photo_width_, photo_height_)) {
        apply(*edit);
    }
}

MaybeError FramingState::release(CanvasPoint at, Session& session) {
    if (gesture_ == FramingGesture::none || gesture_ == FramingGesture::turn) {
        return std::nullopt;
    }
    move(at);
    if (gesture_ == FramingGesture::horizon) {
        if (const std::optional<float> angle = straighten_angle(view_, from_, to_)) {
            apply(turn_frame(start_, *angle, photo_width_, photo_height_));
        }
    }
    return commit(session);
}

void FramingState::begin_turn() {
    if (on_ && gesture_ == FramingGesture::none) {
        start(FramingGesture::turn);
    }
}

void FramingState::turn_to(float angle) {
    if (gesture_ == FramingGesture::turn) {
        apply(turn_frame(start_, angle, photo_width_, photo_height_));
    }
}

MaybeError FramingState::commit(Session& session) {
    gesture_ = FramingGesture::none;
    handle_ = FrameHandle::none;
    session.set_frame_lock({draft_.aspect, draft_.portrait});
    EditSettings s = session.settings();
    s.frame = draft_;
    return session.set_settings(s);
}

bool FramingState::cancel() noexcept {
    if (gesture_ == FramingGesture::none) {
        return false;
    }
    draft_ = start_;
    gesture_ = FramingGesture::none;
    handle_ = FrameHandle::none;
    fitted_ = false;
    return true;
}

MaybeError FramingState::set_angle(float angle, Session& session) {
    if (!on_ || gesture_ != FramingGesture::none) {
        return std::nullopt;
    }
    start(FramingGesture::turn);
    apply(turn_frame(start_, angle, photo_width_, photo_height_));
    return commit(session);
}

MaybeError FramingState::set_aspect(Aspect aspect, Session& session) {
    if (!on_ || gesture_ != FramingGesture::none) {
        return std::nullopt;
    }
    start(FramingGesture::handle);
    apply(choose_aspect(draft_, aspect, photo_width_, photo_height_));
    return commit(session);
}

MaybeError FramingState::flip_aspect(Session& session) {
    if (!on_ || gesture_ != FramingGesture::none) {
        return std::nullopt;
    }
    start(FramingGesture::handle);
    apply(turn_aspect(draft_, photo_width_, photo_height_));
    return commit(session);
}

MaybeError FramingState::reset(Session& session) {
    if (!on_ || gesture_ != FramingGesture::none) {
        return std::nullopt;
    }
    start(FramingGesture::handle);
    const Frame upright = whole_photo({draft_.aspect, draft_.portrait});
    apply(upright.aspect == Aspect::original && !upright.portrait
              ? FrameEdit{upright, false}
              : choose_aspect(upright, upright.aspect, photo_width_, photo_height_));
    return commit(session);
}

void FramingState::follow(const std::optional<Frame>& current) {
    if (on_ && gesture_ == FramingGesture::none) {
        draft_ = current ? *current : whole_photo({draft_.aspect, draft_.portrait});
        fitted_ = false;
    }
}

// --- Guides
// -----------------------------------------------------------------------------------------

FramingGuides framing_guides(FramingGesture gesture) noexcept {
    switch (gesture) {
        case FramingGesture::handle:
        case FramingGesture::move: return FramingGuides::thirds;
        case FramingGesture::horizon:
        case FramingGesture::turn: return FramingGuides::grid;
        case FramingGesture::none: return FramingGuides::none;
    }
    return FramingGuides::none;
}

std::vector<CanvasLine> framing_guide_lines(const CanvasRect& frame, FramingGuides guides) {
    const int cells = guides == FramingGuides::thirds ? 3
                      : guides == FramingGuides::grid ? framing_grid_cells
                                                      : 0;
    std::vector<CanvasLine> lines;
    if (cells == 0) {
        return lines;
    }
    const double w = frame.max.x - frame.min.x;
    const double h = frame.max.y - frame.min.y;
    for (int i = 1; i < cells; ++i) {
        const double x = frame.min.x + w * i / cells;
        lines.push_back({{x, frame.min.y}, {x, frame.max.y}});
    }
    for (int i = 1; i < cells; ++i) {
        const double y = frame.min.y + h * i / cells;
        lines.push_back({{frame.min.x, y}, {frame.max.x, y}});
    }
    return lines;
}

// --- Keys and the other tools
// -----------------------------------------------------------------------

FramingKeyAction framing_key(FramingKey key, const FramingKeyContext& c) noexcept {
    if (shortcuts_blocked(c.sheets) || c.text_editing || c.stroking) {
        return FramingKeyAction::none;
    }
    switch (key) {
        case FramingKey::c:
            if (c.framing) {
                return FramingKeyAction::leave_framing;
            }
            return c.has_file ? FramingKeyAction::begin_framing : FramingKeyAction::none;
        case FramingKey::return_key:
            return c.framing ? FramingKeyAction::leave_framing : FramingKeyAction::none;
        case FramingKey::escape:
            return c.framing && c.gesture ? FramingKeyAction::cancel_gesture
                                          : FramingKeyAction::none;
        case FramingKey::x:
            return c.framing && !c.gesture ? FramingKeyAction::turn_aspect : FramingKeyAction::none;
    }
    return FramingKeyAction::none;
}

CanvasTools tools_when_framing(CanvasTools /*tools*/) noexcept {
    return CanvasTools{};
}

// --- Scopes
// -----------------------------------------------------------------------------------------

ScopeSource scope_source(bool framing) noexcept {
    return framing ? ScopeSource::whole_base : ScopeSource::framed_base;
}

std::string_view scope_source_detail(ScopeSource source) noexcept {
    return source == ScopeSource::whole_base ? "whole photo, while framing" : "";
}

// --- Explanations
// -----------------------------------------------------------------------------------

std::string framing_angle_text(float angle) {
    if (angle == 0.0f || !std::isfinite(angle)) {
        return "Upright";
    }
    const char* const way = angle > 0.0f ? " clockwise" : " anticlockwise";
    const long tenths = std::lround(std::abs(static_cast<double>(angle)) * 10.0);
    if (tenths == 0) {
        return std::string("turned under 0.1°") + way;
    }
    return "turned " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "°" + way;
}

std::string framing_size_text(PixelSize framed, PixelSize photo, bool exact) {
    const std::string about(size_estimate_prefix(exact));
    const double area = static_cast<double>(photo.width) * photo.height;
    const double share =
        area > 0.0 ? 100.0 * static_cast<double>(framed.width) * framed.height / area : 0.0;
    const long percent = std::lround(share);
    const std::string of_photo =
        percent < 1 ? "under 1%" : std::to_string(std::min(percent, 100L)) + "%";
    return "Frame " + about + times(framed) + " of " + about + times(photo) + " (" + of_photo +
           " of the photo)";
}

std::string framing_live_line(PixelSize framed, PixelSize photo, float angle, bool exact) {
    std::string angle_words = framing_angle_text(angle);
    if (angle_words == "Upright") {
        angle_words = "upright";
    }
    return framing_size_text(framed, photo, exact) + ", " + angle_words + ".";
}

std::string_view framing_aspect_name(Aspect aspect) noexcept {
    switch (aspect) {
        case Aspect::original: return "Original";
        case Aspect::free: return "Free";
        case Aspect::square: return "1:1";
        case Aspect::four_five: return "4:5";
        case Aspect::five_seven: return "5:7";
        case Aspect::three_two: return "3:2";
        case Aspect::sixteen_nine: return "16:9";
    }
    return "";
}

std::string_view framing_aspect_menu_text(Aspect aspect) noexcept {
    switch (aspect) {
        case Aspect::original: return "Original — the photo's own shape";
        case Aspect::free: return "Free — any shape";
        case Aspect::square: return "1:1 — a square";
        case Aspect::four_five: return "4:5 — an 8×10 inch sheet";
        case Aspect::five_seven: return "5:7 — a 5×7 inch sheet";
        case Aspect::three_two: return "3:2 — a 35 mm negative";
        case Aspect::sixteen_nine: return "16:9 — a widescreen picture";
    }
    return "";
}

std::string_view framing_fit_note(bool fitted) noexcept {
    return fitted ? "The blades closed in so no corner runs past the photo's edge." : "";
}

std::string_view framing_hint(bool mac) noexcept {
    return mac ? "Drag the blades to frame the print — drag inside to move them, ⌥-drag along the "
                 "horizon to level it."
               : "Drag the blades to frame the print — drag inside to move them, Alt-drag along "
                 "the horizon to level it.";
}

}  // namespace prism::gui
