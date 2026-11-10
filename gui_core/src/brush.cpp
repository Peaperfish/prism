#include "prism/gui/brush.hpp"

namespace prism::gui {

namespace {

BrushAction act(BrushActionKind kind, BrushTone tone = BrushTone::dodge, FieldPoint point = {}) {
    return {kind, tone, point};
}

}  // namespace

BrushTone other_tone(BrushTone tone) noexcept {
    return tone == BrushTone::dodge ? BrushTone::burn : BrushTone::dodge;
}

FieldPoint brush_point(ViewPoint image, BrushCanvas canvas) noexcept {
    if (canvas.width <= 0 || canvas.height <= 0) {
        return {};
    }
    // Image coordinates run along the pixels' edges, so pixel x's centre is at x + 0.5, and
    // stroke_field.hpp's u = (x + 0.5) / W is the image coordinate over the width.
    return {image.x / static_cast<float>(canvas.width),
            image.y / static_cast<float>(canvas.height)};
}

std::vector<BrushAction> BrushGesture::press(ViewPoint at, BrushModifiers keys, BrushTone base,
                                             float spacing, BrushCanvas canvas) {
    if (state_ == State::panning) {
        return {act(BrushActionKind::passthrough)};
    }
    if (state_ != State::idle) {
        return {};  // a second press while the first is held
    }
    if (keys.space) {
        state_ = State::panning;
        return {act(BrushActionKind::passthrough)};
    }
    state_ = State::stroking;
    tone_ = keys.alt ? other_tone(base) : base;
    spacing_ = spacing;
    canvas_ = canvas;
    last_ = brush_point(at, canvas);
    return {act(BrushActionKind::begin_stroke, tone_),
            act(BrushActionKind::add_point, tone_, last_)};
}

void BrushGesture::add_if_far(ViewPoint at, std::vector<BrushAction>& out) {
    const FieldPoint p = brush_point(at, canvas_);
    if (far_enough(last_, p, aspect_of(canvas_.width, canvas_.height), spacing_)) {
        last_ = p;
        out.push_back(act(BrushActionKind::add_point, tone_, p));
    }
}

std::vector<BrushAction> BrushGesture::move(ViewPoint at) {
    std::vector<BrushAction> out;
    if (state_ == State::panning) {
        out.push_back(act(BrushActionKind::passthrough));
    } else if (state_ == State::stroking) {
        add_if_far(at, out);
    }
    return out;
}

std::vector<BrushAction> BrushGesture::release(ViewPoint at) {
    std::vector<BrushAction> out;
    switch (state_) {
        case State::idle: break;
        case State::panning: out.push_back(act(BrushActionKind::passthrough)); break;
        case State::stroking:
            add_if_far(at, out);
            out.push_back(act(BrushActionKind::commit_stroke, tone_));
            break;
        case State::cancelled: break;
    }
    state_ = State::idle;
    return out;
}

std::vector<BrushAction> BrushGesture::escape() {
    if (state_ == State::stroking) {
        state_ = State::cancelled;
        return {act(BrushActionKind::cancel_stroke, tone_)};
    }
    if (state_ == State::cancelled) {
        return {};  // the stroke is already gone; the button is still down
    }
    state_ = State::idle;
    return {act(BrushActionKind::leave_brush)};
}

std::vector<BrushAction> BrushGesture::finish() {
    const bool was_stroking = state_ == State::stroking;
    state_ = State::idle;
    if (was_stroking) {
        return {act(BrushActionKind::commit_stroke, tone_)};
    }
    return {};
}

}  // namespace prism::gui
