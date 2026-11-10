#include "prism/gui/brush.hpp"

#include <memory>
#include <utility>

namespace prism::gui {

namespace {

BrushAction act(BrushActionKind kind, BrushTone tone = BrushTone::dodge, FieldPoint point = {}) {
    return {kind, tone, point};
}

}  // namespace

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

std::vector<BrushAction> BrushGesture::move(ViewPoint at, BrushModifiers /*keys*/) {
    std::vector<BrushAction> out;
    if (state_ == State::panning) {
        out.push_back(act(BrushActionKind::passthrough));
    } else if (state_ == State::stroking) {
        add_if_far(at, out);
    }
    return out;
}

std::vector<BrushAction> BrushGesture::release(ViewPoint at, BrushModifiers /*keys*/) {
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

float brush_weight(BrushTone tone, float strength) noexcept {
    return tone == BrushTone::dodge ? strength : -strength;
}

MaybeError StrokePainter::set_strokes(Session& session, Strokes strokes) {
    EditSettings s = session.settings();
    s.strokes = std::move(strokes);
    return session.set_settings(s);
}

MaybeError StrokePainter::apply(const BrushAction& action, const BrushStyle& style,
                                BrushCanvas canvas, Session& session) {
    switch (action.kind) {
        case BrushActionKind::begin_stroke:
            if (before_) {
                return std::nullopt;
            }
            session.begin_gesture();
            before_ = session.settings().strokes;
            stroke_ = {};
            stroke_.radius = style.radius;
            stroke_.softness = style.softness;
            stroke_.weight = brush_weight(action.tone, style.strength);
            return std::nullopt;
        case BrushActionKind::add_point: {
            if (!before_) {
                return std::nullopt;
            }
            stroke_.points.push_back(action.point);
            auto field = before_->field() ? std::make_shared<StrokeField>(*before_->field())
                                          : std::make_shared<StrokeField>();
            if (field->strokes.empty()) {
                field->aspect = aspect_of(canvas.width, canvas.height);
            }
            field->strokes.push_back(std::make_shared<const FieldStroke>(stroke_));
            return set_strokes(session, Strokes(std::move(field)));
        }
        case BrushActionKind::commit_stroke:
            if (before_) {
                session.end_gesture();
                before_.reset();
            }
            return std::nullopt;
        case BrushActionKind::cancel_stroke: {
            if (!before_) {
                return std::nullopt;
            }
            Strokes before = std::move(*before_);
            before_.reset();
            MaybeError error = set_strokes(session, std::move(before));
            session.end_gesture();
            return error;
        }
        case BrushActionKind::leave_brush:
        case BrushActionKind::passthrough: return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace prism::gui
