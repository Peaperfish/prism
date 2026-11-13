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

namespace {

// `v` clamped to [0, limit] and made a whole number of pixels. Clamping first keeps the cast in
// range whatever the double was.
int clamped_pixel(double v, int limit) noexcept {
    return static_cast<int>(std::clamp(v, 0.0, static_cast<double>(limit)));
}

int cells_down(int v, int cells) noexcept {
    return v / detail_cell * detail_cell - cells * detail_cell;
}

int cells_up(int v, int cells) noexcept {
    return (v + detail_cell - 1) / detail_cell * detail_cell + cells * detail_cell;
}

}  // namespace

std::optional<FrameBox> file_region(const PreviewRect& preview, PixelSize framed_preview,
                                    PixelSize framed_full) noexcept {
    if (framed_preview.width < 1 || framed_preview.height < 1 || framed_full.width < 1 ||
        framed_full.height < 1) {
        return std::nullopt;
    }
    const FileScale k = file_scale(framed_full, framed_preview);
    const int x0 = clamped_pixel(std::floor(preview.x0 * k.x), framed_full.width);
    const int y0 = clamped_pixel(std::floor(preview.y0 * k.y), framed_full.height);
    const int x1 = clamped_pixel(std::ceil(preview.x1 * k.x), framed_full.width);
    const int y1 = clamped_pixel(std::ceil(preview.y1 * k.y), framed_full.height);
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return FrameBox{x0, y0, x1 - x0, y1 - y0};
}

std::optional<FrameBox> visible_file_region(const ViewState& s, const ViewGeometry& g,
                                            PixelSize framed_full) noexcept {
    const ViewRect visible = view_visible_rect(s, g);
    if (visible.empty()) {
        return std::nullopt;
    }
    const ViewPoint a = view_screen_to_image(s, g, visible.min);
    const ViewPoint b = view_screen_to_image(s, g, visible.max);
    return file_region({a.x, a.y, b.x, b.y}, {g.image_width, g.image_height}, framed_full);
}

FrameBox detail_region(const FrameBox& visible, PixelSize framed_full, bool idle) noexcept {
    const int margin = idle ? 1 : 0;
    const int x0 = std::max(0, cells_down(visible.x0, margin));
    const int y0 = std::max(0, cells_down(visible.y0, margin));
    const int x1 = std::min(framed_full.width, cells_up(visible.x0 + visible.width, margin));
    const int y1 = std::min(framed_full.height, cells_up(visible.y0 + visible.height, margin));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

RegionBudget region_budget(const FrameBox& region) noexcept {
    const std::int64_t pixels = static_cast<std::int64_t>(std::max(0, region.width)) *
                                static_cast<std::int64_t>(std::max(0, region.height));
    if (pixels <= region_live_max_pixels) {
        return RegionBudget::live;
    }
    return pixels <= region_max_pixels ? RegionBudget::on_release : RegionBudget::never;
}

bool region_renders_now(RegionBudget budget, bool gesture) noexcept {
    switch (budget) {
        case RegionBudget::live: return true;
        case RegionBudget::on_release: return !gesture;
        case RegionBudget::never: return false;
    }
    return false;
}

PixelSize detail_output_size(const FrameBox& region, double points_per_preview_pixel,
                             double density, FileScale scale) {
    const int width = std::max(1, region.width);
    const int height = std::max(1, region.height);
    const double device_per_file_pixel = points_per_preview_pixel * density / scale.shown;
    if (device_per_file_pixel >= 1.0 - real_pixels_tolerance) {
        return {width, height};
    }
    const int long_side = std::max(width, height);
    const double shown = std::round(static_cast<double>(long_side) * device_per_file_pixel);
    const int target = static_cast<int>(std::clamp(shown, 1.0, static_cast<double>(long_side)));
    return downscaled_size(width, height, target);
}

}  // namespace prism::gui
