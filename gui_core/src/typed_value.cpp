#include "prism/gui/typed_value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

namespace {

constexpr std::array<std::string_view, 1> exposure_units{"stops"};
constexpr std::array<std::string_view, 2> factor_units{"\xC3\x97", "x"};  // "×" in UTF-8, and x

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_word(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

std::string_view trimmed(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// 10^k, exact for k up to 22 (a double holds those exactly), else the library's best.
double power_of_ten(int k) noexcept {
    static constexpr double exact[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                       1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                       1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    return k <= 22 ? exact[k] : std::pow(10.0, k);
}

constexpr float largest_typed = 1e30f;

}  // namespace

std::optional<float> parse_typed_number(std::string_view text,
                                        std::span<const std::string_view> units) {
    text = trimmed(text);
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    std::uint64_t mantissa = 0;  // the first 18 significant digits
    int significant = 0;
    int dropped_integer_digits = 0;  // integer digits past the 18th: each is a factor of ten
    int fraction_digits = 0;         // digits after the mark that went into the mantissa
    int digits = 0;
    bool seen_mark = false;
    std::size_t i = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '.' || c == ',') {
            if (seen_mark) {
                return std::nullopt;
            }
            seen_mark = true;
            continue;
        }
        if (c < '0' || c > '9') {
            break;
        }
        ++digits;
        if (mantissa == 0 && c == '0') {
            // A leading zero adds nothing, but after the mark it still moves the point.
            fraction_digits += seen_mark ? 1 : 0;
            continue;
        }
        if (significant < 18) {
            mantissa = mantissa * 10 + static_cast<std::uint64_t>(c - '0');
            ++significant;
            fraction_digits += seen_mark ? 1 : 0;
        } else if (!seen_mark) {
            ++dropped_integer_digits;
        }
    }
    if (digits == 0) {
        return std::nullopt;
    }

    // What follows the number: nothing, or one unit word after optional spaces.
    std::string_view rest = trimmed(text.substr(i));
    if (!rest.empty()) {
        bool known = false;
        for (const std::string_view unit : units) {
            known = known || same_word(rest, unit);
        }
        if (!known) {
            return std::nullopt;
        }
    }

    double value = static_cast<double>(mantissa);
    if (dropped_integer_digits > 0) {
        value *= power_of_ten(std::min(dropped_integer_digits, 400));
    }
    if (fraction_digits > 0) {
        value /= power_of_ten(fraction_digits);
    }
    const float magnitude = value > static_cast<double>(largest_typed)
                                ? largest_typed
                                : static_cast<float>(value);
    return negative ? -magnitude : magnitude;
}

std::span<const std::string_view> typed_units(SimpleControl control) noexcept {
    if (control == SimpleControl::brightness) {
        return exposure_units;
    }
    return factor_units;
}

std::optional<float> typed_row_value(SimpleControl control, std::string_view text) {
    return parse_typed_number(text, typed_units(control));
}

std::optional<float> typed_slider_position(SimpleControl control, std::string_view text) {
    const std::optional<float> value = typed_row_value(control, text);
    if (!value) {
        return std::nullopt;
    }
    return simple_position_for(control, *value);
}

std::string typed_field_text(const EditSettings& s, SimpleControl control) {
    EditSettings stored = s;
    set_stage_enabled(stored, stage_of(control), true);  // the stored value, also when off
    const std::string line = stage_value_text(stored, stage_of(control));
    constexpr std::string_view times = "\xC3\x97";
    if (control != SimpleControl::brightness && line.compare(0, times.size(), times) == 0) {
        return line.substr(times.size());
    }
    // Saturation 0 reads "black and white": the number that gives it is 0.
    return control == SimpleControl::colour && stored.saturation == 0.0f ? "0.00" : line;
}

}  // namespace prism::gui
