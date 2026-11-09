#pragma once

#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// Contrast shown as a darkroom paper grade. The engine, recipes and the CLI keep the multiplier
// (EditSettings::contrast, the slope at middle grey); a grade is only another way for the window to
// show and set it.
//
// A grade is a fixed step of paper contrast. Each whole grade 0 to 5 is the ISO range R of a real
// paper through its filters (see paper_grade.cpp): a softer paper needs a longer range of exposure
// to go from white to black, so grade 2, R = 90, is the multiplier 1 and any grade is 90 / R. Between
// two whole grades the multiplier moves in a straight line in its logarithm, so each half grade is
// the same ratio as the next.

inline constexpr int lowest_grade = 0;
inline constexpr int highest_grade = 5;

// The Contrast slider snaps to half grades: positions 0 to 10, grade = position / 2.
inline constexpr int grade_slider_steps = 10;
inline constexpr int grade_slider_neutral = 4;  // grade 2, the multiplier 1

// The multiplier for `grade`, clamped to 0 to 5 (NaN gives grade 2). A whole grade is exactly
// 90 / R, so grade 2 is exactly 1.
[[nodiscard]] float contrast_for_grade(float grade);

// Where a multiplier falls against the grades.
enum class GradeEnd { within, softer, harder };

struct GradeReading {
    float grade = 2.0f;  // the grade, held at 0 or 5 when the multiplier is past the ends
    GradeEnd end = GradeEnd::within;
};

// The inverse of contrast_for_grade on its range: a whole grade's own multiplier reads back as
// exactly that grade. A multiplier below grade 0's is `softer`, above grade 5's is `harder`; NaN
// and 0 or less count as softer.
[[nodiscard]] GradeReading grade_for_contrast(float contrast);

// The Contrast slider's position (0 to 10, a whole number) nearest to `contrast`, and the
// multiplier a position stands for.
[[nodiscard]] float grade_slider_position(float contrast);
[[nodiscard]] float contrast_for_grade_position(float position);

// Sets only the contrast multiplier and turns only that stage on, as apply_simple_control does
// for the other two sliders. The Contrast slider's position goes through contrast_for_grade_position
// first, and a typed value is already a multiplier.
void apply_contrast(EditSettings& s, float contrast);

// The Contrast value line: "Grade 3 · ×1.29", "Grade 3½ · ×1.39", "Grade 2.7 · ×1.15", and past the
// ends "Softer than grade 0 · ×0.60" and "Harder than grade 5 · ×2.00". A grade within 0.005 of a
// whole or half grade reads as that one.
[[nodiscard]] std::string format_paper_grade(float contrast);

// Just the grade part of that line, "3", "3½", "2.7", for the typed field to hold.
[[nodiscard]] std::string grade_number_text(float grade);

}  // namespace prism::gui
