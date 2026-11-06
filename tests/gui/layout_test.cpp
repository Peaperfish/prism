#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "prism/gui/layout.hpp"
#include "prism/gui/preferences.hpp"

using prism::gui::ellipsize_middle;
using prism::gui::panel_points_from_pixels;
using prism::gui::panel_width_pixels;

namespace {

// Every character (code point) is 10 units wide: a stand-in for ImGui's CalcTextSize.
float measure(std::string_view s) {
    float width = 0;
    for (char c : s) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            width += 10;
        }
    }
    return width;
}

// A continuation byte never starts the string or follows "...": no character was cut apart.
bool whole_characters(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool continuation = (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80;
        if (continuation && (i == 0 || s[i - 1] == '.')) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(Layout, PanelUsesThePreferredWidthWhenThereIsRoom) {
    EXPECT_FLOAT_EQ(panel_width_pixels(320, 1.0f, 1280), 320);
    EXPECT_FLOAT_EQ(panel_width_pixels(320, 2.0f, 2560), 640) << "scales with the interface";
    EXPECT_FLOAT_EQ(panel_width_pixels(400, 1.0f, 1280), 400);
}

TEST(Layout, PanelIsAtMostFortyPercentOfTheWindow) {
    EXPECT_FLOAT_EQ(panel_width_pixels(320, 1.0f, 700), 280);
    EXPECT_FLOAT_EQ(panel_width_pixels(1200, 1.0f, 1280), 512);
}

TEST(Layout, PanelIsAtLeastTwoHundredFortyPoints) {
    EXPECT_FLOAT_EQ(panel_width_pixels(100, 1.0f, 1280), 240);
    EXPECT_FLOAT_EQ(panel_width_pixels(100, 1.5f, 1920), 360);
}

TEST(Layout, OnANarrowWindowTheMinimumGivesWayButTheCanvasKeepsFortyPercent) {
    // 240 points are 480 pixels at 2x; 40% of 800 is only 320.
    const float width = panel_width_pixels(320, 2.0f, 800);
    EXPECT_FLOAT_EQ(width, 480) << "60% of the window";
    EXPECT_FLOAT_EQ(panel_width_pixels(320, 2.0f, 600), 360);
    EXPECT_FLOAT_EQ(panel_width_pixels(320, 1.0f, 0), 0);
    for (float available = 100; available <= 4000; available += 37) {
        for (float scale : {0.75f, 1.0f, 1.5f, 2.0f}) {
            const float w = panel_width_pixels(320, scale, available);
            EXPECT_LE(w, 0.6f * available + 1e-3f) << available << " " << scale;
            EXPECT_GE(w, std::min(240 * scale, 0.4f * available) - 1e-3f) << available;
        }
    }
}

TEST(Layout, TheDefaultPanelWidthIs440PointsAndStillCappedAtFortyPercent) {
    EXPECT_EQ(prism::gui::panel_width_default, 440);
    EXPECT_FLOAT_EQ(panel_width_pixels(prism::gui::panel_width_default, 1.0f, 1440), 440);
    EXPECT_FLOAT_EQ(panel_width_pixels(prism::gui::panel_width_default, 1.0f, 1000), 400);
    EXPECT_FLOAT_EQ(panel_width_pixels(prism::gui::panel_width_default, 2.0f, 2880), 880);
}

TEST(Layout, SplitterPixelsBecomeClampedPoints) {
    EXPECT_EQ(panel_points_from_pixels(640, 2.0f), 320);
    EXPECT_EQ(panel_points_from_pixels(301, 1.0f), 301);
    EXPECT_EQ(panel_points_from_pixels(10, 1.0f), prism::gui::panel_width_min);
    EXPECT_EQ(panel_points_from_pixels(100000, 1.0f), prism::gui::panel_width_max);
    EXPECT_EQ(panel_points_from_pixels(500, 0.0f), prism::gui::panel_width_default);
}

TEST(Layout, TextThatFitsIsUnchanged) {
    EXPECT_EQ(ellipsize_middle("photo.jpg", 90, measure), "photo.jpg");
    EXPECT_EQ(ellipsize_middle("", 0, measure), "");
}

TEST(Layout, LongTextLosesItsMiddle) {
    const std::string text = "a very long holiday photo name DSC09876.png";  // 43 characters
    const std::string out = ellipsize_middle(text, 200, measure);           // room for 20
    EXPECT_LE(measure(out), 200);
    EXPECT_EQ(measure(out), 200) << "as much as fits: " << out;
    EXPECT_EQ(out.find("..."), 5u) << "a third of what is kept is the start: " << out;
    EXPECT_TRUE(out.ends_with("DSC09876.png")) << "the end, with the extension, is kept: " << out;
    EXPECT_TRUE(out.starts_with("a ver"));
}

TEST(Layout, NeverCutsACharacterApart) {
    const std::string text = "写真の休暇_ü_Ångström_写真写真写真.png";
    for (float width = 30; width <= measure(text); width += 10) {
        const std::string out = ellipsize_middle(text, width, measure);
        EXPECT_LE(measure(out), width);
        EXPECT_TRUE(whole_characters(out)) << width;
    }
}

TEST(Layout, TooNarrowForAnything) {
    EXPECT_EQ(ellipsize_middle("photo.jpg", 30, measure), "...");
    EXPECT_EQ(ellipsize_middle("photo.jpg", 29, measure), "");
}

// --- Split ------------------------------------------------------------------------------------

TEST(SplitLayout, TheFractionIsKeptFromFivePercentToNinetyFive) {
    using namespace prism::gui;
    EXPECT_EQ(clamp_split_fraction(0.5f), 0.5f);
    EXPECT_EQ(clamp_split_fraction(-3.0f), split_fraction_min);
    EXPECT_EQ(clamp_split_fraction(0.0f), 0.05f);
    EXPECT_EQ(clamp_split_fraction(1.0f), 0.95f);
    EXPECT_EQ(clamp_split_fraction(42.0f), 0.95f);
    EXPECT_EQ(clamp_split_fraction(std::nanf("")), 0.5f);
    EXPECT_EQ(split_fraction_default, 0.5f);
}

TEST(SplitLayout, TheFractionFromTheMouse) {
    using namespace prism::gui;
    EXPECT_FLOAT_EQ(split_fraction_from_x(300, 100, 400), 0.5f);
    EXPECT_EQ(split_fraction_from_x(0, 100, 400), 0.05f);    // left of the picture
    EXPECT_EQ(split_fraction_from_x(900, 100, 400), 0.95f);  // right of it
    EXPECT_EQ(split_fraction_from_x(5, 0, 0), 0.5f);         // no width
}

TEST(SplitLayout, TheHalvesMeetOnAWholePixelWithOneTextureCoordinate) {
    using namespace prism::gui;
    const SplitLayout mid = split_layout(10.0f, 301.0f, 0.5f);  // 10 + 150.5 rounds to 161
    EXPECT_EQ(mid.left_x0, 10.0f);
    EXPECT_EQ(mid.divider_x, 161.0f);
    EXPECT_EQ(mid.right_x1, 311.0f);
    EXPECT_EQ(mid.divider_x, std::round(mid.divider_x));
    EXPECT_FLOAT_EQ(mid.u_divider, 151.0f / 301.0f);

    // A fractional picture position: the divider is still a whole pixel.
    const SplitLayout frac = split_layout(10.4f, 300.0f, 0.5f);
    EXPECT_EQ(frac.divider_x, std::round(frac.divider_x));
    EXPECT_FLOAT_EQ(frac.u_divider, (frac.divider_x - 10.4f) / 300.0f);
}

TEST(SplitLayout, TheEndsAreTheClampedFractions) {
    using namespace prism::gui;
    const SplitLayout left = split_layout(0.0f, 1000.0f, 0.0f);
    EXPECT_EQ(left.divider_x, 50.0f);
    EXPECT_FLOAT_EQ(left.u_divider, 0.05f);
    const SplitLayout right = split_layout(0.0f, 1000.0f, 1.0f);
    EXPECT_EQ(right.divider_x, 950.0f);
    EXPECT_FLOAT_EQ(right.u_divider, 0.95f);
    EXPECT_EQ(right.right_x1, 1000.0f);
}

TEST(SplitLayout, OnePixelWideAndEmptyPicturesStayInside) {
    using namespace prism::gui;
    for (const float fraction : {0.0f, 0.05f, 0.5f, 0.95f, 1.0f}) {
        const SplitLayout one = split_layout(7.0f, 1.0f, fraction);
        EXPECT_GE(one.divider_x, 7.0f);
        EXPECT_LE(one.divider_x, 8.0f);
        EXPECT_EQ(one.divider_x, std::round(one.divider_x));
        EXPECT_GE(one.u_divider, 0.0f);
        EXPECT_LE(one.u_divider, 1.0f);
        EXPECT_EQ(one.right_x1, 8.0f);
    }
    const SplitLayout none = split_layout(7.0f, 0.0f, 0.5f);
    EXPECT_EQ(none.divider_x, 7.0f);
    EXPECT_EQ(none.right_x1, 7.0f);
    EXPECT_EQ(none.u_divider, 0.0f);
}
