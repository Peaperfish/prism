#include "prism/gui/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "prism/ranges.hpp"

namespace prism::gui {

namespace {

// The shortest decimal text that reads back as exactly `v` (std::to_chars guarantees that).
std::string shortest(float v) {
    char buffer[32];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer, v);
    if (r.ec != std::errc{}) {
        throw std::logic_error("to_chars failed");  // cannot happen: 32 chars fit any float
    }
    return std::string(buffer, r.ptr);
}

void check_range(std::vector<std::string>& problems, const char* name, float v, ValueRange range) {
    if (!in_range(v, range)) {
        problems.push_back(range_message(name, v, range));
    }
}

void check_positive(std::vector<std::string>& problems, const char* name, float v) {
    if (!is_positive(v)) {
        problems.push_back(positive_message(name, v));
    }
}

float clamp_simple(float v) {
    // std::clamp would pass NaN straight through; a slider position must be a real number.
    return std::isnan(v) ? 0.0f : std::clamp(v, simple_min, simple_max);
}

// The two fields' slider positions, not yet clamped. A stage that is off is at 0.
SimpleControls unclamped_simple(const EditSettings& s) {
    SimpleControls c;
    c.brightness = s.exposure_on ? s.exposure_stops * 50.0f : 0.0f;
    c.colour = s.saturation_on ? (s.saturation - 1.0f) * 100.0f : 0.0f;
    return c;
}

bool outside(float v) {
    return !(v >= simple_min && v <= simple_max);  // also true for NaN
}

constexpr StageInfo stages[] = {
    {StageId::decode, "decode", "Decode",
     "Decode: turns the file's pixel values into linear light, proportional to the light in the "
     "scene.",
     false},
    {StageId::exposure, "exposure", "Exposure",
     "Exposure: makes every part of the image brighter or darker by the same number of stops.",
     true},
    {StageId::dodge_burn, "dodge_burn", "Dodge & burn",
     "Dodge & burn: makes the parts of the image you paint over lighter or darker, by the number "
     "of stops you choose.",
     true, false},
    {StageId::white_balance, "white_balance", "White balance",
     "White balance: re-lights the photo as if the light you name, in Kelvin and tint, had lit it.",
     true},
    {StageId::matrix, "matrix", "Colour matrix",
     "Colour matrix: mixes red, green and blue in linear light to calibrate camera colour or apply "
     "a grade.",
     true},
    {StageId::saturation, "saturation", "Saturation",
     "Saturation: scales each pixel's colourfulness around its luminance, which stays as it is.",
     true},
    {StageId::contrast, "contrast", "Contrast",
     "Contrast: widens or narrows the spread of light in stops around middle grey with a smooth "
     "S-curve.",
     true},
    {StageId::curve, "curve", "Tone curve",
     "Tone curve: shapes tones and rolls off bright highlights so scene light fits the screen.",
     true},
    {StageId::encode, "encode", "Output encode",
     "Output encode: converts linear light to 8-bit sRGB for the screen and exported files.",
     false},
};

// One entry per StageId, each at its own number: ClipReport and PixelTrace index by it.
constexpr bool stages_in_order() {
    if (std::size(stages) != stage_count) {
        return false;
    }
    for (std::size_t i = 0; i < std::size(stages); ++i) {
        if (static_cast<std::size_t>(stages[i].id) != i) {
            return false;
        }
    }
    return true;
}
static_assert(stages_in_order());

}  // namespace

std::span<const StageInfo> pipeline_stages() noexcept {
    return stages;
}

const StageInfo& stage_info(StageId id) noexcept {
    for (const StageInfo& info : stages) {
        if (info.id == id) {
            return info;
        }
    }
    return stages[0];
}

bool stage_enabled(const EditSettings& s, StageId id) noexcept {
    switch (id) {
        case StageId::decode: return true;
        case StageId::exposure: return s.exposure_on;
        case StageId::dodge_burn: return s.dodge_burn_on;
        case StageId::white_balance: return s.white_balance_on;
        case StageId::matrix: return s.matrix_on;
        case StageId::saturation: return s.saturation_on;
        case StageId::contrast: return s.contrast_on;
        case StageId::curve: return s.curve_on;
        case StageId::encode: return true;
    }
    return true;
}

void set_stage_enabled(EditSettings& s, StageId id, bool enabled) noexcept {
    switch (id) {
        case StageId::decode: break;
        case StageId::exposure: s.exposure_on = enabled; break;
        case StageId::dodge_burn: s.dodge_burn_on = enabled; break;
        case StageId::white_balance: s.white_balance_on = enabled; break;
        case StageId::matrix: s.matrix_on = enabled; break;
        case StageId::saturation: s.saturation_on = enabled; break;
        case StageId::contrast: s.contrast_on = enabled; break;
        case StageId::curve: s.curve_on = enabled; break;
        case StageId::encode: break;
    }
}

PixelGrid stage_grid(int full_w, int full_h, int x0, int y0, int stride, int cols,
                     int rows) noexcept {
    return PixelGrid{full_w, full_h, x0, y0, stride, cols, rows};
}

bool operator==(const Strokes& a, const Strokes& b) noexcept {
    if (a.empty() || b.empty()) {
        return a.empty() == b.empty();
    }
    return a.field_ == b.field_ || *a.field_ == *b.field_;
}

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
    if (s.white_balance) {
        // The range checks are on floats (a NaN fails them), and the ranges fit a float exactly.
        check_range(problems, "Kelvin", static_cast<float>(s.white_balance->kelvin), kelvin_range);
        check_range(problems, "Tint", static_cast<float>(s.white_balance->tint), tint_range);
    }
    check_range(problems, "Saturation", s.saturation, saturation_range);
    check_range(problems, "Contrast", s.contrast, contrast_range);
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

ProcessSettings to_process_settings(const EditSettings& s, StageId stop_after,
                                    const std::optional<Chromaticity>& as_shot_white,
                                    const std::optional<PixelGrid>& grid) {
    const std::vector<std::string> problems = validate(s);
    if (!problems.empty()) {
        std::string message = "invalid settings:";
        for (const std::string& p : problems) {
            message += " " + p + ".";
        }
        throw std::invalid_argument(message);
    }

    const auto reached = [&](StageId stage) {
        return static_cast<int>(stage) <= static_cast<int>(stop_after);
    };

    ProcessSettings p;
    if (reached(StageId::exposure) && s.exposure_on) {
        p.exposure_stops = s.exposure_stops;
    }
    if (reached(StageId::dodge_burn) && s.dodge_burn_on && !s.strokes.empty()) {
        if (!grid) {
            throw std::invalid_argument(
                "dodge & burn: the settings have strokes but no grid was given for the image");
        }
        p.dodge_burn.field = *s.strokes.field();
        p.dodge_burn.grid = *grid;
    }
    if (reached(StageId::white_balance) && s.white_balance_on && s.white_balance) {
        p.white_balance = white_balance_matrix(
            *s.white_balance, as_shot_white.value_or(white_of(default_as_shot)));
    }
    if (reached(StageId::matrix) && s.matrix_on && s.matrix) {
        p.matrix = s.matrix->values;
    }
    if (reached(StageId::saturation) && s.saturation_on) {
        p.saturation = s.saturation;
    }
    if (reached(StageId::contrast) && s.contrast_on) {
        p.contrast = s.contrast;
    }
    if (reached(StageId::curve) && s.curve_on) {
        if (const std::optional<std::string> spec = curve_spec(s)) {
            p.curve = prism::parse_curve(*spec);
        }
    }
    return p;
}

SimpleControls simple_from(const EditSettings& s) {
    const SimpleControls c = unclamped_simple(s);
    return {clamp_simple(c.brightness), clamp_simple(c.colour)};
}

SimpleOutOfRange outside_simple_range(const EditSettings& s) {
    const SimpleControls c = unclamped_simple(s);
    return {outside(c.brightness), outside(c.colour)};
}

void apply_simple(EditSettings& s, const SimpleControls& c) {
    apply_simple_control(s, SimpleControl::brightness, c.brightness);
    apply_simple_control(s, SimpleControl::colour, c.colour);
}

void apply_simple_control(EditSettings& s, SimpleControl control, float position) {
    switch (control) {
        case SimpleControl::brightness:
            s.exposure_stops = clamp_simple(position) / 50.0f;
            s.exposure_on = true;
            break;
        case SimpleControl::contrast:
            break;  // no position in this scale: Contrast is in paper grades
        case SimpleControl::colour:
            s.saturation = 1.0f + clamp_simple(position) / 100.0f;
            s.saturation_on = true;
            break;
    }
}

float simple_position_for(SimpleControl control, float value) {
    if (std::isnan(value)) {
        return 0.0f;
    }
    switch (control) {
        case SimpleControl::brightness: return clamp_simple(value * 50.0f);
        case SimpleControl::contrast: return 0.0f;  // no position in this scale
        case SimpleControl::colour: return clamp_simple((value - 1.0f) * 100.0f);
    }
    return 0.0f;
}

void reset_simple_control(EditSettings& s, SimpleControl control) {
    if (control == SimpleControl::contrast) {
        s.contrast = 1.0f;
        s.contrast_on = true;
        return;
    }
    apply_simple_control(s, control, 0.0f);
}

}  // namespace prism::gui
