#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

#include "prism/gui/stage_controls.hpp"
#include "prism/gui/typed_value.hpp"

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
// app takes: text -> slider position -> settings field -> value line.
std::string line_after_typing(SimpleControl control, std::string_view text,
                              EditSettings s = {}) {
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
    for (const std::string_view bad : {"1.5 x", "1.5 \xC3\x97", "1.5 stop", "stops", "1.5 stops!"}) {
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
        // The value line puts the sign first; the field takes it after the number.
        EXPECT_FALSE(prism::gui::typed_row_value(control, std::string(times) + "1.2"));
    }
}

TEST(TypedValue, RejectsEverythingElse) {
    for (const std::string_view bad :
         {"", " ", "+", "-", ".", ",", "+.", "abc", "1.2.3", "1,2,3", "1.000,5", "1e3", "1.2e-1",
          "--1", "+-1", "1-", "1 2", "0x10", "nan", "inf", "-inf", "1.2 y", "1.2 xx", "1.2x2",
          "\xE2\x88\x92" "1", "1.2%", "(1)", "1_000", "\xD9\xA1"}) {
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

TEST(TypedValue, SaturationAndContrastAreConvertedThroughTheirMaps) {
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "1.2"), 20.0f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "1"), 0.0f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "0"), -100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "3"), 100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::colour, "-4"), -100.0f);
    EXPECT_NEAR(*prism::gui::typed_slider_position(SimpleControl::contrast, "1.5"), 100.0f, 1e-3f);
    EXPECT_FLOAT_EQ(*prism::gui::typed_slider_position(SimpleControl::contrast, "1"), 0.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::contrast, "5"), 100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::contrast, "0.1"), -100.0f);
    // No slider position gives a contrast of 0 or less: the low end.
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::contrast, "0"), -100.0f);
    EXPECT_EQ(*prism::gui::typed_slider_position(SimpleControl::contrast, "-2"), -100.0f);
}

TEST(TypedValue, SimplePositionForIsTheInverseOfApplySimpleControl) {
    for (const SimpleControl control :
         {SimpleControl::brightness, SimpleControl::contrast, SimpleControl::colour}) {
        for (float position = -100.0f; position <= 100.0f; position += 12.5f) {
            EditSettings s;
            prism::gui::apply_simple_control(s, control, position);
            const float value = control == SimpleControl::brightness ? s.exposure_stops
                                : control == SimpleControl::contrast ? s.contrast
                                                                     : s.saturation;
            EXPECT_NEAR(prism::gui::simple_position_for(control, value), position, 1e-3f)
                << position;
        }
    }
    EXPECT_EQ(prism::gui::simple_position_for(SimpleControl::colour, std::nanf("")), 0.0f);
}

// Typed value -> field -> value line: the line shows what was typed, to the line's two places.
TEST(TypedValue, TheValueLineShowsWhatWasTyped) {
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "0.7"), "+0.70 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "-1.25 stops"), "-1.25 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "+1,5"), "+1.50 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "0"), "+0.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "2"), "+2.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "-2.00"), "-2.00 stops");
    EXPECT_EQ(line_after_typing(SimpleControl::brightness, "9"), "+2.00 stops");  // clamped

    EXPECT_EQ(line_after_typing(SimpleControl::colour, "1.2"), "\xC3\x97" "1.20");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "0.35 x"), "\xC3\x97" "0.35");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "0"), "black and white");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "2"), "\xC3\x97" "2.00");
    EXPECT_EQ(line_after_typing(SimpleControl::colour, "7"), "\xC3\x97" "2.00");  // clamped

    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "1.2"), "\xC3\x97" "1.20");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "0.8\xC3\x97"), "\xC3\x97" "0.80");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "1"), "\xC3\x97" "1.00");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "1.5"), "\xC3\x97" "1.50");
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "9"), "\xC3\x97" "1.50");  // clamped
    EXPECT_EQ(line_after_typing(SimpleControl::contrast, "0.01"), "\xC3\x97" "0.67");
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
        EXPECT_EQ(line_after_typing(SimpleControl::contrast, text), "\xC3\x97" + text) << text;
    }
    for (int hundredths = -200; hundredths <= 200; ++hundredths) {  // Exposure's, in stops
        const int a = std::abs(hundredths);
        const std::string text = std::string(hundredths < 0 ? "-" : "+") + std::to_string(a / 100) +
                                 "." + (a % 100 < 10 ? "0" : "") + std::to_string(a % 100);
        EXPECT_EQ(line_after_typing(SimpleControl::brightness, text), text + " stops") << text;
    }
}

TEST(TypedValue, TheFieldOpensOnTheStoredValueAndReadsBackAsIt) {
    EditSettings s;
    prism::gui::apply_simple_control(s, SimpleControl::brightness, 35.0f);
    prism::gui::apply_simple_control(s, SimpleControl::colour, 20.0f);
    prism::gui::apply_simple_control(s, SimpleControl::contrast, -50.0f);
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::brightness), "+0.70 stops");
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::colour), "1.20");
    EXPECT_EQ(prism::gui::typed_field_text(s, SimpleControl::contrast), "0.82");
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
