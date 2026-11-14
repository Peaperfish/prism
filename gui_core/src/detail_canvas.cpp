#include "prism/gui/detail_canvas.hpp"

#include <algorithm>
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
    const bool needed = real_pixels_needed(points, density);
    const bool enlarged = !view.fit && needed;
    if (in.mode != CanvasView::picture || !enlarged) {
        // Fit, not enlarged, or a preview-only view: nothing asked for. A preview-only view that
        // is enlarged says so on the chip (the picture view here is not enlarged: no chip).
        plan.chip =
            preview_only_chip(needed, view.fit, in.base_state == DetailBaseState::not_needed);
        return clear_all();
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
        // Held while refused, and after a refusal until the frame shown is of settings asked for
        // since (the last request's): only then may an older frame stand in again.
        const bool hold = !admitted || (last && last->hold &&
                                        !(shown && shown->key.settings == last->key.settings));
        plan.submit = DetailSubmit{request, output, key, in.framed_full, admitted, hold};
        latest = SubmittedDetail{key, request.region, output, in.framed_full, admitted, hold};
    }

    const bool refused = latest && !latest->admitted;
    const bool current_on_its_way = latest && latest->admitted && latest->key == key;
    const bool current = shown && shown->key.settings == in.settings;
    const bool older_may_stand_in =
        current_on_its_way && !(latest && latest->hold) && shown && shown->frame == in.frame;
    plan.draw =
        shown && !refused && shown->picture == in.framed_full && (current || older_may_stand_in);
    if (plan.draw) {
        plan.where = detail_screen_rect(view, in.geometry, in.framed_full, shown->region);
        plan.nearest = detail_shows_pixels(zoom, static_cast<float>(k.shown));
        plan.whole_pixels = !(static_cast<double>(zoom) / k.shown < 1.0 - real_pixels_tolerance);
        if (plan.chip == PreviewChip::region_too_big_live) {
            plan.chip = PreviewChip::none;  // real pixels are what is drawn
        }
    }
    return plan;
}

double view_file_percent(const ViewState& s, const ViewGeometry& g) noexcept {
    return 100.0 * static_cast<double>(view_zoom(s, g)) / static_cast<double>(view_actual_zoom(g));
}

std::string canvas_zoom_chip(const ViewState& s, const ViewGeometry& g, PreviewChip chip) {
    return file_zoom_chip(view_file_percent(s, g), chip);
}

bool view_at_actual_size(const ViewState& s, const ViewGeometry& g) noexcept {
    return !s.fit && view_same_zoom(view_zoom(s, g) / view_actual_zoom(g), 1.0f);
}

bool view_actual_size_available(const ViewGeometry& g) noexcept {
    return view_fit_zoom(g) < view_actual_zoom(g);  // as 0.6.0's fit < 1, in file pixels
}

std::optional<FilePixel> file_pixel_at(const ViewState& s, const ViewGeometry& g,
                                       PixelSize framed_full, ViewPoint screen) noexcept {
    if (framed_full.width < 1 || framed_full.height < 1 || !view_pixel_at(s, g, screen)) {
        return std::nullopt;
    }
    const FileScale k = file_scale(framed_full, {g.image_width, g.image_height});
    const ViewPoint image = view_screen_to_image(s, g, screen);
    const auto clamp = [](double v, int size) {
        return static_cast<int>(std::clamp(std::floor(v), 0.0, static_cast<double>(size - 1)));
    };
    return FilePixel{clamp(static_cast<double>(image.x) * k.x, framed_full.width),
                     clamp(static_cast<double>(image.y) * k.y, framed_full.height)};
}

FilePixel file_pixel_of(int x, int y, PixelSize framed_preview, PixelSize framed_full) noexcept {
    const FileScale k = file_scale(framed_full, framed_preview);
    const auto clamp = [](double v, int size) {
        return static_cast<int>(
            std::clamp(std::floor(v), 0.0, static_cast<double>(std::max(0, size - 1))));
    };
    return {clamp((x + 0.5) * k.x, framed_full.width), clamp((y + 0.5) * k.y, framed_full.height)};
}

PixelSource sample_source(const DetailFramePlan& plan, const std::optional<ShownDetail>& shown,
                          std::uint64_t base, std::uint64_t settings, FilePixel pixel) noexcept {
    if (!plan.draw || !plan.whole_pixels || !shown || base == 0 || shown->key.base != base ||
        shown->key.settings != settings) {
        return PixelSource::preview;
    }
    const FrameBox& r = shown->region;
    const bool inside = pixel.x >= r.x0 && pixel.y >= r.y0 &&
                        static_cast<long long>(pixel.x) < static_cast<long long>(r.x0) + r.width &&
                        static_cast<long long>(pixel.y) < static_cast<long long>(r.y0) + r.height;
    return inside ? PixelSource::photo : PixelSource::preview;
}

FilePixel file_pixel_rescaled(FilePixel pixel, PixelSize from, PixelSize to) noexcept {
    if (from.width < 1 || from.height < 1 || to.width < 1 || to.height < 1 || from == to) {
        return pixel;
    }
    return file_pixel_of(pixel.x, pixel.y, from, to);  // file_pixel_of maps a pixel by its middle
}

PickMarker file_pick_marker(const ViewState& s, const ViewGeometry& g, PixelSize framed_full,
                            FilePixel pixel) noexcept {
    PickMarker m;
    const std::optional<PatchBounds> bounds =
        pick_patch_bounds(framed_full.width, framed_full.height, pixel.x, pixel.y);
    if (!bounds) {
        return m;
    }
    m.patch = detail_screen_rect(
        s, g, framed_full,
        FrameBox{bounds->x0, bounds->y0, bounds->x1 - bounds->x0 + 1, bounds->y1 - bounds->y0 + 1});
    m.on_canvas = file_pixel_marker(s, g, framed_full, pixel).on_canvas;
    return m;
}

SampleMarker file_pixel_marker(const ViewState& s, const ViewGeometry& g, PixelSize framed_full,
                               FilePixel pixel) noexcept {
    SampleMarker m;
    m.square = detail_screen_rect(s, g, framed_full, FrameBox{pixel.x, pixel.y, 1, 1});
    m.centre = {(m.square.min.x + m.square.max.x) / 2.0f, (m.square.min.y + m.square.max.y) / 2.0f};
    const ViewRect canvas = view_canvas_rect(g);
    m.on_canvas = m.centre.x >= canvas.min.x && m.centre.x < canvas.max.x &&
                  m.centre.y >= canvas.min.y && m.centre.y < canvas.max.y;
    m.shows_square = detail_shows_pixels(view_zoom(view_settled(s, g), g), g.file_scale);
    return m;
}

int brush_spacing_long_side(const BrushCanvas& canvas, bool real_pixels,
                            PixelSize framed_full) noexcept {
    const int preview = brush_long_side(canvas);
    if (!real_pixels || preview < 1) {
        return preview;
    }
    const double k = file_scale(framed_full, {canvas.width, canvas.height}).shown;
    return static_cast<int>(std::max(1.0, std::round(static_cast<double>(preview) * k)));
}

}  // namespace prism::gui
