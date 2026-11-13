#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "prism/frame.hpp"
#include "prism/gui/detail_base.hpp"
#include "prism/gui/detail_renderer.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/image.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom (#37) on the canvas: each frame, what the canvas does about real pixels, decided
// here as pure functions so it can be tested without a window. The app only carries it out: it
// asks the DetailRenderer for what plan_detail_frame says, uploads the frames it gets back, and
// draws the shown one where the plan says, over the enlarged preview.

namespace prism::gui {

// What the canvas shows, as far as real pixels go. Only the final picture in the normal view gets
// real pixels; everything else draws from the preview's own data and stays exactly as in 0.6.0
// (no request, no "· preview" on the chip).
enum class CanvasView {
    picture,        // the final picture (Output encode), nothing over it from the preview's data
    framing,        // the easel: the whole turned photo on the GPU (#58)
    test_strip,     // the test strip's bands are rendered from the preview
    compare,        // the split Compare view: its other half is a preview render
    earlier_stage,  // a stage before Output encode is viewed in the light path
    clip_overlay,   // J: the clipping overlay is the preview's clip mask
    stroke_mask,    // M in Brush mode: the stroke mask is the preview's
};

// Which view the canvas is in, from the app's state: the first that holds of framing, the test
// strip, Compare (split), an earlier stage, the clipping overlay, the stroke mask; else picture.
[[nodiscard]] CanvasView canvas_view(bool framing, bool test_strip, bool compare,
                                     bool earlier_stage, bool clip_overlay,
                                     bool stroke_mask) noexcept;

// The number DetailKey::base carries for the session's detail base: a new one whenever
// Session::detail_base() gives another photo (a pointer to another object, or none), or another
// photo is opened (whose not_needed base could be at the address of the last one's). 0 while there
// is no base. It keeps only a weak reference, so it never holds a base the session let go of, and
// an object at a freed one's address still counts as another.
class DetailBaseNumber {
public:
    // UI thread, once per frame, with session.detail_base() and whether a photo was opened since
    // the last call. Returns the number now.
    std::uint64_t update(const std::shared_ptr<const Photo>& base, bool photo_opened);
    [[nodiscard]] std::uint64_t current() const noexcept { return current_; }

private:
    std::weak_ptr<const Photo> seen_;
    std::uint64_t last_ = 0;     // the last number given
    std::uint64_t current_ = 0;  // 0: no base
};

// How long after a wheel or trackpad scroll event the canvas still counts as being zoomed or
// panned, in milliseconds: wheel events come in bursts with gaps of a frame or more, which must not
// end the gesture (the region would be asked for with its margin and dropped again at the next
// event).
inline constexpr double wheel_gesture_ms = 200.0;

// Whether a wheel or trackpad scroll still counts as under way at `now_ms`: within wheel_gesture_ms
// of the last one (`last_ms`, nothing if there has been none).
[[nodiscard]] bool wheel_gesture_live(double now_ms, std::optional<double> last_ms) noexcept;

// What is under way on the canvas, as the app knows it this frame.
struct CanvasActivity {
    bool hand_drag = false;       // the picture is being dragged (panned)
    bool wheel = false;           // wheel_gesture_live: scrolling or zooming with the wheel
    bool pinch = false;           // a trackpad pinch this frame
    bool brush_stroke = false;    // a Dodge & burn stroke is being painted
    bool control_active = false;  // a control is being dragged (control_is_active): a slider
};

// Whether a gesture is under way: any of the above. RegionBudget::on_release regions wait for none.
[[nodiscard]] bool canvas_gesture(const CanvasActivity& a) noexcept;

// The detail frame now uploaded for drawing: what DetailRenderer::poll gave.
struct ShownDetail {
    DetailKey key;
    FrameBox region;
    PixelSize picture;  // DetailFrame::picture
    bool operator==(const ShownDetail&) const = default;
};

// The request last handed to the renderer.
struct SubmittedDetail {
    DetailKey key;
    FrameBox region;
    PixelSize output;
    PixelSize picture;      // the framed picture the region is in
    bool admitted = false;  // DetailRenderer::submit gave it a generation (not 0)
    bool operator==(const SubmittedDetail&) const = default;
};

// What plan_detail_frame looks at.
struct DetailFrameInput {
    // The view, and the canvas with the picture as drawn: its image is the preview texture (the
    // framed preview), and its file_scale is set.
    ViewState view;
    ViewGeometry geometry;
    // Session::preview_base()'s size. The texture is the current picture only when it is this size;
    // after a frame change the old texture shows for a moment, and nothing is placed on it then.
    PixelSize framed_preview_now;
    // The framed picture at full size: plan_export_frame(settings.frame, detail base size).size, or
    // 0 x 0 with no detail base.
    PixelSize framed_full;
    DetailBaseState base_state = DetailBaseState::off;
    std::uint64_t base = 0;      // DetailBaseNumber: 0 is no base
    std::uint64_t settings = 0;  // Session::settings_generation()
    bool gesture = false;        // canvas_gesture
    CanvasView mode = CanvasView::picture;
    std::optional<ShownDetail> shown;          // the frame uploaded, if any
    std::optional<SubmittedDetail> submitted;  // the last request, if any (since the last clear)
};

// What to ask the renderer for: DetailRenderRequest without the base and the settings, which the
// app adds (Session::detail_base(), Session::settings()).
struct DetailSubmit {
    DetailRequest request;
    PixelSize output;
    DetailKey key;
    PixelSize picture;  // for SubmittedDetail
    // What detail_admit will say, for the record the app keeps until submit() says.
    bool admitted = false;
};

// What the app does this frame, in this order: clear() the renderer and forget the shown frame and
// the last request; submit(); draw the shown frame at `where` (after drawing the preview, before
// the overlays), sampled with nearest filtering if `nearest`; give the chip `chip`.
struct DetailFramePlan {
    bool clear = false;
    std::optional<DetailSubmit> submit;
    bool draw = false;
    ViewRect where;
    bool nearest = false;
    PreviewChip chip = PreviewChip::none;
};

// The canvas's rule for real pixels (#37):
//
// - Only for CanvasView::picture, with the view zoomed in (not Fit) so far that the preview is
//   enlarged (real_pixels_needed). Otherwise nothing is asked for, nothing drawn, no chip: Fit and
//   every preview-only view look exactly as in 0.6.0; and anything asked for or shown is cleared.
// - Without a usable detail base (none, or not ready): nothing is asked for or drawn, anything of
//   an old base is cleared, and the chip says why (preview_chip).
// - The request is detail_request for the visible file region, with the margin only when no
//   gesture is under way, and detail_output_size for the view. It is handed over only when it
//   differs from the last one (region, output, picture, base or settings), or the last was refused
//   during a gesture and is now admitted. Over 30 MP (RegionBudget::never) nothing is asked for,
//   anything asked for or shown is cleared, and the chip says region_too_big.
// - A frame of another base than the current one is never drawn; the request and frame of an old
//   base are cleared (and the new base asked for in the same frame).
// - The shown frame is drawn only if it is in the current framed picture, and its settings are the
//   current ones or the last request is for the current ones and admitted (a slider drag shows real
//   pixels a moment behind rather than none). If the last request was refused during a gesture,
//   nothing is drawn (the preview shows) and the chip says region_too_big_live; that reason only
//   shows when nothing is drawn.
// - While the texture is not yet the current picture (framed_preview_now differs), nothing is
//   asked for, drawn or cleared.
// - `where` is the frame's region taken to preview pixels by the per-axis file scale, then to the
//   screen (view_image_to_screen), each edge on a whole device pixel as view_placement puts the
//   picture's corner. `nearest` is detail_shows_pixels.
[[nodiscard]] DetailFramePlan plan_detail_frame(const DetailFrameInput& in);

// Where `region` of a `framed_full` picture lands on the screen for the view: its edges taken to
// preview pixels by the per-axis file scale (file_scale(framed_full, image size)), to the screen by
// view_image_to_screen, and each rounded to a whole device pixel.
[[nodiscard]] ViewRect detail_screen_rect(const ViewState& s, const ViewGeometry& g,
                                          PixelSize framed_full, const FrameBox& region) noexcept;

// Whether a detail frame is drawn with nearest sampling: the preview's rule (view_shows_pixels)
// applied to device pixels per file pixel rather than per preview pixel, so photo pixels show as
// squares past 200% of the file. `zoom` is device pixels per preview pixel, `file_scale` file
// pixels per preview pixel (ViewGeometry::file_scale).
[[nodiscard]] bool detail_shows_pixels(float zoom, float file_scale) noexcept;

// --- The zoom chip and 100%
// -----------------------------------------------------------------------

// The zoom in percent of the file for the view: 100 x device pixels per file pixel.
[[nodiscard]] double view_file_percent(const ViewState& s, const ViewGeometry& g) noexcept;

// The zoom chip's text: file_zoom_chip(view_file_percent, chip), e.g. "25%", "400% · preview".
[[nodiscard]] std::string canvas_zoom_chip(const ViewState& s, const ViewGeometry& g,
                                           PreviewChip chip);

// Whether the view is at 100% of the file (zoomed, not Fit, within view_same_zoom of it), so the
// chip's 100% is lit and the View menu's "100% (real pixels)" has nothing to do.
[[nodiscard]] bool view_at_actual_size(const ViewState& s, const ViewGeometry& g) noexcept;

// Whether 100% of the file can be chosen: not when the fit already shows the photo at 100% or
// more (a photo smaller than the canvas in device pixels).
[[nodiscard]] bool view_actual_size_available(const ViewGeometry& g) noexcept;

}  // namespace prism::gui
