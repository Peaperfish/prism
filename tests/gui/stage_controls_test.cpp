#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#include "prism/gui/stage_controls.hpp"

using prism::gui::CurveKind;
using prism::gui::EditSettings;
using prism::gui::SimpleControl;
using prism::gui::StageId;

namespace {

constexpr SimpleControl all_controls[] = {SimpleControl::brightness, SimpleControl::contrast,
                                          SimpleControl::colour};

float position_of(const EditSettings& s, SimpleControl control) {
    const prism::gui::SimpleControls c = prism::gui::simple_from(s);
    switch (control) {
        case SimpleControl::brightness: return c.brightness;
        case SimpleControl::contrast: return c.contrast;
        case SimpleControl::colour: return c.colour;
    }
    return 0.0f;
}

}  // namespace

// --- Which slider belongs to which stage ------------------------------------------------------

TEST(StageSlider, ThreeStagesHaveOneAndTheMappingRoundTrips) {
    EXPECT_EQ(prism::gui::stage_slider(StageId::exposure), SimpleControl::brightness);
    EXPECT_EQ(prism::gui::stage_slider(StageId::saturation), SimpleControl::colour);
    EXPECT_EQ(prism::gui::stage_slider(StageId::contrast), SimpleControl::contrast);
    for (const StageId id : {StageId::decode, StageId::matrix, StageId::curve, StageId::encode}) {
        EXPECT_FALSE(prism::gui::stage_slider(id).has_value());
    }
    for (const SimpleControl c : all_controls) {
        EXPECT_EQ(prism::gui::stage_slider(prism::gui::stage_of(c)), c);
    }
}

// --- Slider <-> value ------------------------------------------------------------------------

TEST(StageSlider, EndsAndMiddleGiveTheDocumentedValues) {
    EditSettings s;
    prism::gui::apply_simple_control(s, SimpleControl::brightness, 100);
    EXPECT_EQ(s.exposure_stops, 2.0f);
    prism::gui::apply_simple_control(s, SimpleControl::brightness, -100);
    EXPECT_EQ(s.exposure_stops, -2.0f);
    prism::gui::apply_simple_control(s, SimpleControl::brightness, 0);
    EXPECT_EQ(s.exposure_stops, 0.0f);

    prism::gui::apply_simple_control(s, SimpleControl::colour, 100);
    EXPECT_EQ(s.saturation, 2.0f);
    prism::gui::apply_simple_control(s, SimpleControl::colour, -100);
    EXPECT_EQ(s.saturation, 0.0f);
    prism::gui::apply_simple_control(s, SimpleControl::colour, 0);
    EXPECT_EQ(s.saturation, 1.0f);

    prism::gui::apply_simple_control(s, SimpleControl::contrast, 100);
    EXPECT_FLOAT_EQ(s.contrast, 1.5f);
    prism::gui::apply_simple_control(s, SimpleControl::contrast, -100);
    EXPECT_FLOAT_EQ(s.contrast, 1.0f / 1.5f);
    prism::gui::apply_simple_control(s, SimpleControl::contrast, 0);
    EXPECT_EQ(s.contrast, 1.0f);
}

TEST(StageSlider, SliderToValueToSliderIsTheSameAtBothEndsAndZero) {
    for (const SimpleControl control : all_controls) {
        for (const float position : {-100.0f, 0.0f, 100.0f}) {
            EditSettings s;
            prism::gui::apply_simple_control(s, control, position);
            EXPECT_NEAR(position_of(s, control), position, 1e-3f)
                << "control " << static_cast<int>(control) << " at " << position;
            EXPECT_FALSE(prism::gui::outside_simple_range(s).brightness);
            EXPECT_FALSE(prism::gui::outside_simple_range(s).contrast);
            EXPECT_FALSE(prism::gui::outside_simple_range(s).colour);
        }
    }
}

TEST(StageSlider, MovingOneSliderLeavesTheOtherFieldsExactlyAsTheyWere) {
    EditSettings s;
    s.saturation = 3.0f;  // beyond Colour's end
    s.contrast = 0.2f;
    s.exposure_on = false;
    prism::gui::apply_simple_control(s, SimpleControl::brightness, 25);
    EXPECT_EQ(s.saturation, 3.0f);
    EXPECT_EQ(s.contrast, 0.2f);
    EXPECT_EQ(s.exposure_stops, 0.5f);
    EXPECT_TRUE(s.exposure_on);
}

TEST(StageSlider, StageAndSimpleSlidersAgreeBecauseTheyAreOneFunction) {
    // The Simple slider and the stage slider both call apply_simple_control and simple_from, so
    // the same position gives the same settings whichever was moved (existing Simple behaviour).
    EditSettings a;
    EditSettings b;
    prism::gui::apply_simple(a, {30, -40, 70});
    for (const StageId id : {StageId::exposure, StageId::contrast, StageId::saturation}) {
        const SimpleControl c = *prism::gui::stage_slider(id);
        const float v = c == SimpleControl::brightness ? 30.0f
                        : c == SimpleControl::contrast ? -40.0f
                                                       : 70.0f;
        prism::gui::apply_simple_control(b, c, v);
    }
    EXPECT_EQ(a, b);
}

// --- Formatting ------------------------------------------------------------------------------

TEST(StageFormat, Exposure) {
    EXPECT_EQ(prism::gui::format_exposure(0.7f), "+0.70 stops");
    EXPECT_EQ(prism::gui::format_exposure(-1.0f), "-1.00 stops");
    EXPECT_EQ(prism::gui::format_exposure(0.0f), "+0.00 stops");
    EXPECT_EQ(prism::gui::format_exposure(-0.0f), "+0.00 stops");
    EXPECT_EQ(prism::gui::format_exposure(-0.001f), "+0.00 stops");  // never "-0.00"
    EXPECT_EQ(prism::gui::format_exposure(2.0f), "+2.00 stops");
    EXPECT_EQ(prism::gui::format_exposure(-2.0f), "-2.00 stops");
    EXPECT_EQ(prism::gui::format_exposure(12.5f), "+12.50 stops");  // from outside the slider
}

TEST(StageFormat, MultipliersAndBlackAndWhite) {
    EXPECT_EQ(prism::gui::format_multiplier(1.2f), "x1.20");
    EXPECT_EQ(prism::gui::format_multiplier(1.0f), "x1.00");
    EXPECT_EQ(prism::gui::format_multiplier(1.0f / 1.5f), "x0.67");
    EXPECT_EQ(prism::gui::format_multiplier(1.5f), "x1.50");
    EXPECT_EQ(prism::gui::format_saturation(1.2f), "x1.20");
    EXPECT_EQ(prism::gui::format_saturation(2.0f), "x2.00");
    EXPECT_EQ(prism::gui::format_saturation(0.0f), "black and white");
    EXPECT_EQ(prism::gui::format_saturation(0.004f), "x0.00");  // only exactly 0 is B&W
}

TEST(StageFormat, RowTextForEveryStage) {
    EditSettings s;
    s.exposure_stops = 0.7f;
    s.saturation = 1.2f;
    s.contrast = 1.2f;
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::exposure), "+0.70 stops");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::saturation), "x1.20");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::contrast), "x1.20");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::matrix), "none");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::curve), "none");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::decode), "");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::encode), "");

    s.matrix = prism::gui::MatrixFile{std::filesystem::path("grades") / "teal.txt", {}};
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::matrix), "teal.txt");

    s.curve = CurveKind::gamma;
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::curve), "gamma 2.2");
    s.curve = CurveKind::reinhard;
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::curve), "Reinhard, white 4");
    s.curve = CurveKind::filmic;
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::curve), "filmic");

    s.exposure_on = false;  // the stored value is still shown; the row greys it out
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::exposure), "+0.70 stops");
}

// --- Tone curve ------------------------------------------------------------------------------

TEST(StageCurve, NamesAndParameters) {
    EXPECT_EQ(prism::gui::curve_kind_name(CurveKind::none), "None");
    EXPECT_EQ(prism::gui::curve_kind_name(CurveKind::gamma), "Gamma");
    EXPECT_EQ(prism::gui::curve_kind_name(CurveKind::reinhard), "Reinhard");
    EXPECT_EQ(prism::gui::curve_kind_name(CurveKind::filmic), "Filmic");
    EXPECT_FALSE(prism::gui::curve_parameter(CurveKind::none));
    EXPECT_FALSE(prism::gui::curve_parameter(CurveKind::filmic));
    const auto gamma = prism::gui::curve_parameter(CurveKind::gamma);
    ASSERT_TRUE(gamma);
    EXPECT_EQ(gamma->default_value, EditSettings{}.gamma);
    const auto white = prism::gui::curve_parameter(CurveKind::reinhard);
    ASSERT_TRUE(white);
    EXPECT_EQ(white->default_value, EditSettings{}.reinhard_white);
}

TEST(StageCurve, EveryValueInTheRangesIsValidAndPresetsFitIn) {
    for (const CurveKind kind : {CurveKind::gamma, CurveKind::reinhard}) {
        const auto p = prism::gui::curve_parameter(kind);
        ASSERT_TRUE(p);
        EXPECT_LT(p->min, p->default_value);
        EXPECT_LT(p->default_value, p->max);
        for (const float v : {p->min, p->default_value, p->max}) {
            EditSettings s;
            prism::gui::set_curve_kind(s, kind);
            prism::gui::set_curve_parameter(s, v);
            EXPECT_TRUE(prism::gui::validate(s).empty());
            EXPECT_EQ(prism::gui::curve_parameter_value(s), v);
        }
    }
}

TEST(StageCurve, SettingTheParameterClampsAndOnlyTouchesTheCurrentCurve) {
    EditSettings s;
    prism::gui::set_curve_kind(s, CurveKind::gamma);
    prism::gui::set_curve_parameter(s, 100.0f);
    EXPECT_EQ(s.gamma, 4.0f);
    prism::gui::set_curve_parameter(s, -3.0f);
    EXPECT_EQ(s.gamma, 0.5f);
    prism::gui::set_curve_parameter(s, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(s.gamma, 2.2f);
    EXPECT_EQ(s.reinhard_white, 4.0f);

    prism::gui::set_curve_kind(s, CurveKind::filmic);
    const EditSettings before = s;
    prism::gui::set_curve_parameter(s, 3.0f);  // filmic has none
    EXPECT_EQ(s, before);
    EXPECT_EQ(prism::gui::curve_parameter_value(s), 0.0f);
}

TEST(StageCurve, ChoosingACurveKeepsBothParametersAndTurnsTheStageOn) {
    EditSettings s;
    s.curve_on = false;
    s.gamma = 1.8f;
    s.reinhard_white = 8.0f;
    prism::gui::set_curve_kind(s, CurveKind::reinhard);
    EXPECT_TRUE(s.curve_on);
    prism::gui::set_curve_kind(s, CurveKind::gamma);
    EXPECT_EQ(s.gamma, 1.8f);
    EXPECT_EQ(s.reinhard_white, 8.0f);
    EXPECT_EQ(prism::gui::curve_parameter_value(s), 1.8f);
}

TEST(StageCurve, ReinhardWhiteStartsAtOneWhereTheCurveIsTheIdentity) {
    const auto white = prism::gui::curve_parameter(CurveKind::reinhard);
    ASSERT_TRUE(white);
    EXPECT_EQ(white->min, 1.0f);
    EditSettings s;
    prism::gui::set_curve_kind(s, CurveKind::reinhard);
    prism::gui::set_curve_parameter(s, 0.5f);
    EXPECT_EQ(s.reinhard_white, 1.0f);
    s.reinhard_white = 0.7f;  // stored below the range: shown at its end
    EXPECT_EQ(prism::gui::curve_parameter_value(s), 1.0f);
}

TEST(StageCurve, ReinhardSliderRoundTripsItsValues) {
    const auto white = prism::gui::curve_parameter(CurveKind::reinhard);
    ASSERT_TRUE(white);
    ASSERT_TRUE(white->logarithmic);
    for (const float v : {1.0f, 1.01f, 1.5f, 2.0f, 3.3f, 4.0f, 7.25f, 8.0f, 15.9f, 16.0f}) {
        const float position = prism::gui::curve_slider_position(*white, v);
        EXPECT_GE(position, 0.0f);
        EXPECT_LE(position, 1.0f);
        EXPECT_NEAR(prism::gui::curve_value_at(*white, position), v, v * 1e-5f) << v;
    }
    // The ends are exact, and equal ratios take equal steps.
    EXPECT_EQ(prism::gui::curve_value_at(*white, 0.0f), 1.0f);
    EXPECT_EQ(prism::gui::curve_value_at(*white, 1.0f), 16.0f);
    EXPECT_NEAR(prism::gui::curve_slider_position(*white, 2.0f), 0.25f, 1e-6f);
    EXPECT_NEAR(prism::gui::curve_slider_position(*white, 4.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(prism::gui::curve_slider_position(*white, 8.0f), 0.75f, 1e-6f);
    // Outside the range, and NaN, land where the slider shows them.
    EXPECT_EQ(prism::gui::curve_slider_position(*white, 0.5f), 0.0f);
    EXPECT_EQ(prism::gui::curve_slider_position(*white, 100.0f), 1.0f);
    EXPECT_NEAR(prism::gui::curve_slider_position(
                    *white, std::numeric_limits<float>::quiet_NaN()), 0.5f, 1e-6f);
}

TEST(StageCurve, GammaSliderIsLinearAndRoundTrips) {
    const auto gamma = prism::gui::curve_parameter(CurveKind::gamma);
    ASSERT_TRUE(gamma);
    EXPECT_NEAR(prism::gui::curve_slider_position(*gamma, 2.25f), 0.5f, 1e-6f);
    for (const float v : {0.5f, 1.0f, 1.8f, 2.2f, 4.0f}) {
        EXPECT_NEAR(prism::gui::curve_value_at(
                        *gamma, prism::gui::curve_slider_position(*gamma, v)), v, 1e-5f) << v;
    }
}

// --- Viewing vs. editing ---------------------------------------------------------------------

TEST(StageHint, OnlyLaterNonNeutralStagesThatAreOnAreHidden) {
    EditSettings s;
    for (const auto& info : prism::gui::pipeline_stages()) {
        EXPECT_FALSE(prism::gui::hidden_by_view(s, info.id, StageId::decode)) << info.slug;
    }
    s.saturation = 1.3f;
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::exposure));
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::matrix));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::saturation));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::contrast));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::encode));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::contrast, StageId::exposure));  // neutral

    s.saturation_on = false;
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::exposure));
}

TEST(StageHint, CoversExposureMatrixContrastAndCurveToo) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.contrast = 1.2f;
    s.curve = CurveKind::filmic;
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::exposure, StageId::decode));
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::contrast, StageId::saturation));
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::curve, StageId::contrast));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::curve, StageId::curve));

    s.matrix = prism::gui::MatrixFile{"identity.txt", prism::identity_matrix};
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::matrix, StageId::exposure));
    s.matrix->values[0] = 0.5f;
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::matrix, StageId::exposure));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::decode, StageId::decode));
}

TEST(StageHint, Text) {
    EXPECT_EQ(prism::gui::view_hint(StageId::exposure),
              "Not visible here: you're viewing after Exposure.");
    EXPECT_EQ(prism::gui::view_hint(StageId::matrix),
              "Not visible here: you're viewing after Colour matrix.");
}

TEST(StageHint, WhileComparingTheFurthestStageShownDecides) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.saturation = 1.3f;
    s.contrast = 1.2f;
    // Viewing Exposure, comparing with Output encode: everything is shown, nothing is hidden.
    for (const auto& info : prism::gui::pipeline_stages()) {
        EXPECT_FALSE(prism::gui::hidden_by_view(s, info.id, StageId::exposure, StageId::encode));
    }
    // Compare with Saturation: Contrast is after both pictures, Saturation is not.
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::exposure,
                                            StageId::saturation));
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::contrast, StageId::exposure,
                                           StageId::saturation));
    // A compare stage before the viewed one changes nothing: the viewed stage is the furthest.
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::contrast, StageId::saturation,
                                           StageId::decode));
    EXPECT_FALSE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::saturation,
                                            StageId::decode));
    // The viewed stage as compare, and Off, are the old rule.
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::exposure,
                                           StageId::exposure));
    EXPECT_TRUE(prism::gui::hidden_by_view(s, StageId::saturation, StageId::exposure));
    EXPECT_EQ(prism::gui::furthest_shown(StageId::exposure, StageId::curve), StageId::curve);
    EXPECT_EQ(prism::gui::furthest_shown(StageId::curve, StageId::exposure), StageId::curve);
    EXPECT_EQ(prism::gui::furthest_shown(StageId::matrix), StageId::matrix);
}

TEST(StageHint, TheViewedStageAsCompareIsOff) {
    EXPECT_EQ(prism::gui::effective_compare(StageId::matrix, StageId::matrix), std::nullopt);
    EXPECT_EQ(prism::gui::effective_compare(StageId::matrix, std::nullopt), std::nullopt);
    EXPECT_EQ(prism::gui::effective_compare(StageId::matrix, StageId::decode), StageId::decode);
    EXPECT_EQ(prism::gui::view_hint(StageId::exposure, StageId::exposure),
              prism::gui::view_hint(StageId::exposure));
    EXPECT_EQ(prism::gui::view_hint(StageId::exposure, StageId::contrast),
              "Not visible here: the furthest stage shown is after Contrast.");
    EXPECT_EQ(prism::gui::view_hint(StageId::contrast, StageId::exposure),
              "Not visible here: the furthest stage shown is after Contrast.");
}

TEST(StageHint, CaptionsNameEachPicture) {
    EXPECT_EQ(prism::gui::stage_caption(StageId::decode), "Original");
    EXPECT_EQ(prism::gui::stage_caption(StageId::exposure), "after Exposure");
    EXPECT_EQ(prism::gui::stage_caption(StageId::matrix), "after Colour matrix");
    EXPECT_EQ(prism::gui::stage_caption(StageId::encode), "after Output encode");
}

namespace {

using prism::gui::EscapeAction;
using prism::gui::EscapeContext;
using prism::gui::escape_action;

TEST(Escape, FirstClearsTheComparisonThenReturnsToTheFinalPicture) {
    EscapeContext c;
    c.comparing = true;
    c.viewing_earlier = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
    c.comparing = false;
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
    c.viewing_earlier = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

TEST(Escape, ComparingAgainstTheFinalPictureStillClears) {
    EscapeContext c;
    c.comparing = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
}

TEST(Escape, ClosesAPopupOrEndsATextEditOrDragFirst) {
    for (int which = 0; which < 3; ++which) {
        EscapeContext c;
        c.comparing = true;
        c.viewing_earlier = true;
        c.text_editing = which == 0;
        c.popup_open = which == 1;
        c.item_active = which == 2;
        EXPECT_EQ(escape_action(c), EscapeAction::none) << which;
    }
}

}  // namespace
