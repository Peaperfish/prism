#include "prism/gui/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace prism::gui {

namespace {

// Contrast's Simple mapping: contrast = base^(slider / 100).
constexpr float simple_contrast_base = 1.5f;

// The shortest decimal text that reads back as exactly `v` (std::to_chars guarantees that).
std::string shortest(float v) {
    char buffer[32];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, v);
    if (r.ec != std::errc{}) {
        throw std::logic_error("to_chars failed");  // cannot happen: 32 chars fit any float
    }
    return std::string(buffer, r.ptr);
}

void check_range(std::vector<std::string>& problems, const char* name, float v, float min,
                 float max) {
    if (!std::isfinite(v) || v < min || v > max) {
        std::ostringstream message;
        message << name << " must be from " << min << " to " << max << " (it is " << v << ")";
        problems.push_back(message.str());
    }
}

void check_positive(std::vector<std::string>& problems, const char* name, float v) {
    if (!std::isfinite(v) || v <= 0.0f) {
        std::ostringstream message;
        message << name << " must be a number above 0 (it is " << v << ")";
        problems.push_back(message.str());
    }
}

float clamp_simple(float v) {
    // std::clamp would pass NaN straight through; a slider position must be a real number.
    return std::isnan(v) ? 0.0f : std::clamp(v, simple_min, simple_max);
}

// The three fields' slider positions, not yet clamped. A stage that is off is at 0.
SimpleControls unclamped_simple(const EditSettings& s) {
    SimpleControls c;
    c.brightness = s.exposure_on ? s.exposure_stops * 50.0f : 0.0f;
    c.contrast =
        s.contrast_on ? 100.0f * std::log(s.contrast) / std::log(simple_contrast_base) : 0.0f;
    c.colour = s.saturation_on ? (s.saturation - 1.0f) * 100.0f : 0.0f;
    return c;
}

bool outside(float v) {
    return !(v >= simple_min && v <= simple_max);  // also true for NaN
}

}  // namespace

MatrixFile MatrixFile::load(const std::filesystem::path& path) {
    return {path, prism::load_matrix(path)};
}

void reset_to_defaults(EditSettings& s) {
    s = EditSettings{};
}

std::vector<std::string> validate(const EditSettings& s) {
    std::vector<std::string> problems;
    if (!std::isfinite(s.exposure_stops)) {
        problems.push_back("Exposure must be a finite number of stops");
    }
    check_range(problems, "Saturation", s.saturation, 0.0f, 10.0f);
    check_range(problems, "Contrast", s.contrast, 0.1f, 10.0f);
    check_positive(problems, "Gamma", s.gamma);
    check_positive(problems, "Reinhard white point", s.reinhard_white);
    return problems;
}

std::optional<std::string> curve_spec(const EditSettings& s) {
    switch (s.curve) {
        case CurveKind::none: return std::nullopt;
        case CurveKind::gamma: return "gamma:" + shortest(s.gamma);
        case CurveKind::reinhard: return "reinhard:" + shortest(s.reinhard_white);
        case CurveKind::filmic: return "filmic";
    }
    return std::nullopt;  // unreachable; keeps compilers from warning about a missing return
}

ProcessSettings to_process_settings(const EditSettings& s) {
    const std::vector<std::string> problems = validate(s);
    if (!problems.empty()) {
        std::string message = "invalid settings:";
        for (const std::string& p : problems) {
            message += " " + p + ".";
        }
        throw std::invalid_argument(message);
    }

    ProcessSettings p;
    if (s.exposure_on) {
        p.exposure_stops = s.exposure_stops;
    }
    if (s.matrix_on && s.matrix) {
        p.matrix = s.matrix->values;
    }
    if (s.saturation_on) {
        p.saturation = s.saturation;
    }
    if (s.contrast_on) {
        p.contrast = s.contrast;
    }
    if (const std::optional<std::string> spec = curve_spec(s); s.curve_on && spec) {
        p.curve = prism::parse_curve(*spec);
    }
    return p;
}

SimpleControls simple_from(const EditSettings& s) {
    const SimpleControls c = unclamped_simple(s);
    return {clamp_simple(c.brightness), clamp_simple(c.contrast), clamp_simple(c.colour)};
}

SimpleOutOfRange outside_simple_range(const EditSettings& s) {
    const SimpleControls c = unclamped_simple(s);
    return {outside(c.brightness), outside(c.contrast), outside(c.colour)};
}

void apply_simple(EditSettings& s, const SimpleControls& c) {
    s.exposure_stops = clamp_simple(c.brightness) / 50.0f;
    s.contrast = std::pow(simple_contrast_base, clamp_simple(c.contrast) / 100.0f);
    s.saturation = 1.0f + clamp_simple(c.colour) / 100.0f;
    s.exposure_on = s.contrast_on = s.saturation_on = true;
}

}  // namespace prism::gui
