#include "prism/gui/brush.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace prism::gui {

namespace {

BrushAction act(BrushActionKind kind, BrushTone tone = BrushTone::dodge, FieldPoint point = {}) {
    return {kind, tone, point};
}

}  // namespace

BrushCanvas brush_canvas(const Session& session) {
    const std::shared_ptr<const Image> base = session.preview_base();
    if (!base) {
        return {};
    }
    return {base->width, base->height, session.preview_framing()};
}

PixelSize brush_photo_size(const BrushCanvas& canvas) noexcept {
    if (canvas.framing) {
        return {canvas.framing->photo_width, canvas.framing->photo_height};
    }
    return {canvas.width, canvas.height};
}

int brush_long_side(const BrushCanvas& canvas) noexcept {
    if (canvas.width <= 0 || canvas.height <= 0) {
        return 0;
    }
    const PixelSize photo = brush_photo_size(canvas);
    const int long_side = std::max(photo.width, photo.height);
    if (!canvas.framing) {
        return long_side;
    }
    const double scale = static_cast<double>(canvas.framing->scale);
    if (!(scale > 0.0) || !std::isfinite(scale)) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(long_side) / scale)));
}

FieldPoint brush_point(ViewPoint image, const BrushCanvas& canvas) {
    if (canvas.width <= 0 || canvas.height <= 0) {
        return {};
    }
    if (canvas.framing) {
        // Image coordinates are framed positions over the scale (pixel i's centre at i + 0.5,
        // FramePoint's convention), and to_photo gives the photo point under one.
        const PictureFrame& f = *canvas.framing;
        const double scale = static_cast<double>(f.scale);
        const PhotoPoint q = FrameMap(f.fitted, f.photo_width, f.photo_height)
                                 .to_photo({static_cast<double>(image.x) * scale,
                                            static_cast<double>(image.y) * scale});
        return {static_cast<float>(q.u), static_cast<float>(q.v)};
    }
    // Image coordinates run along the pixels' edges, so pixel x's centre is at x + 0.5, and
    // stroke_field.hpp's u = (x + 0.5) / W is the image coordinate over the width.
    return {image.x / static_cast<float>(canvas.width),
            image.y / static_cast<float>(canvas.height)};
}

ViewPoint brush_canvas_point(FieldPoint p, const BrushCanvas& canvas) {
    if (canvas.width <= 0 || canvas.height <= 0) {
        return {};
    }
    if (canvas.framing) {
        const PictureFrame& f = *canvas.framing;
        const double scale = static_cast<double>(f.scale);
        const FramePoint q = FrameMap(f.fitted, f.photo_width, f.photo_height)
                                 .to_frame({static_cast<double>(p.u), static_cast<double>(p.v)});
        return {static_cast<float>(q.x / scale), static_cast<float>(q.y / scale)};
    }
    return {p.u * static_cast<float>(canvas.width), p.v * static_cast<float>(canvas.height)};
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
    const PixelSize photo = brush_photo_size(canvas_);
    if (far_enough(last_, p, aspect_of(photo.width, photo.height), spacing_)) {
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
            switched_on_ = false;
            if (!session.settings().dodge_burn_on) {
                // In the stroke's own gesture: one undo step for the stroke and the switch.
                EditSettings on = session.settings();
                on.dodge_burn_on = true;
                switched_on_ = true;
                if (MaybeError error = session.set_settings(on)) {
                    session.end_gesture();
                    before_.reset();
                    switched_on_ = false;
                    return error;
                }
            }
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
                const PixelSize photo = brush_photo_size(canvas);
                field->aspect = aspect_of(photo.width, photo.height);
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
            EditSettings restored = session.settings();
            restored.strokes = std::move(*before_);
            if (switched_on_) {
                restored.dodge_burn_on = false;
            }
            before_.reset();
            switched_on_ = false;
            MaybeError error = session.set_settings(restored);
            session.end_gesture();
            return error;
        }
        case BrushActionKind::leave_brush:
        case BrushActionKind::passthrough: return std::nullopt;
    }
    return std::nullopt;
}

bool brush_key(BrushMode& brush, BrushKey key, bool shift) noexcept {
    if (!brush.on()) {
        return false;
    }
    const float radius = brush.radius();
    const float softness = brush.softness();
    switch (key) {
        case BrushKey::x: brush.swap_tone(); return true;
        case BrushKey::left_bracket:
            if (shift) {
                brush.set_softness(softness + brush_softness_step);
            } else {
                brush.set_radius(radius / brush_size_step);
            }
            break;
        case BrushKey::right_bracket:
            if (shift) {
                brush.set_softness(softness - brush_softness_step);
            } else {
                brush.set_radius(radius * brush_size_step);
            }
            break;
    }
    return brush.radius() != radius || brush.softness() != softness;
}

BrushTone brush_shown_tone(BrushTone base, bool alt_held,
                           std::optional<BrushTone> stroke) noexcept {
    if (stroke) {
        return *stroke;
    }
    return alt_held ? other_tone(base) : base;
}

std::string brush_status_text(BrushTone tone, float strength) {
    const std::string amount = brush_strength_text(strength);
    return tone == BrushTone::dodge
               ? "Dodge " + amount + " — lighter, like holding back light"
               : "Burn " + amount + " — darker, like more time under the enlarger";
}

BrushRing brush_ring(const ViewState& s, const ViewGeometry& g, float radius,
                     float softness) noexcept {
    const int long_side = std::max(g.image_width, g.image_height);
    if (long_side <= 0 || g.density <= 0.0f || !std::isfinite(radius) || radius <= 0.0f) {
        return {};
    }
    // Zoom is device pixels per image pixel; screen units are device pixels over the density.
    const float outer = radius * static_cast<float>(long_side) * view_zoom(s, g) / g.density;
    const float soft = std::isfinite(softness) ? std::clamp(softness, 0.0f, 1.0f) : 0.0f;
    return {outer, outer * (1.0f - soft)};
}

BrushRing brush_ring(const ViewState& s, const ViewGeometry& g, const BrushCanvas& canvas,
                     float radius, float softness) noexcept {
    const int long_side = brush_long_side(canvas);
    if (long_side <= 0 || g.density <= 0.0f || !std::isfinite(radius) || radius <= 0.0f ||
        g.image_width <= 0 || g.image_height <= 0) {
        return {};
    }
    const float outer = radius * static_cast<float>(long_side) * view_zoom(s, g) / g.density;
    const float soft = std::isfinite(softness) ? std::clamp(softness, 0.0f, 1.0f) : 0.0f;
    return {outer, outer * (1.0f - soft)};
}

bool stroke_mask_shown(bool brushing, bool m_held, bool typing) noexcept {
    return brushing && m_held && !typing;
}

std::uint32_t stroke_mask_abgr(std::uint8_t byte, std::uint32_t dodge_abgr,
                               std::uint32_t burn_abgr) noexcept {
    if (byte == 128) {
        return 0u;
    }
    const std::uint32_t colour = byte > 128 ? dodge_abgr : burn_abgr;
    const int distance = byte > 128 ? byte - 128 : 128 - byte;  // 1 to 127
    const std::uint32_t alpha = (colour >> 24) & 0xFFu;
    const std::uint32_t scaled = (alpha * static_cast<std::uint32_t>(distance) + 63u) / 127u;
    return (colour & 0x00FFFFFFu) | (scaled << 24);
}

}  // namespace prism::gui
