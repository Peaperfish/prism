#include "prism/gui/export_size.hpp"

#include <algorithm>

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

}  // namespace prism::gui
