#include <gtest/gtest.h>

#include <algorithm>
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
