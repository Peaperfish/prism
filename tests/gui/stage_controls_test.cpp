#include <gtest/gtest.h>

#include <cctype>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#include "prism/gui/paper_grade.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/ranges.hpp"
#include "prism/white_balance.hpp"

using prism::gui::CurveKind;
using prism::gui::EditSettings;
using prism::gui::SimpleControl;
using prism::gui::StageId;

namespace {

constexpr SimpleControl all_controls[] = {SimpleControl::brightness, SimpleControl::contrast,
                                          SimpleControl::colour};

// The controls with a position on the -100 to +100 scale; Contrast's slider is in paper grades.
constexpr SimpleControl scale_controls[] = {SimpleControl::brightness, SimpleControl::colour};

float position_of(const EditSettings& s, SimpleControl control) {
    const prism::gui::SimpleControls c = prism::gui::simple_from(s);
    return control == SimpleControl::brightness ? c.brightness : c.colour;
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

}

TEST(StageSlider, SliderToValueToSliderIsTheSameAtBothEndsAndZero) {
    for (const SimpleControl control : scale_controls) {
        for (const float position : {-100.0f, 0.0f, 100.0f}) {
            EditSettings s;
            prism::gui::apply_simple_control(s, control, position);
            EXPECT_NEAR(position_of(s, control), position, 1e-3f)
                << "control " << static_cast<int>(control) << " at " << position;
            EXPECT_FALSE(prism::gui::outside_simple_range(s).brightness);
            EXPECT_FALSE(prism::gui::outside_simple_range(s).colour);
        }
    }
}

TEST(StageSlider, ContrastsResetGivesExactlyOneAndTurnsOnOnlyItsStage) {
    EditSettings s;
    s.contrast = prism::gui::contrast_for_grade(3.5f);
    s.contrast_on = false;
    s.exposure_stops = 1.0f;
    s.exposure_on = false;
    EditSettings expected = s;
    prism::gui::reset_simple_control(s, SimpleControl::contrast);
    expected.contrast = 1.0f;
    expected.contrast_on = true;
    EXPECT_EQ(s.contrast, 1.0f);
    EXPECT_EQ(s, expected);
}

TEST(StageSlider, ContrastHasNoPositionOnTheOldScale) {
    EditSettings s;
    s.contrast = 2.0f;
    s.contrast_on = false;
    const EditSettings before = s;
    prism::gui::apply_simple_control(s, SimpleControl::contrast, 50);
    EXPECT_EQ(s, before);
    EXPECT_EQ(prism::gui::simple_position_for(SimpleControl::contrast, 1.5f), 0.0f);
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
    prism::gui::apply_simple(a, {30, 70});
    for (const StageId id : {StageId::exposure, StageId::saturation}) {
        const SimpleControl c = *prism::gui::stage_slider(id);
        prism::gui::apply_simple_control(b, c, c == SimpleControl::brightness ? 30.0f : 70.0f);
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
    EXPECT_EQ(prism::gui::format_multiplier(1.2f), "×1.20");
    EXPECT_EQ(prism::gui::format_multiplier(1.0f), "×1.00");
    EXPECT_EQ(prism::gui::format_multiplier(1.0f / 1.5f), "×0.67");
    EXPECT_EQ(prism::gui::format_multiplier(1.5f), "×1.50");
    EXPECT_EQ(prism::gui::format_saturation(1.2f), "×1.20");
    EXPECT_EQ(prism::gui::format_saturation(2.0f), "×2.00");
    EXPECT_EQ(prism::gui::format_saturation(0.0f), "black and white");
    EXPECT_EQ(prism::gui::format_saturation(0.004f), "×0.00");  // only exactly 0 is B&W
}

// A multiplier is written with the multiplication sign, never the letter x.
TEST(StageFormat, MultipliersUseTheTimesSignInEveryOutput) {
    const auto has_ascii_x_times = [](const std::string& text) {
        for (std::size_t i = 0; i + 1 < text.size(); ++i) {
            if (text[i] == 'x' && std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
                return true;
            }
        }
        return false;
    };
    for (const float v : {0.0f, 0.004f, 0.5f, 1.0f, 1.2f, 2.0f, 10.0f}) {
        EditSettings s;
        s.saturation = v;
        s.contrast = v == 0.0f ? 0.1f : v;
        s.exposure_stops = v - 1;
        for (const StageId stage : {StageId::decode, StageId::exposure, StageId::white_balance,
                                    StageId::matrix, StageId::saturation, StageId::contrast,
                                    StageId::curve, StageId::encode}) {
            EXPECT_FALSE(has_ascii_x_times(prism::gui::stage_value_text(s, stage))) << v;
        }
        EXPECT_FALSE(has_ascii_x_times(prism::gui::format_multiplier(v)));
        EXPECT_FALSE(has_ascii_x_times(prism::gui::format_saturation(v)));
    }
    EXPECT_NE(prism::gui::stage_value_text(EditSettings{}, StageId::contrast).find("\u00D7"),
              std::string::npos);
}

TEST(StageFormat, RowTextForEveryStage) {
    EditSettings s;
    s.exposure_stops = 0.7f;
    s.saturation = 1.2f;
    s.contrast = 1.2f;
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::exposure), "+0.70 stops");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::saturation), "×1.20");
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::contrast), "Grade 2.7 · ×1.20");
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

TEST(StageHint, OnlyStagesAfterTheViewedOneAreAfterTheView) {
    const auto& stages = prism::gui::pipeline_stages();
    for (const auto& viewed : stages) {
        for (const auto& info : stages) {
            EXPECT_EQ(prism::gui::stage_after_view(info.id, viewed.id),
                      static_cast<int>(info.id) > static_cast<int>(viewed.id))
                << info.slug << " viewing " << viewed.slug;
        }
    }
}

TEST(StageHint, Text) {
    EXPECT_EQ(prism::gui::view_hint(StageId::exposure),
              "Not visible here: you're viewing after Exposure.");
    EXPECT_EQ(prism::gui::view_hint(StageId::matrix),
              "Not visible here: you're viewing after Colour matrix.");
}

TEST(StageHint, WhileComparingTheFurthestStageShownDecides) {
    // Viewing Exposure, comparing with Output encode: everything is shown.
    for (const auto& info : prism::gui::pipeline_stages()) {
        EXPECT_FALSE(prism::gui::stage_after_view(info.id, StageId::exposure, StageId::encode));
    }
    // Compare with Saturation: Contrast is after both pictures, Saturation is not.
    EXPECT_FALSE(prism::gui::stage_after_view(StageId::saturation, StageId::exposure,
                                              StageId::saturation));
    EXPECT_TRUE(prism::gui::stage_after_view(StageId::contrast, StageId::exposure,
                                             StageId::saturation));
    // A compare stage before the viewed one changes nothing: the viewed stage is the furthest.
    EXPECT_TRUE(prism::gui::stage_after_view(StageId::contrast, StageId::saturation,
                                             StageId::decode));
    EXPECT_FALSE(prism::gui::stage_after_view(StageId::saturation, StageId::saturation,
                                              StageId::decode));
    // The viewed stage as compare, and Off, are the old rule.
    EXPECT_TRUE(prism::gui::stage_after_view(StageId::saturation, StageId::exposure,
                                             StageId::exposure));
    EXPECT_TRUE(prism::gui::stage_after_view(StageId::saturation, StageId::exposure));
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

TEST(Escape, PickModeGoesBeforeSampleModeTheStripTheComparisonAndTheStage) {
    EscapeContext c;
    c.picking = true;
    c.sampling = true;  // cannot happen together, but the order still holds
    c.test_strip = true;
    c.comparing = true;
    c.viewing_earlier = true;
    EXPECT_EQ(escape_action(c), EscapeAction::leave_pick);
    c.picking = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_sample);
    c.sampling = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
    c.comparing = false;
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
}

TEST(Escape, PickModeYieldsToPopupsTextEditsDragsAndFocusedWindows) {
    for (int which = 0; which < 4; ++which) {
        EscapeContext c;
        c.picking = true;
        c.text_editing = which == 0;
        c.popup_open = which == 1;
        c.item_active = which == 2;
        c.window_focused = which == 3;
        EXPECT_NE(escape_action(c), EscapeAction::leave_pick) << which;
    }
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

TEST(Escape, AFocusedWindowTakesTheEscAndLeavesThePictureAlone) {
    EscapeContext c;
    c.comparing = true;
    c.viewing_earlier = true;
    c.window_focused = true;
    EXPECT_EQ(escape_action(c), EscapeAction::close_window);
    c.window_focused = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
}

TEST(Escape, AWindowWithNothingToBackOutOfStillCloses) {
    EscapeContext c;
    c.window_focused = true;
    EXPECT_EQ(escape_action(c), EscapeAction::close_window);
}

TEST(Escape, APopupOrATextEditOrDragInTheWindowIsClosedBeforeTheWindow) {
    for (int which = 0; which < 3; ++which) {
        EscapeContext c;
        c.window_focused = true;
        c.text_editing = which == 0;
        c.popup_open = which == 1;  // a combo list in Preferences, and the Saving modal
        c.item_active = which == 2;
        EXPECT_EQ(escape_action(c), EscapeAction::none) << which;
    }
}

}  // namespace

TEST(Escape, TheTestStripGoesBeforeTheComparisonAndTheViewedStage) {
    EscapeContext c;
    c.test_strip = true;
    c.viewing_earlier = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;  // the app turned it off
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
    c.test_strip = true;
    c.comparing = true;  // exclusive in the app, but the order holds regardless
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
}

TEST(Escape, TheTestStripOnTheFinalPictureStillClears) {
    EscapeContext c;
    c.test_strip = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

TEST(Escape, ImGuiOwnedThingsAndFocusedWindowsComeBeforeTheStrip) {
    for (int which = 0; which < 4; ++which) {
        EscapeContext c;
        c.test_strip = true;
        c.comparing = true;
        c.viewing_earlier = true;
        c.text_editing = which == 0;
        c.popup_open = which == 1;
        c.item_active = which == 2;
        c.window_focused = which == 3;
        EXPECT_EQ(escape_action(c), which == 3 ? EscapeAction::close_window : EscapeAction::none)
            << which;
    }
    EscapeContext c;  // a text edit inside a focused window: the edit first
    c.test_strip = true;
    c.window_focused = true;
    c.text_editing = true;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

// ImGui-owned (text, popup, drag), then the focused window (Preferences, About, notices, Recipe not
// loaded, View recipe), then the test strip, the comparison and the viewed stage.
TEST(Escape, TheWholeOrderOnePressAtATime) {
    EscapeContext c;
    c.text_editing = c.popup_open = c.item_active = true;
    c.window_focused = c.test_strip = c.comparing = c.viewing_earlier = true;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
    c.text_editing = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
    c.popup_open = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
    c.item_active = false;  // e.g. the View recipe text let go of: the window is next
    EXPECT_EQ(escape_action(c), EscapeAction::close_window);
    c.window_focused = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
    c.comparing = false;
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
    c.viewing_earlier = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

TEST(Escape, FullSequenceFromStripToFinal) {
    EscapeContext c;
    c.test_strip = true;
    c.viewing_earlier = true;
    c.window_focused = true;
    EXPECT_EQ(escape_action(c), EscapeAction::close_window);
    c.window_focused = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
    c.viewing_earlier = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

// Sample mode comes after what ImGui owns and the focused window, and before everything else.
TEST(Escape, SampleModeIsLeftBeforeTheStripTheComparisonAndTheViewedStage) {
    EscapeContext c;
    c.sampling = c.test_strip = c.comparing = c.viewing_earlier = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_sample);
    c.sampling = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_test_strip);
    c.test_strip = false;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_compare);
    c.comparing = false;
    EXPECT_EQ(escape_action(c), EscapeAction::show_final);
    c.viewing_earlier = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

TEST(Escape, AFocusedWindowAndWhatImGuiOwnsComeBeforeSampleMode) {
    for (int which = 0; which < 4; ++which) {
        EscapeContext c;
        c.sampling = true;
        c.text_editing = which == 0;
        c.popup_open = which == 1;
        c.item_active = which == 2;
        c.window_focused = which == 3;
        EXPECT_EQ(escape_action(c), which == 3 ? EscapeAction::close_window : EscapeAction::none)
            << which;
    }
}

TEST(Escape, SampleModeAloneIsOnePress) {
    EscapeContext c;
    c.sampling = true;
    EXPECT_EQ(escape_action(c), EscapeAction::clear_sample);
    c.sampling = false;
    EXPECT_EQ(escape_action(c), EscapeAction::none);
}

// --- White balance -----------------------------------------------------------------------------

namespace {

using prism::WhiteBalance;
using prism::gui::WhiteBalanceControl;

// The as-shot white of a photo lit by `wb`.
std::optional<prism::Chromaticity> lit_by(WhiteBalance wb) {
    return prism::white_of(wb);
}

double mired(double kelvin) {
    return 1e6 / kelvin;
}

}  // namespace

TEST(WhiteBalanceSlider, TheEndsAreTheEndsOfTheKelvinRange) {
    EXPECT_EQ(prism::kelvin_range.min, 2000.0);
    EXPECT_EQ(prism::kelvin_range.max, 15000.0);
    EXPECT_EQ(prism::gui::kelvin_at_slider_position(0.0), 2000.0);
    EXPECT_EQ(prism::gui::kelvin_at_slider_position(1.0), 15000.0);
    EXPECT_EQ(prism::gui::kelvin_slider_position(2000.0), 0.0);
    EXPECT_EQ(prism::gui::kelvin_slider_position(15000.0), 1.0);
    // Past the ends, and no number at all.
    EXPECT_EQ(prism::gui::kelvin_at_slider_position(-0.5), 2000.0);
    EXPECT_EQ(prism::gui::kelvin_at_slider_position(1.5), 15000.0);
    EXPECT_EQ(prism::gui::kelvin_slider_position(500.0), 0.0);
    EXPECT_EQ(prism::gui::kelvin_slider_position(40000.0), 1.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(prism::gui::kelvin_at_slider_position(nan), 2000.0);
    EXPECT_EQ(prism::gui::kelvin_slider_position(nan), prism::gui::kelvin_slider_position(6500.0));
}

TEST(WhiteBalanceSlider, TheMiddleIsTheMiredMidpointNotTheKelvinOne) {
    const double middle_mired = (mired(2000.0) + mired(15000.0)) / 2.0;
    const double middle_kelvin = 1e6 / middle_mired;
    EXPECT_NEAR(middle_kelvin, 3529.4, 0.05);  // 60 / 17 thousand
    EXPECT_NEAR(prism::gui::kelvin_at_slider_position(0.5), middle_kelvin, 1e-6);
    EXPECT_NEAR(prism::gui::kelvin_slider_position(middle_kelvin), 0.5, 1e-12);
    // Linear in Kelvin it would be 8500 K, far to the right of this slider's middle.
    EXPECT_GT(prism::gui::kelvin_slider_position(8500.0), 0.85);
}

TEST(WhiteBalanceSlider, EqualStepsAlongItAreEqualStepsInMireds) {
    const double step = (mired(2000.0) - mired(15000.0)) / 20.0;
    for (int i = 0; i <= 20; ++i) {
        const double kelvin = prism::gui::kelvin_at_slider_position(i / 20.0);
        EXPECT_NEAR(mired(kelvin), mired(2000.0) - i * step, 1e-9) << i;
        if (i > 0) {
            EXPECT_GT(kelvin, prism::gui::kelvin_at_slider_position((i - 1) / 20.0));
        }
    }
}

TEST(WhiteBalanceSlider, PositionAndKelvinRoundTrip) {
    for (double kelvin = 2000.0; kelvin <= 15000.0; kelvin += 10.0) {
        const double position = prism::gui::kelvin_slider_position(kelvin);
        EXPECT_GE(position, 0.0);
        EXPECT_LE(position, 1.0);
        EXPECT_NEAR(prism::gui::kelvin_at_slider_position(position), kelvin, 1e-6) << kelvin;
        // Through the slider's own float, a drag still lands on the same 10 K.
        const float as_float = static_cast<float>(position);
        EXPECT_EQ(prism::gui::snap_kelvin(prism::gui::kelvin_at_slider_position(as_float)), kelvin)
            << kelvin;
    }
    for (int i = 0; i <= 1000; ++i) {
        const double position = i / 1000.0;
        EXPECT_NEAR(prism::gui::kelvin_slider_position(
                        prism::gui::kelvin_at_slider_position(position)),
                    position, 1e-12);
    }
}

TEST(WhiteBalanceSlider, SnapsKelvinToTenAndTintToWholeNumbersWithinTheirRanges) {
    EXPECT_EQ(prism::gui::snap_kelvin(5327.0), 5330.0);
    EXPECT_EQ(prism::gui::snap_kelvin(5324.9), 5320.0);
    EXPECT_EQ(prism::gui::snap_kelvin(1200.0), 2000.0);
    EXPECT_EQ(prism::gui::snap_kelvin(99999.0), 15000.0);
    EXPECT_EQ(prism::gui::snap_tint(26.6), 27.0);
    EXPECT_EQ(prism::gui::snap_tint(-26.6), -27.0);
    EXPECT_EQ(prism::gui::snap_tint(-140.0), -100.0);
    EXPECT_EQ(prism::gui::snap_tint(140.0), 100.0);
    EXPECT_FALSE(std::signbit(prism::gui::snap_tint(-0.2)));
}

TEST(WhiteBalanceFormat, KelvinToTenAndTintWithItsSignAndARealMinus) {
    EXPECT_EQ(prism::gui::format_kelvin(5327.0), "5330 K");
    EXPECT_EQ(prism::gui::format_kelvin(6500.0), "6500 K");
    EXPECT_EQ(prism::gui::format_kelvin(15000.0), "15000 K");
    EXPECT_EQ(prism::gui::format_tint(26.6), "+27");
    EXPECT_EQ(prism::gui::format_tint(10.0), "+10");
    EXPECT_EQ(prism::gui::format_tint(0.0), "0");
    EXPECT_EQ(prism::gui::format_tint(-0.3), "0");
    EXPECT_EQ(prism::gui::format_tint(-10.0), "\xE2\x88\x92" "10");  // U+2212, not a hyphen
    EXPECT_EQ(prism::gui::format_tint(-10.0).find('-'), std::string::npos);
}

TEST(WhiteBalanceValue, AsShotOnARawFileShowsTheCamerasLight) {
    const EditSettings s;
    EXPECT_EQ(prism::gui::white_balance_value_text(s, lit_by({5328, 26.7})),
              "As shot \xC2\xB7 5330 K, tint +27");
    EXPECT_EQ(prism::gui::white_balance_value_text(s, lit_by({4000, -12.3})),
              "As shot \xC2\xB7 4000 K, tint \xE2\x88\x92" "12");
    EXPECT_EQ(prism::gui::white_balance_value_text(s, lit_by({6500, 0})),
              "As shot \xC2\xB7 6500 K, tint 0");
}

TEST(WhiteBalanceValue, AsShotWithoutACamerasLightIsTakenAs6500) {
    EXPECT_EQ(prism::gui::white_balance_value_text(EditSettings{}, std::nullopt),
              "As shot \xC2\xB7 taken as 6500 K");
}

TEST(WhiteBalanceValue, AChosenLightShowsItsKelvinAndTint) {
    EditSettings s;
    s.white_balance = WhiteBalance{5200, 10};
    // The same with and without the photo's own light: the choice is what is shown.
    for (const auto& as_shot : {std::optional<prism::Chromaticity>{}, lit_by({5328, 26.7})}) {
        EXPECT_EQ(prism::gui::white_balance_value_text(s, as_shot), "5200 K \xC2\xB7 tint +10");
    }
    EXPECT_EQ(prism::gui::stage_value_text(s, StageId::white_balance), "5200 K \xC2\xB7 tint +10");
    s.white_balance = WhiteBalance{5200, 0};
    EXPECT_EQ(prism::gui::white_balance_value_text(s, std::nullopt), "5200 K \xC2\xB7 tint 0");
    s.white_balance = WhiteBalance{3200, -10};
    EXPECT_EQ(prism::gui::white_balance_value_text(s, std::nullopt),
              "3200 K \xC2\xB7 tint \xE2\x88\x92" "10");
    EXPECT_EQ(prism::gui::stage_value_text(EditSettings{}, StageId::white_balance), "As shot");
}

TEST(WhiteBalanceControls, AsShotTheSlidersSitAtTheAsShotLightClampedToTheirRanges) {
    const EditSettings s;
    EXPECT_EQ(prism::gui::slider_white_balance(s, lit_by({5328, 26.7})), (WhiteBalance{5330, 27}));
    EXPECT_EQ(prism::gui::slider_white_balance(s, std::nullopt), (WhiteBalance{6500, 0}));
    // A light the sliders cannot reach sits at their ends.
    const WhiteBalance hot = prism::gui::slider_white_balance(s, lit_by({1500, 0}));
    EXPECT_EQ(hot.kelvin, 2000.0);
    EXPECT_GE(hot.tint, -100.0);
    EXPECT_LE(hot.tint, 100.0);
    // The unrounded light is what the value line reads from.
    const WhiteBalance as_shot = prism::gui::as_shot_white_balance(lit_by({5328, 26.7}));
    EXPECT_NEAR(as_shot.kelvin, 5328.0, 1.0);
    EXPECT_NEAR(as_shot.tint, 26.7, 0.2);
    EXPECT_EQ(prism::gui::as_shot_white_balance(std::nullopt), prism::default_as_shot);
}

TEST(WhiteBalanceControls, MovingOneSliderTakesTheOtherNumberFromAsShot) {
    const auto as_shot = lit_by({5328, 26.7});
    EditSettings kelvin;
    prism::gui::set_white_balance(kelvin, WhiteBalanceControl::kelvin, 4204.0, as_shot);
    EXPECT_EQ(kelvin.white_balance, (WhiteBalance{4200, 27}));

    EditSettings tint;
    prism::gui::set_white_balance(tint, WhiteBalanceControl::tint, -3.4, as_shot);
    EXPECT_EQ(tint.white_balance, (WhiteBalance{5330, -3}));

    // Without a camera's light the other number is 6500 K, or tint 0.
    EditSettings plain;
    prism::gui::set_white_balance(plain, WhiteBalanceControl::tint, 12.0, std::nullopt);
    EXPECT_EQ(plain.white_balance, (WhiteBalance{6500, 12}));
    // Nothing but the white balance changed.
    plain.white_balance.reset();
    EXPECT_EQ(plain, EditSettings{});
}

TEST(WhiteBalanceControls, OnceChosenEachSliderChangesOnlyItsOwnNumber) {
    EditSettings s;
    s.white_balance = WhiteBalance{5234.5, 3.25};  // off the sliders' grid, as a recipe may be
    const auto as_shot = lit_by({3000, -40});
    prism::gui::set_white_balance(s, WhiteBalanceControl::tint, 15.0, as_shot);
    EXPECT_EQ(s.white_balance, (WhiteBalance{5234.5, 15}));
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 7000.0, as_shot);
    EXPECT_EQ(s.white_balance, (WhiteBalance{7000, 15}));
    // Values are clamped to the sliders' ranges.
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 90000.0, as_shot);
    prism::gui::set_white_balance(s, WhiteBalanceControl::tint, -500.0, as_shot);
    EXPECT_EQ(s.white_balance, (WhiteBalance{15000, -100}));
    EXPECT_TRUE(prism::gui::validate(s).empty());
}

TEST(WhiteBalanceControls, ASliderSetToWhereItSitsLeavesAnAsShotPhotoAsShot) {
    const auto as_shot = lit_by({5328, 26.7});
    EditSettings s;
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 5331.0, as_shot);
    prism::gui::set_white_balance(s, WhiteBalanceControl::tint, 27.2, as_shot);
    prism::gui::set_white_balance(s, WhiteBalanceControl::tint,
                                  std::numeric_limits<double>::quiet_NaN(), as_shot);
    EXPECT_EQ(s, EditSettings{});
    // Once chosen, the same numbers are a choice and stay one.
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 5400.0, as_shot);
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 5330.0, as_shot);
    EXPECT_EQ(s.white_balance, (WhiteBalance{5330, 27}));
}

TEST(WhiteBalanceControls, MovingASliderTurnsTheStageOnAndResetGoesBackToAsShotExactly) {
    EditSettings s;
    s.white_balance_on = false;
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, 4000.0, std::nullopt);
    EXPECT_TRUE(s.white_balance_on);
    ASSERT_TRUE(s.white_balance);

    s.white_balance_on = false;
    prism::gui::reset_white_balance(s);
    EXPECT_EQ(s.white_balance, std::nullopt);
    EXPECT_TRUE(s.white_balance_on);
    EXPECT_EQ(s, EditSettings{});
    EXPECT_FALSE(prism::gui::to_process_settings(s).white_balance);
}

TEST(WhiteBalanceControls, EveryChoiceTheSlidersCanMakeIsValid) {
    for (const double position : {0.0, 0.001, 0.37, 0.5, 0.999, 1.0}) {
        for (const double tint : {-100.0, -0.4, 0.0, 33.3, 100.0}) {
            EditSettings s;
            prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin,
                                          prism::gui::kelvin_at_slider_position(position),
                                          lit_by({9000, 60}));
            prism::gui::set_white_balance(s, WhiteBalanceControl::tint, tint, lit_by({9000, 60}));
            ASSERT_TRUE(s.white_balance);
            EXPECT_TRUE(prism::gui::validate(s).empty());
            EXPECT_EQ(std::fmod(s.white_balance->kelvin, 10.0), 0.0);
            EXPECT_EQ(s.white_balance->tint, std::round(s.white_balance->tint));
        }
    }
}
