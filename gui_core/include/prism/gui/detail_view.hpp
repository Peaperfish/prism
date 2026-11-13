#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "prism/frame.hpp"
#include "prism/gui/detail_base.hpp"
#include "prism/gui/view_transform.hpp"
#include "prism/resize.hpp"

// Real-pixel zoom (#37): the view's numbers measured against the file rather than the preview.
// These are pure functions; the canvas (gui_app) asks them what to show.
//
// The view's image space stays the framed preview's pixels (view_transform.hpp), so panning, the
// brush and the framing canvas are untouched. What changes is what a zoom means: 100% is one pixel
// of the file (the framed picture at the export's own size, before any reduction) on one device
// pixel. On a 2x Retina screen a file pixel is then half a point across.
//
// Units used below:
//   points per preview pixel  the view's scale: screen units across one preview pixel. ViewState's
//                             zoom is device pixels per preview pixel, so this is that zoom divided
//                             by ViewGeometry::density.
//   density                   device pixels per screen unit (ImGui's DisplayFramebufferScale).
//   file percent              device pixels per file pixel, times 100.

namespace prism::gui {

// How many file pixels one preview pixel stands for, along each axis of the framed picture: the
// framed full size (plan_export_frame on the export's decode) over the framed preview size
// (plan_export_frame on the preview base). The two are rounded to whole pixels on their own, so
// the axes differ slightly (about 0.03% on a 60 MP photo) and each is kept.
//
// `shown` is the one the zoom is read along: the axis on which the framed preview is longer (x
// for a square). Its ratio is the most exact, since the half pixel either size can be rounded by
// is the smallest part of the longer side; and it is the side the chip's percentage is about.
struct FileScale {
    double x = 1.0;
    double y = 1.0;
    double shown = 1.0;
};

// The scale from `framed_preview` up to `framed_full`. 1 on both axes if either size is empty.
[[nodiscard]] FileScale file_scale(PixelSize framed_full, PixelSize framed_preview) noexcept;

// The closest the view goes: 800% of the file, eight device pixels per file pixel.
inline constexpr double file_zoom_max_percent = 800.0;

// The zoom in percent of the file for a view showing `points_per_preview_pixel` on a screen of
// `density`: 100 * points_per_preview_pixel * density / scale.shown.
[[nodiscard]] double file_zoom_percent(double points_per_preview_pixel, double density,
                                       FileScale scale) noexcept;

// The view's scale (points per preview pixel) that shows `percent` of the file: the inverse of
// file_zoom_percent; 0 if density isn't above 0. For "100% (real pixels)" and for the zoom steps,
// which step in file percent: view_step_zoom(percent / 100, fit_percent / 100, direction) * 100
// gives the next stop, its top stop being 800%.
[[nodiscard]] double view_scale_for_file_percent(double percent, double density,
                                                 FileScale scale) noexcept;

// The largest view scale allowed: 800% of the file, or the fit's scale if that is larger (a tiny
// photo whose fit is beyond 800% stays at its fit, as view_zoom_max says for the preview). Not yet
// enforced: view_settled and view_zoomed_to still clamp at view_zoom_max preview pixels (about 215%
// of a 60 MP file) until the canvas takes this as its limit.
[[nodiscard]] double max_view_scale(double fit_points_per_preview_pixel, double density,
                                    FileScale scale) noexcept;

// The chip's number: "N%", N rounded to the nearest whole percent, halves away from zero, as
// view_zoom_label rounds. No " of preview": the percent is of the file.
[[nodiscard]] std::string file_zoom_label(double percent);

// How far two device-pixels-per-preview-pixel zooms may differ and still count as the same: the
// tolerance of view_same_zoom. Zooms are stored as floats, whose rounding is about 6e-8 of the
// value, so this is far above that noise; and it is far below anything visible (at a zoom of
// 1 + 1e-4 a 2560 px preview is a quarter of a device pixel wider).
inline constexpr double real_pixels_tolerance = 1e-4;

// Whether the canvas would show preview pixels enlarged, so real pixels are wanted: more than one
// device pixel per preview pixel, by more than real_pixels_tolerance. At or below that the preview
// already has a pixel (or more) for each device pixel, and nothing changes from 0.6.0.
[[nodiscard]] bool real_pixels_needed(double points_per_preview_pixel, double density) noexcept;

// A rectangle in preview pixels (the view's image space): [x0, x1) x [y0, y1), continuous.
struct PreviewRect {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

// The file pixels `preview` covers, as render_region takes them: pixels of the framed picture at
// full size (`framed_full`), measured with the per-axis scale from `framed_preview`. Each edge is
// rounded outward (an edge in the middle of a file pixel takes that pixel in), then clamped to the
// framed picture. Nothing if nothing of the picture is left, either size is empty or an edge is
// NaN.
[[nodiscard]] std::optional<FrameBox> file_region(const PreviewRect& preview,
                                                  PixelSize framed_preview,
                                                  PixelSize framed_full) noexcept;

// The part of the picture on the canvas, in file pixels: view_visible_rect taken to preview pixels
// through view_screen_to_image, then file_region. The view's image is the framed preview
// (g.image_width x g.image_height).
[[nodiscard]] std::optional<FrameBox> visible_file_region(const ViewState& s, const ViewGeometry& g,
                                                          PixelSize framed_full) noexcept;

// The design's region snapping (#37 section 5): the region a detail render asks for is the visible
// one with its edges moved outward to the nearest multiple of detail_cell file pixels, so a pan of
// a few pixels needs no new render; when no gesture is under way one more cell on each side, so a
// small pan is sharp at once. Clamped to the framed picture. The budget below is measured on the
// visible region, not on this one: the snapping and margin add at most 2 or 4 cells across.
inline constexpr int detail_cell = 128;
[[nodiscard]] FrameBox detail_region(const FrameBox& visible, PixelSize framed_full,
                                     bool idle) noexcept;

// How many file pixels a region of real pixels may have. Megapixels are millions of pixels.
inline constexpr std::int64_t region_live_max_pixels = 8'000'000;  // rendered while dragging
inline constexpr std::int64_t region_max_pixels = 30'000'000;      // rendered at all

enum class RegionBudget {
    live,        // at most 8 MP: rendered at once, also during a drag, a pan or a zoom
    on_release,  // over 8 MP, at most 30 MP: rendered only once no gesture is under way
    never,       // over 30 MP: not rendered; the preview stays
};

// The budget of a region of `region.width` x `region.height` file pixels (an empty one is live).
[[nodiscard]] RegionBudget region_budget(const FrameBox& region) noexcept;

// Whether a region of this budget is rendered now, with or without a gesture under way.
[[nodiscard]] bool region_renders_now(RegionBudget budget, bool gesture) noexcept;

// The size a region's render is drawn from: what the detail worker hands the canvas.
//  - At 100% and above (one device pixel or more per file pixel, along the shown axis, within
//    real_pixels_tolerance of 100%): the region itself, so each file pixel is drawn whole.
//  - Below 100%: the region shrunk by area averaging (prism::downscale) to the device pixels it
//    covers, so the texture is never larger than the canvas in device pixels. Its long side is the
//    region's long side times the device pixels per file pixel, rounded to the nearest pixel
//    (halves up) and at least 1; the short side is downscaled_size's, so it is exactly what
//    downscale(image, long side) gives.
// Never 0 x 0: an empty region (which file_region never gives) counts as 1 x 1. A NaN density gives
// the region itself.
[[nodiscard]] PixelSize detail_output_size(const FrameBox& region, double points_per_preview_pixel,
                                           double density, FileScale scale);

// Why the zoom chip says "· preview": the canvas shows preview pixels enlarged, and real pixels
// can't take their place for a reason that lasts. A render of real pixels that is merely under way
// is not a reason: the chip would flash on every pan and slider move.
enum class PreviewChip {
    none,      // no chip: the preview isn't enlarged, or real pixels are (or soon will be) shown
    not_kept,  // DetailBaseState::off: no full-size photo is kept
    reading,   // the full-size photo is still being read
    failed,    // reading the full-size photo failed
    too_big,   // the full-size photo would take more than a quarter of the memory
    region_too_big,       // the part on screen is over 30 MP (RegionBudget::never)
    region_too_big_live,  // over 8 MP during a gesture (RegionBudget::on_release while one runs)
};

// The chip's rule. `enlarged` is real_pixels_needed for the view; `budget` is region_budget of the
// visible region; `gesture` is whether a drag, pan, wheel or brush stroke is under way. Nothing at
// all when the preview isn't enlarged, whatever the state. Then a base that isn't read gives its
// reason; a ready one (or one not needed, where the preview is the full photo) gives the budget's.
[[nodiscard]] PreviewChip preview_chip(bool enlarged, DetailBaseState base, RegionBudget budget,
                                       bool gesture) noexcept;

// The chip: "N%" (file_zoom_label), or "N% · preview" when `chip` gives a reason.
[[nodiscard]] std::string file_zoom_chip(double percent, PreviewChip chip);

// The View menu item that goes to 100%: "100% (real pixels)".
[[nodiscard]] std::string_view real_pixels_menu_item() noexcept;

// Explain for 100%: "100%: one pixel of the photo on each pixel of your screen, the photo at its
// full size. On a Retina screen a photo pixel is half a point across."
[[nodiscard]] std::string_view real_pixels_explain() noexcept;

// One Explain line per chip reason, saying why the preview is shown; empty for PreviewChip::none.
// For example "Showing the preview, enlarged: the full-size photo is still being read."
[[nodiscard]] std::string_view preview_chip_explain(PreviewChip chip) noexcept;

}  // namespace prism::gui
