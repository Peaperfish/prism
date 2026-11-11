#include "prism/gui/export_size.hpp"

#include <algorithm>
#include <string>

#include "prism/resize.hpp"

namespace prism::gui {

std::optional<int> export_long_side(const ExportSize& size, int file_width, int file_height) {
    if (file_width < 1 || file_height < 1) {
        return std::nullopt;
    }
    int wanted = 0;
    switch (size.mode) {
        case ExportSize::Mode::original:
            return std::nullopt;
        case ExportSize::Mode::long_side:
            wanted = size.long_side;
            break;
        case ExportSize::Mode::percent:
            if (size.percent < 1 || size.percent > 100) {
                return std::nullopt;
            }
            wanted = long_side_for_percent(file_width, file_height, size.percent);
            break;
    }
    if (wanted < 1 || wanted >= std::max(file_width, file_height)) {
        return std::nullopt;
    }
    return wanted;
}

namespace {

std::string times(PixelSize size) {
    return std::to_string(size.width) + " \xC3\x97 " + std::to_string(size.height);
}

}  // namespace

ExportPlan plan_export(const ExportSize& size, PixelSize file, PixelSize start) {
    ExportPlan plan{start, start, std::nullopt};
    if (start.width < 1 || start.height < 1) {
        return plan;
    }
    const std::optional<int> wanted = export_long_side(size, file.width, file.height);
    if (wanted && std::max(start.width, start.height) > *wanted) {
        plan.long_side = wanted;
        plan.size = downscaled_size(start.width, start.height, *wanted);
    }
    return plan;
}

PixelSize framed_export_size(const std::optional<Frame>& frame, PixelSize size) {
    if (size.width < 1 || size.height < 1) {
        return size;
    }
    return plan_export_frame(frame, size.width, size.height).size;
}

std::string_view size_estimate_prefix(bool exact) noexcept {
    return exact ? "" : "about ";
}

std::string export_result_text(const ExportSize& size, const ExportPlan& plan, bool exact) {
    const std::string at = std::string(size_estimate_prefix(exact)) + times(plan.size);
    if (plan.reduced()) {
        return "Exports at " + at;
    }
    if (size.mode == ExportSize::Mode::original) {
        return "Exports at full size, " + at;
    }
    return "Not smaller than the photo: exports at full size, " + at;
}

std::string export_original_text(PixelSize file, bool exact) {
    return "Full size, " + std::string(size_estimate_prefix(exact)) + times(file);
}

}  // namespace prism::gui
