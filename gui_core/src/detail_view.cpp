#include "prism/gui/detail_view.hpp"

#include <algorithm>
#include <cmath>

namespace prism::gui {

FileScale file_scale(PixelSize framed_full, PixelSize framed_preview) noexcept {
    if (framed_full.width < 1 || framed_full.height < 1 || framed_preview.width < 1 ||
        framed_preview.height < 1) {
        return {};
    }
    const double x =
        static_cast<double>(framed_full.width) / static_cast<double>(framed_preview.width);
    const double y =
        static_cast<double>(framed_full.height) / static_cast<double>(framed_preview.height);
    return {x, y, framed_preview.width >= framed_preview.height ? x : y};
}

double file_zoom_percent(double points_per_preview_pixel, double density,
                         FileScale scale) noexcept {
    return 100.0 * points_per_preview_pixel * density / scale.shown;
}

double view_scale_for_file_percent(double percent, double density, FileScale scale) noexcept {
    return percent / 100.0 * scale.shown / density;
}

double max_view_scale(double fit_points_per_preview_pixel, double density,
                      FileScale scale) noexcept {
    return std::max(view_scale_for_file_percent(file_zoom_max_percent, density, scale),
                    fit_points_per_preview_pixel);
}

std::string file_zoom_label(double percent) {
    return std::to_string(std::lround(percent)) + "%";
}

bool real_pixels_needed(double points_per_preview_pixel, double density) noexcept {
    const double device_per_preview_pixel = points_per_preview_pixel * density;
    return device_per_preview_pixel > 1.0 + real_pixels_tolerance;
}

}  // namespace prism::gui
