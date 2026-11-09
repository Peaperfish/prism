#include "prism/gui/paper_grade.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

namespace {

// ISO 6846 range R for whole grades 0 to 5, from Ilford MULTIGRADE RC papers technical
// information, ISO 6846 range R, filters 0-5 (Ilford Multigrade RC Deluxe, current).
constexpr std::array<int, 6> iso_range{130, 110, 90, 70, 60, 50};

// The multiplier of a whole grade: grade 2 (R = 90) is 1.
constexpr double grade_two_range = 90.0;

double whole_grade_contrast(int grade) {
    return grade_two_range / static_cast<double>(iso_range[static_cast<std::size_t>(grade)]);
}

float float_of_grade(int grade) {
    return static_cast<float>(whole_grade_contrast(grade));
}

// `v` with one decimal, always with '.', whatever the locale.
std::string one_place(double v) {
    char buffer[64];
    const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof buffer,
                                                 static_cast<float>(v), std::chars_format::fixed, 1);
    return r.ec == std::errc{} ? std::string(buffer, r.ptr) : std::string("?");
}

}  // namespace

float contrast_for_grade(float grade) {
    const double g = std::isnan(grade) ? 2.0 : std::clamp(static_cast<double>(grade), 0.0,
                                                          static_cast<double>(highest_grade));
    const int below = std::min(static_cast<int>(std::floor(g)), highest_grade - 1);
    if (g == static_cast<double>(below)) {
        return float_of_grade(below);
    }
    if (g == static_cast<double>(below + 1)) {
        return float_of_grade(below + 1);
    }
    const double low = std::log(whole_grade_contrast(below));
    const double high = std::log(whole_grade_contrast(below + 1));
    return static_cast<float>(std::exp(low + (g - below) * (high - low)));
}

GradeReading grade_for_contrast(float contrast) {
    if (std::isnan(contrast) || !(contrast > 0.0f) || contrast < float_of_grade(lowest_grade)) {
        return {static_cast<float>(lowest_grade), GradeEnd::softer};
    }
    if (contrast > float_of_grade(highest_grade)) {
        return {static_cast<float>(highest_grade), GradeEnd::harder};
    }
    for (int whole = lowest_grade; whole <= highest_grade; ++whole) {
        if (contrast == float_of_grade(whole)) {
            return {static_cast<float>(whole), GradeEnd::within};
        }
    }
    const double c = std::log(static_cast<double>(contrast));
    int below = lowest_grade;
    while (below < highest_grade - 1 && contrast >= float_of_grade(below + 1)) {
        ++below;
    }
    const double low = std::log(whole_grade_contrast(below));
    const double high = std::log(whole_grade_contrast(below + 1));
    return {static_cast<float>(below + (c - low) / (high - low)), GradeEnd::within};
}

float grade_slider_position(float contrast) {
    return std::round(grade_for_contrast(contrast).grade * 2.0f);
}

float contrast_for_grade_position(float position) {
    return contrast_for_grade(position / 2.0f);
}

void apply_contrast(EditSettings& s, float contrast) {
    s.contrast = contrast;
    s.contrast_on = true;
}

std::string grade_number_text(float grade) {
    const double twice = static_cast<double>(grade) * 2.0;
    if (std::fabs(twice - std::round(twice)) <= 0.01) {  // within 0.005 of a whole or half grade
        const long halves = std::lround(twice);
        const std::string whole = std::to_string(halves / 2);
        return halves % 2 == 0 ? whole : whole + "½";
    }
    return one_place(grade);
}

std::string format_paper_grade(float contrast) {
    const GradeReading reading = grade_for_contrast(contrast);
    std::string line;
    switch (reading.end) {
        case GradeEnd::softer:
            line = "Softer than grade " + std::to_string(lowest_grade);
            break;
        case GradeEnd::harder:
            line = "Harder than grade " + std::to_string(highest_grade);
            break;
        case GradeEnd::within: line = "Grade " + grade_number_text(reading.grade); break;
    }
    return line + " · " + format_multiplier(contrast);
}

}  // namespace prism::gui
