#include "prism/gui/detail_canvas.hpp"

#include <cmath>

namespace prism::gui {

CanvasView canvas_view(bool framing, bool test_strip, bool compare, bool earlier_stage,
                       bool clip_overlay, bool stroke_mask) noexcept {
    if (framing) {
        return CanvasView::framing;
    }
    if (test_strip) {
        return CanvasView::test_strip;
    }
    if (compare) {
        return CanvasView::compare;
    }
    if (earlier_stage) {
        return CanvasView::earlier_stage;
    }
    if (clip_overlay) {
        return CanvasView::clip_overlay;
    }
    if (stroke_mask) {
        return CanvasView::stroke_mask;
    }
    return CanvasView::picture;
}

std::uint64_t DetailBaseNumber::update(const std::shared_ptr<const Photo>& base,
                                       bool photo_opened) {
    // The same object only if it shares the seen one's control block, which the weak reference
    // keeps alive: an object made at a freed one's address has another.
    const bool same =
        base && !seen_.expired() && !seen_.owner_before(base) && !base.owner_before(seen_);
    if (!base) {
        seen_.reset();
        current_ = 0;
    } else if (!same || photo_opened || current_ == 0) {
        seen_ = base;
        current_ = ++last_;
    }
    return current_;
}

bool wheel_gesture_live(double now_ms, std::optional<double> last_ms) noexcept {
    return last_ms && now_ms - *last_ms < wheel_gesture_ms;
}

bool canvas_gesture(const CanvasActivity& a) noexcept {
    return a.hand_drag || a.wheel || a.pinch || a.brush_stroke || a.control_active;
}

namespace {

// To a whole device pixel, as view_placement puts the picture's corner.
float snapped(float screen, float density) noexcept {
    return std::round(screen * density) / density;
}

}  // namespace

ViewRect detail_screen_rect(const ViewState& s, const ViewGeometry& g, PixelSize framed_full,
                            const FrameBox& region) noexcept {
    const FileScale k = file_scale(framed_full, {g.image_width, g.image_height});
    const auto to_screen = [&](double x, double y) {
        const ViewPoint p =
            view_image_to_screen(s, g, {static_cast<float>(x / k.x), static_cast<float>(y / k.y)});
        return ViewPoint{snapped(p.x, g.density), snapped(p.y, g.density)};
    };
    return {to_screen(region.x0, region.y0),
            to_screen(static_cast<double>(region.x0) + region.width,
                      static_cast<double>(region.y0) + region.height)};
}

bool detail_shows_pixels(float zoom, float file_scale) noexcept {
    const float k = std::isfinite(file_scale) && file_scale > 0.0f ? file_scale : 1.0f;
    return view_shows_pixels(zoom / k);
}

DetailFramePlan plan_detail_frame(const DetailFrameInput& in) {
    DetailFramePlan plan;
    const bool anything = in.shown.has_value() || in.submitted.has_value();
    const auto clear_all = [&] {
        plan.clear = anything;
        return plan;
    };

    const ViewState view = view_settled(in.view, in.geometry);
    const float zoom = view_zoom(view, in.geometry);
    const double density = in.geometry.density;
    const double points = density > 0.0 ? zoom / density : 0.0;
    const bool enlarged = !view.fit && real_pixels_needed(points, density);
    if (in.mode != CanvasView::picture || !enlarged) {
        return clear_all();  // Fit, not enlarged, or a preview-only view: as in 0.6.0
    }

    const bool base_usable =
        in.base != 0 && in.framed_full.width > 0 && in.framed_full.height > 0 &&
        (in.base_state == DetailBaseState::ready || in.base_state == DetailBaseState::not_needed);
    if (!base_usable) {
        plan.chip = preview_chip(true, false, in.base_state, RegionBudget::live, in.gesture);
        return clear_all();
    }
    if (in.framed_preview_now != PixelSize{in.geometry.image_width, in.geometry.image_height}) {
        return plan;  // the texture is a picture of before: wait for the current one
    }
    const std::optional<FrameBox> visible = visible_file_region(view, in.geometry, in.framed_full);
    if (!visible) {
        return clear_all();
    }

    const DetailRequest request = detail_request(*visible, in.framed_full, !in.gesture);
    plan.chip = preview_chip(true, false, in.base_state, request.budget, in.gesture);
    if (request.budget == RegionBudget::never) {
        return clear_all();
    }

    const bool stale = (in.submitted && in.submitted->key.base != in.base) ||
                       (in.shown && in.shown->key.base != in.base);
    plan.clear = stale;
    const std::optional<SubmittedDetail> last = stale ? std::nullopt : in.submitted;
    const std::optional<ShownDetail> shown = stale ? std::nullopt : in.shown;

    const FileScale k =
        file_scale(in.framed_full, {in.geometry.image_width, in.geometry.image_height});
    const PixelSize output = detail_output_size(request.region, points, density, k);
    const DetailKey key{in.base, in.settings};
    const bool admitted = detail_admit(request.budget, in.gesture) == DetailAdmit::render;
    const bool changed = !last || last->key != key || last->region != request.region ||
                         last->output != output || last->picture != in.framed_full ||
                         (!last->admitted && admitted);
    std::optional<SubmittedDetail> latest = last;
    if (changed) {
        plan.submit = DetailSubmit{request, output, key, in.framed_full, admitted};
        latest = SubmittedDetail{key, request.region, output, in.framed_full, admitted};
    }

    const bool refused = latest && !latest->admitted;
    const bool current_on_its_way = latest && latest->admitted && latest->key == key;
    plan.draw = shown && !refused && shown->picture == in.framed_full &&
                (shown->key.settings == in.settings || current_on_its_way);
    if (plan.draw) {
        plan.where = detail_screen_rect(view, in.geometry, in.framed_full, shown->region);
        plan.nearest = detail_shows_pixels(zoom, static_cast<float>(k.shown));
        if (plan.chip == PreviewChip::region_too_big_live) {
            plan.chip = PreviewChip::none;  // real pixels are what is drawn
        }
    }
    return plan;
}

}  // namespace prism::gui
