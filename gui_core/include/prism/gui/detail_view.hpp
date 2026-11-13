#pragma once

#include <string>

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
// file_zoom_percent. For "100% (real pixels)" and for the zoom steps, which step in file percent:
// view_step_zoom(percent / 100, fit_percent / 100, direction) * 100 gives the next stop, its top
// stop being 800%.
[[nodiscard]] double view_scale_for_file_percent(double percent, double density,
                                                 FileScale scale) noexcept;

// The largest view scale allowed: 800% of the file, or the fit's scale if that is larger (a tiny
// photo whose fit is beyond 800% stays at its fit, as view_zoom_max says for the preview).
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

}  // namespace prism::gui
