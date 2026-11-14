#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

#include "prism/gui/paper_grade.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/typed_value.hpp"
#include "prism/white_balance.hpp"

namespace {

using prism::gui::EditSettings;
using prism::gui::SimpleControl;

constexpr std::string_view times = "\xC3\x97";  // ×

float parsed(std::string_view text, SimpleControl control = SimpleControl::colour) {
    const std::optional<float> v = prism::gui::typed_row_value(control, text);
    EXPECT_TRUE(v.has_value()) << text;
    return v.value_or(std::numeric_limits<float>::quiet_NaN());
}

// What the row's value line shows after `text` was typed into the row, through the same path the
// app takes: text -> slider position -> settings field -> value line. Contrast's slider is in grades,
// so its typed value goes straight to the multiplier, as the app does.
std::string line_after_typing(SimpleControl control, std::string_view text,
                              EditSettings s = {}) {
    if (control == SimpleControl::contrast) {
        const std::optional<float> value = prism::gui::typed_row_value(control, text);
        EXPECT_TRUE(value.has_value()) << text;
        s.contrast = value.value_or(1.0f);
        s.contrast_on = true;
        return prism::gui::stage_value_text(s, prism::gui::stage_of(control));
    }
    const std::optional<float> position = prism::gui::typed_slider_position(control, text);
    EXPECT_TRUE(position.has_value()) << text;
    prism::gui::apply_simple_control(s, control, position.value_or(0.0f));
    return prism::gui::stage_value_text(s, prism::gui::stage_of(control));
}

}  // namespace

TEST(TypedValue, ReadsPlainNumbersWithAnOptionalSign) {
    EXPECT_FLOAT_EQ(parsed("1.2"), 1.2f);
    EXPECT_FLOAT_EQ(parsed("+1.2"), 1.2f);
    EXPECT_FLOAT_EQ(parsed("-1.2"), -1.2f);
    EXPECT_FLOAT_EQ(parsed("7"), 7.0f);
    EXPECT_FLOAT_EQ(parsed(".5"), 0.5f);
    EXPECT_FLOAT_EQ(parsed("5."), 5.0f);
    EXPECT_FLOAT_EQ(parsed("0"), 0.0f);
    EXPECT_FLOAT_EQ(parsed("0.05"), 0.05f);
    EXPECT_FLOAT_EQ(parsed("000.250"), 0.25f);
    EXPECT_FLOAT_EQ(parsed("  1.25  "), 1.25f);
    EXPECT_FLOAT_EQ(parsed("\t1.25\n"), 1.25f);
}

TEST(TypedValue, ACommaIsADecimalMarkToo) {
    EXPECT_EQ(parsed("1,2"), parsed("1.2"));
    EXPECT_EQ(parsed("-0,75"), parsed("-0.75"));
    EXPECT_EQ(parsed(",5"), 0.5f);
}

TEST(TypedValue, TheResultIsTheNearestFloatToTheText) {
    EXPECT_EQ(parsed("0.7"), 0.7f);
    EXPECT_EQ(parsed("1.2"), 1.2f);
    EXPECT_EQ(parsed("0.1"), 0.1f);
    EXPECT_EQ(parsed("123456.789"), 123456.789f);
    EXPECT_EQ(parsed("0.000000000000000000000000001"), 1e-27f);  // more zeros than a table holds
    EXPECT_EQ(parsed("3.14159265358979323846264338"), 3.14159265358979323846f);  // digits dropped
}

TEST(TypedValue, HugeNumbersReadAsAVeryLargeOne) {
    EXPECT_EQ(parsed(std::string(60, '9')), 1e30f);
    EXPECT_EQ(parsed("-" + std::string(500, '9')), -1e30f);
    EXPECT_EQ(parsed("1" + std::string(40, '0')), 1e30f);
}

TEST(TypedValue, ExposureTakesTheWordStopsAfterTheNumber) {
    using prism::gui::SimpleControl;
    EXPECT_FLOAT_EQ(parsed("1.5 stops", SimpleControl::brightness), 1.5f);
    EXPECT_FLOAT_EQ(parsed("-0.7stops", SimpleControl::brightness), -0.7f);
    EXPECT_FLOAT_EQ(parsed("+0.70   STOPS  ", SimpleControl::brightness), 0.7f);
    EXPECT_FLOAT_EQ(parsed("2", SimpleControl::brightness), 2.0f);
    for (const std::string_view bad : {"1.5 x", "1.5 \xC3\x97", "1.5 stopss", "stops", "stop", "1.5 stops!"}) {
        EXPECT_FALSE(prism::gui::typed_row_value(SimpleControl::brightness, bad)) << bad;
    }
}

TEST(TypedValue, ExposureTakesTheSingularToo) {
    using prism::gui::SimpleControl;
    EXPECT_FLOAT_EQ(parsed("1 stop", SimpleControl::brightness), 1.0f);
    EXPECT_FLOAT_EQ(parsed("-1STOP", SimpleControl::brightness), -1.0f);
    EXPECT_FLOAT_EQ(parsed("0.5stop", SimpleControl::brightness), 0.5f);
    EXPECT_FALSE(prism::gui::typed_row_value(SimpleControl::colour, "1 stop"));
}

TEST(TypedValue, TheMinusSignOfTheGuideIsAMinus) {
    using prism::gui::SimpleControl;
    // U+2212, as the guide and the strip's labels print it.
    EXPECT_FLOAT_EQ(parsed("\xE2\x88\x92" "0.5", SimpleControl::brightness), -0.5f);
    EXPECT_FLOAT_EQ(parsed(" \xE2\x88\x92" "0,5 stops ", SimpleControl::brightness), -0.5f);
    EXPECT_FLOAT_EQ(parsed("\xE2\x88\x92" "1 stop", SimpleControl::brightness), -1.0f);
    EXPECT_FLOAT_EQ(parsed("\xE2\x88\x92" "0.2", SimpleControl::colour), -0.2f);
    for (const std::string_view bad : {"\xE2\x88\x92", "\xE2\x88\x92" "-1", "-" "\xE2\x88\x92" "1",
                                       "1\xE2\x88\x92", "\xE2\x88" "1", "+" "\xE2\x88\x92" "1"}) {
        EXPECT_FALSE(prism::gui::typed_row_value(SimpleControl::brightness, bad)) << bad;
    }
}

TEST(TypedValue, SaturationAndContrastTakeATimesSign) {
    for (const SimpleControl control : {SimpleControl::colour, SimpleControl::contrast}) {
        EXPECT_FLOAT_EQ(parsed("1.2 x", control), 1.2f);
        EXPECT_FLOAT_EQ(parsed("1.2x", control), 1.2f);
        EXPECT_FLOAT_EQ(parsed("1.2X", control), 1.2f);
        EXPECT_FLOAT_EQ(parsed(std::string("1.2") + std::string(times), control), 1.2f);
        EXPECT_FLOAT_EQ(parsed(std::string("1,2 ") + std::string(times) + " ", control), 1.2f);
        EXPECT_FALSE(prism::gui::typed_row_value(control, "1.2 stops"));
    }
    // The value line puts the sign first; Saturation's field takes it after the number (Contrast's
    // takes it before too, see the paper grade tests).
    EXPECT_FALSE(prism::gui::typed_row_value(SimpleControl::colour, std::string(times) + "1.2"));
}

TEST(TypedValue, RejectsEverythingElse) {
    for (const std::string_view bad :
         {"", " ", "+", "-", ".", ",", "+.", "abc", "1.2.3", "1,2,3", "1.000,5", "1e3", "1.2e-1",
          "--1", "+-1", "1-", "1 2", "0x10", "nan", "inf", "-inf", "1.2 y", "1.2 xx", "1.2x2",
          "1.2%", "(1)", "1_000", "\xD9\xA1"}) {
        for (const SimpleControl control :
             {SimpleControl::brightness, SimpleControl::colour, SimpleControl::contrast}) {
            EXPECT_FALSE(prism::gui::typed_row_value(control, bad)) << bad;
            EXPECT_FALSE(prism::gui::typed_slider_position(control, bad)) << bad;
        }
    }
}

TEST(TypedValue, ReadsTheSameUnderAnotherLocale) {
    const char* const before = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = before != nullptr ? before : "C";
    // A comma locale, if this machine has one; the parser must not care either way.
    for (const char* name : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "German_Germany"}) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) {
            break;
        }
    }
    EXPECT_EQ(parsed("1.5"), 1.5f);
    EXPECT_EQ(parsed("1,5"), 1.5f);
    EXPECT_EQ(prism::gui::typed_field_text(EditSettings{}, SimpleControl::colour), "1.00");
    std::setlocale(LC_NUMERIC, saved.c_str());
}

TEST(TypedValue, ExposureIsConvertedThroughTheStopsMap) {
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::brightness, "1 stops"), 50.0f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::brightness, "-0.7"), -35.0f);
    // The range of the slider: -2 to +2 stops.
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::brightness, "5"), 100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::brightness, "-9 stops"), -100.0f);
}

TEST(TypedValue, SaturationIsConvertedThroughItsMap) {
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "1.2"), 20.0f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "1"), 0.0f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "0"), -100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "3"), 100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "-4"), -100.0f);
}

TEST(TypedValue, SimplePositionForIsTheInverseOfApplySimpleControl) {
    for (const SimpleControl control : {SimpleControl::brightness, SimpleControl::colour}) {
        for (float position = -100.0f; position <= 100.0f; position += 12.5f) {
            EditSettings s;
            prism::gui::apply_simple_control(s, control, position);
            const float value =
                control == SimpleControl::brightness ? s.exposure_stops : s.saturation;
            EXPECT_NEAR(prism::gui::simple_position_for(control, value), position, 1e-3f)
                << position;
        }
    }
    EXPECT_EQ(prism::gui::simple_position_for(SimpleControl::colour, std::nanf("")), 0.0f);
}

TEST(TypedValue, ContrastHasNoSliderPositionOnTheOldScale) {
    for (const std::string_view text : {"1.5x", "1x", "3", "grade 4", "\xC3\x97" "1.2"}) {
        EXPECT_TRUE(prism::gui::typed_row_value(SimpleControl::contrast, text)) << text;
        EXPECT_FALSE(prism::gui::typed_slider_position(SimpleControl::contrast, text)) << text;
    }
}

// Typed value -> field -> value line: the line shows what was typed, to the line's two places.
TEST(TypedValue, TheValueLineShowsWhatWasTyped) {
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "0.7"), "+0.70 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "-1.25 stops"), "−1.25 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "+1,5"), "+1.50 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "0"), "+0.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "2"), "+2.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "-2.00"), "−2.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "9"), "+2.00 stops");  // clamped

    EXPECT_EQ(line_after_typing(SimpleControl::colour, "1.2"), "\xC3\x97" "1.20");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "0.35 x"), "\xC3\x97" "0.35");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "0"), "black and white");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "2"), "\xC3\x97" "2.00");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "7"), "\xC3\x97" "2.00");  // clamped

    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "3"), "Grade 3 \xC2\xB7 \xC3\x97" "1.29");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "1x"), "Grade 2 \xC2\xB7 \xC3\x97" "1.00");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "50x"),
              "Harder than grade 5 \xC2\xB7 \xC3\x97" "10.00");  // clamped to contrast_range
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "0.01x"),
              "Softer than grade 0 \xC2\xB7 \xC3\x97" "0.10");
}

TEST(TypedValue, EveryTwoPlaceValueTypedComesBackOnTheLine) {
    for (int hundredths = 0; hundredths <= 200; ++hundredths) {
        const std::string text = std::to_string(hundredths / 100) + "." +
                                 (hundredths % 100 < 10 ? "0" : "") +
                                 std::to_string(hundredths % 100);
        if (hundredths != 0) {
            EXPECT_EQ(line_after_typing(SimpleControl::colour, text), "\xC3\x97" + text) << text;
        }
    }
    for (int hundredths = 67; hundredths <= 150; ++hundredths) {  // the Contrast slider's range
        const std::string text = std::to_string(hundredths / 100) + "." +
                                 (hundredths % 100 < 10 ? "0" : "") +
                                 std::to_string(hundredths % 100);
        const std::string line = line_after_typing(SimpleControl::contrast, text + "x");
        const std::string tail = " \xC2\xB7 \xC3\x97" + text;
        ASSERT_GE(line.size(), tail.size()) << text;
        EXPECT_EQ(line.substr(line.size() - tail.size()), tail) << text;
    }
    for (int hundredths = -200; hundredths <= 200; ++hundredths) {  // Exposure's, in stops
        const int a = std::abs(hundredths);
        const std::string digits =
            std::to_string(a / 100) + "." + (a % 100 < 10 ? "0" : "") + std::to_string(a % 100);
        // Typed with the hyphen people's keyboards have; the line shows a real minus sign.
        const std::string text = (hundredths < 0 ? "-" : "+") + digits;
        const std::string line = (hundredths < 0 ? "−" : "+") + digits + " stops";
        EXPECT_EQ(line_after_typing(SimpleControl::brightness, text), line) << text;
    }
}

TEST(TypedValue, TheFieldOpensOnTheStoredValueAndReadsBackAsIt) {
    EditSettings s;
    prism::gui::apply_simple_control(s, SimpleControl::brightness, 35.0f);
    prism::gui::apply_simple_control(s, SimpleControl::colour, 20.0f);
    s.contrast = prism::gui::contrast_for_grade(3.5f);
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::brightness), "+0.70 stops");
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::colour), "1.20");
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::contrast), "3\xC2\xBD");
    // Re-typing what the field held changes nothing the line shows.
    for (const SimpleControl control :
         {SimpleControl::brightness, SimpleControl::colour, SimpleControl::contrast}) {
        const std::string text = prism::gui::typed_field_text(s, control);
        EXPECT_EQ(line_after_typing(control, text, s),
                  prism::gui::stage_value_text(s, prism::gui::stage_of(control)))
            << text;
    }
    // A stage that is off still opens on the value it comes back with; black and white on 0.00.
    s.saturation = 0.0f;
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::colour), "0.00");
    s.exposure_on = false;
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::brightness), "+0.70 stops");
}

// --- White balance -----------------------------------------------------------------------------

namespace {

using prism::gui::WhiteBalanceControl;

std::optional<double> kelvin_typed(std::string_view text) {
    return prism::gui::typed_white_balance_value(WhiteBalanceControl::kelvin, text);
}

std::optional<double> tint_typed(std::string_view text) {
    return prism::gui::typed_white_balance_value(WhiteBalanceControl::tint, text);
}

}  // namespace

TEST(TypedWhiteBalance, KelvinTakesTheNumberWithOrWithoutItsK) {
    for (const std::string_view text :
         {"5200", "5200 K", "5200K", "5 200", "5200 k", " 5200 ", "5 200 K", "5200 kelvin",
          "5,200", "5.200 K", "5200.4", "5204"}) {
        EXPECT_EQ(kelvin_typed(text), 5200.0) << text;
    }
    EXPECT_EQ(kelvin_typed("12 500"), 12500.0);
    EXPECT_EQ(kelvin_typed("12,500"), 12500.0);
    EXPECT_EQ(kelvin_typed("5206"), 5210.0);  // to 10 K, as a drag
}

TEST(TypedWhiteBalance, KelvinIsClampedToItsRange) {
    EXPECT_EQ(kelvin_typed("1000"), 2000.0);
    EXPECT_EQ(kelvin_typed("20000 K"), 15000.0);
    EXPECT_EQ(kelvin_typed("99999999999"), 15000.0);
    EXPECT_EQ(kelvin_typed("5.2"), 2000.0);  // a decimal, not thousands
}

TEST(TypedWhiteBalance, KelvinRejectsWhatIsNotATemperature) {
    for (const std::string_view text :
         {"", "K", "warm", "5200 stops", "5200 x", "-5200", "0", "5200 K K", "1e4", "5,200,0"}) {
        EXPECT_EQ(kelvin_typed(text), std::nullopt) << text;
    }
}

TEST(TypedWhiteBalance, TintTakesASignedWholeNumber) {
    EXPECT_EQ(tint_typed("10"), 10.0);
    EXPECT_EQ(tint_typed("+10"), 10.0);
    EXPECT_EQ(tint_typed("-10"), -10.0);
    EXPECT_EQ(tint_typed("\xE2\x88\x92" "10"), -10.0);  // U+2212, as the row shows it
    EXPECT_EQ(tint_typed(" 0 "), 0.0);
    EXPECT_EQ(tint_typed("9.6"), 10.0);
    EXPECT_EQ(tint_typed("-9,6"), -10.0);
}

TEST(TypedWhiteBalance, TintIsClampedAndRejectsWords) {
    EXPECT_EQ(tint_typed("250"), 100.0);
    EXPECT_EQ(tint_typed("-250"), -100.0);
    for (const std::string_view text : {"", "+", "green", "10 K", "10 stops", "1 0"}) {
        EXPECT_EQ(tint_typed(text), std::nullopt) << text;
    }
}

TEST(TypedWhiteBalance, TheFieldOpensOnWhatTheSliderShowsAndReadsBackAsIt) {
    for (const prism::WhiteBalance shown :
         {prism::WhiteBalance{5330, 27}, prism::WhiteBalance{2000, -100},
          prism::WhiteBalance{15000, 0}, prism::WhiteBalance{6500, -1}}) {
        const std::string kelvin =
            prism::gui::typed_field_text(shown, WhiteBalanceControl::kelvin);
        const std::string tint = prism::gui::typed_field_text(shown, WhiteBalanceControl::tint);
        EXPECT_EQ(kelvin_typed(kelvin), shown.kelvin) << kelvin;
        EXPECT_EQ(tint_typed(tint), shown.tint) << tint;
    }
    EXPECT_EQ(prism::gui::typed_field_text(prism::WhiteBalance{5330, 27},
                                           WhiteBalanceControl::kelvin),
              "5330 K");
    EXPECT_EQ(prism::gui::typed_field_text(prism::WhiteBalance{5330, 27},
                                           WhiteBalanceControl::tint),
              "+27");
}

TEST(TypedWhiteBalance, ATypedValueIsOneChangeThroughTheSameSetterAsADrag) {
    EditSettings s;
    const auto as_shot = std::optional<prism::Chromaticity>(
        prism::white_of(prism::WhiteBalance{5328, 26.7}));
    prism::gui::set_white_balance(s, WhiteBalanceControl::kelvin, *kelvin_typed("5 200"), as_shot);
    EXPECT_EQ(s.white_balance, (prism::WhiteBalance{5200, 27}));
    prism::gui::set_white_balance(s, WhiteBalanceControl::tint, *tint_typed("\xE2\x88\x92" "10"),
                                  as_shot);
    EXPECT_EQ(s.white_balance, (prism::WhiteBalance{5200, -10}));
    EXPECT_EQ(prism::gui::white_balance_value_text(s), "Chosen");
    EXPECT_EQ(prism::gui::stage_value_text(s, prism::gui::StageId::white_balance),
              "5200 K \xC2\xB7 tint \xE2\x88\x92" "10");
}
