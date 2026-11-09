#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "prism/gui/paper_grade.hpp"
#include "prism/gui/stage_controls.hpp"

using prism::gui::contrast_for_grade;
using prism::gui::format_paper_grade;
using prism::gui::GradeEnd;
using prism::gui::grade_for_contrast;

namespace {

// ISO range R of whole grades 0 to 5, typed again here so the table in the code is checked.
constexpr std::array<int, 6> ranges{130, 110, 90, 70, 60, 50};

double whole(int grade) {
    return 90.0 / ranges[static_cast<std::size_t>(grade)];
}

// The grade for a multiplier by the formula alone, not through the code under test.
double grade_by_formula(double contrast) {
    for (int g = 0; g < 5; ++g) {
        if (contrast <= whole(g + 1) * (1 + 1e-12)) {
            return g + (std::log(contrast) - std::log(whole(g))) /
                           (std::log(whole(g + 1)) - std::log(whole(g)));
        }
    }
    return 5.0;
}

std::string multiplier_text(float v) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", static_cast<double>(v));
    return std::string("×") + buffer;
}

}  // namespace

TEST(PaperGrade, WholeGradesAreNinetyOverTheirRange) {
    for (int g = 0; g <= 5; ++g) {
        EXPECT_EQ(contrast_for_grade(static_cast<float>(g)), static_cast<float>(whole(g))) << g;
    }
    EXPECT_EQ(contrast_for_grade(2.0f), 1.0f);
}

TEST(PaperGrade, AHalfGradeIsTheGeometricMeanOfItsNeighbours) {
    for (int g = 0; g < 5; ++g) {
        const double mean = std::sqrt(whole(g) * whole(g + 1));
        EXPECT_NEAR(contrast_for_grade(static_cast<float>(g) + 0.5f), mean, mean * 1e-6) << g;
    }
}

TEST(PaperGrade, HarderGradesAreMonotonicAndReadBackAsThemselves) {
    float previous = 0.0f;
    for (int i = 0; i <= 500; ++i) {
        const float g = static_cast<float>(i) * 0.01f;
        const float c = contrast_for_grade(g);
        EXPECT_GT(c, previous) << g;
        previous = c;
        const prism::gui::GradeReading back = grade_for_contrast(c);
        EXPECT_EQ(back.end, GradeEnd::within) << g;
        EXPECT_NEAR(back.grade, g, 1e-5f) << g;
    }
}

TEST(PaperGrade, WholeGradesRoundTripExactly) {
    for (int g = 0; g <= 5; ++g) {
        const prism::gui::GradeReading back =
            grade_for_contrast(contrast_for_grade(static_cast<float>(g)));
        EXPECT_EQ(back.grade, static_cast<float>(g));
        EXPECT_EQ(back.end, GradeEnd::within);
    }
}

TEST(PaperGrade, PastTheEndsIsSofterOrHarder) {
    const float soft = contrast_for_grade(0.0f);
    const float hard = contrast_for_grade(5.0f);
    EXPECT_EQ(grade_for_contrast(soft).end, GradeEnd::within);
    EXPECT_EQ(grade_for_contrast(hard).end, GradeEnd::within);
    EXPECT_EQ(grade_for_contrast(std::nextafter(soft, 0.0f)).end, GradeEnd::softer);
    EXPECT_EQ(grade_for_contrast(std::nextafter(hard, 10.0f)).end, GradeEnd::harder);
    EXPECT_EQ(grade_for_contrast(0.1f).grade, 0.0f);
    EXPECT_EQ(grade_for_contrast(10.0f).grade, 5.0f);
    EXPECT_EQ(grade_for_contrast(0.0f).end, GradeEnd::softer);
    EXPECT_EQ(grade_for_contrast(std::numeric_limits<float>::quiet_NaN()).end, GradeEnd::softer);
}

TEST(PaperGrade, GradesOutsideZeroToFiveAreClamped) {
    EXPECT_EQ(contrast_for_grade(-3.0f), contrast_for_grade(0.0f));
    EXPECT_EQ(contrast_for_grade(9.0f), contrast_for_grade(5.0f));
    EXPECT_EQ(contrast_for_grade(std::numeric_limits<float>::infinity()), contrast_for_grade(5.0f));
    EXPECT_EQ(contrast_for_grade(std::numeric_limits<float>::quiet_NaN()), 1.0f);
}

TEST(PaperGrade, TheSliderSnapsToHalfGrades) {
    EXPECT_EQ(prism::gui::grade_slider_position(1.0f), 4.0f);
    EXPECT_EQ(prism::gui::grade_slider_position(contrast_for_grade(3.5f)), 7.0f);
    EXPECT_EQ(prism::gui::grade_slider_position(contrast_for_grade(2.7f)), 5.0f);  // 2.5 is nearest
    EXPECT_EQ(prism::gui::grade_slider_position(0.3f), 0.0f);
    EXPECT_EQ(prism::gui::grade_slider_position(7.0f), 10.0f);
    EXPECT_EQ(prism::gui::contrast_for_grade_position(4.0f), 1.0f);
    EXPECT_EQ(prism::gui::contrast_for_grade_position(7.0f), contrast_for_grade(3.5f));
    for (int p = 0; p <= 10; ++p) {
        EXPECT_EQ(prism::gui::grade_slider_position(
                      prism::gui::contrast_for_grade_position(static_cast<float>(p))),
                  static_cast<float>(p));
    }
}

TEST(PaperGrade, TheBuiltInLooksFallInsideTheGrades) {
    for (const float contrast : {0.85f, 0.9f, 1.0f, 1.1f, 1.15f, 1.2f}) {
        const prism::gui::GradeReading reading = grade_for_contrast(contrast);
        EXPECT_EQ(reading.end, GradeEnd::within) << contrast;
        const double expected = grade_by_formula(static_cast<double>(contrast));
        EXPECT_GE(expected, 0.0);
        EXPECT_LE(expected, 5.0);
        EXPECT_NEAR(reading.grade, expected, 1e-5) << contrast;

        // The text is built from the formula's grade, one decimal unless it is a whole or half.
        const double twice = expected * 2;
        std::string grade;
        char buffer[32];
        if (std::fabs(twice - std::round(twice)) <= 0.01) {
            const long halves = std::lround(twice);
            grade = std::to_string(halves / 2) + (halves % 2 != 0 ? "½" : "");
        } else {
            std::snprintf(buffer, sizeof buffer, "%.1f", expected);
            grade = buffer;
        }
        EXPECT_EQ(format_paper_grade(contrast),
                  "Grade " + grade + " · " + multiplier_text(contrast))
            << contrast;
    }
    EXPECT_EQ(format_paper_grade(1.0f), "Grade 2 · ×1.00");
}

TEST(PaperGrade, TheValueLine) {
    EXPECT_EQ(format_paper_grade(contrast_for_grade(3.0f)), "Grade 3 · ×1.29");
    EXPECT_EQ(format_paper_grade(contrast_for_grade(3.5f)), "Grade 3½ · ×1.39");
    EXPECT_EQ(format_paper_grade(contrast_for_grade(0.5f)), "Grade 0½ · ×0.75");
    EXPECT_EQ(format_paper_grade(contrast_for_grade(2.7f)), "Grade 2.7 · ×1.19");
    EXPECT_EQ(format_paper_grade(0.6f), "Softer than grade 0 · ×0.60");
    EXPECT_EQ(format_paper_grade(2.0f), "Harder than grade 5 · ×2.00");
}

TEST(PaperGrade, NearWholeAndHalfGradesReadAsThem) {
    EXPECT_EQ(prism::gui::grade_number_text(3.004f), "3");
    EXPECT_EQ(prism::gui::grade_number_text(2.996f), "3");
    EXPECT_EQ(prism::gui::grade_number_text(3.496f), "3½");
    EXPECT_EQ(prism::gui::grade_number_text(3.02f), "3.0");
    EXPECT_EQ(prism::gui::grade_number_text(3.3f), "3.3");
}
