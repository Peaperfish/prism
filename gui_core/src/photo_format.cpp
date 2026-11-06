#include "prism/gui/photo_format.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "prism/heic.hpp"
#include "prism/raw.hpp"

namespace prism::gui {
namespace {

// `value` with at most one decimal and no trailing ".0", written without the C locale's help.
// `always_one` keeps the decimal even when it is 0.
std::string number(double value, bool always_one = false) {
    const long tenths = std::lround(value * 10.0);
    std::string text = std::to_string(tenths / 10);
    if (tenths % 10 != 0 || always_one) {
        text += '.';
        text += static_cast<char>('0' + tenths % 10);
    }
    return text;
}

std::string join(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty()) {
            out += separator;
        }
        out += part;
    }
    return out;
}

}  // namespace

std::string format_focal(double mm, std::optional<int> mm35) {
    if (!(mm > 0.0)) {
        return {};
    }
    // Under 10 mm one decimal ("6.8 mm"); a value that rounds up to 10 reads "10 mm".
    std::string text = std::lround(mm * 10.0) < 100 ? number(mm, true) : std::to_string(std::lround(mm));
    text += " mm";
    if (mm35 && *mm35 > 0 && std::abs(*mm35 - mm) > 0.1 * mm) {
        text += " (" + std::to_string(*mm35) + " mm equiv.)";
    }
    return text;
}

std::string format_aperture(double f_number) {
    if (!(f_number > 0.0)) {
        return {};
    }
    return "f/" + number(f_number);
}

std::string format_shutter(double seconds) {
    if (!(seconds > 0.0)) {
        return {};
    }
    if (seconds < 1.0) {
        const double inverse = 1.0 / seconds;
        const double whole = std::round(inverse);
        if (whole >= 2.0 && std::abs(inverse - whole) <= 0.03 * whole) {
            return "1/" + std::to_string(static_cast<long>(whole)) + " s";
        }
    }
    return number(seconds) + " s";
}

std::string format_iso(int iso) {
    return iso > 0 ? "ISO " + std::to_string(iso) : std::string();
}

std::string camera_line(const PhotoInfo& info) {
    std::vector<std::string> parts;
    const auto add = [&](std::string text) {
        if (!text.empty()) {
            parts.push_back(std::move(text));
        }
    };
    if (info.model) {
        add(*info.model);
    }
    if (info.focal_length_mm) {
        add(format_focal(*info.focal_length_mm, info.focal_length_35mm));
    }
    if (info.f_number) {
        add(format_aperture(*info.f_number));
    }
    if (info.exposure_seconds) {
        add(format_shutter(*info.exposure_seconds));
    }
    if (info.iso) {
        add(format_iso(*info.iso));
    }
    return join(parts, " \xc2\xb7 ");
}

std::string camera_tooltip(const PhotoInfo& info) {
    std::vector<std::string> lines;
    for (const auto& text : {info.make, info.model, info.lens}) {
        if (text && !text->empty()) {
            lines.push_back(*text);
        }
    }
    if (info.date_taken && !info.date_taken->empty()) {
        lines.push_back("Taken " + *info.date_taken);
    }
    return join(lines, "\n");
}

std::string file_kind_label(const std::filesystem::path& path) {
    if (is_raw_file(path)) {
        return "RAW";
    }
    if (is_heic_file(path)) {
        return "HEIC";
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" ? "PNG" : "JPEG";
}

}  // namespace prism::gui
