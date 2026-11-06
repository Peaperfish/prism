#include "prism/gui/theme_tokens.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace prism::gui {
namespace {

std::string label(const TokenSet& s) {
    return std::string(s.style) + (s.theme == Theme::dark ? " dark" : " light");
}

// Everything text can sit on at rest.
std::vector<std::pair<const char*, Rgba>> backgrounds(const TokenColours& c) {
    return {{"panel", c.panel},     {"chrome", c.chrome},   {"popup", c.popup_bg},
            {"title", c.title_bg},  {"well", c.well},       {"control", c.control},
            {"control on", c.control_on}, {"explain", c.explain}};
}

// ... and while hovered or pressed, where only the body text is drawn.
std::vector<std::pair<const char*, Rgba>> interactive_backgrounds(const TokenColours& c) {
    return {{"control hovered", c.control_hovered}, {"control active", c.control_active}};
}

TEST(ThemeTokens, EveryThemeHasASet) {
    EXPECT_FALSE(token_sets().empty());
    for (const Theme theme : {Theme::dark, Theme::light}) {
        EXPECT_EQ(token_set(theme).theme, theme);
    }
}

TEST(ThemeTokens, BodyAndMutedTextAreReadableOnEveryBackground) {
    for (const TokenSet& set : token_sets()) {
        for (const auto& [name, bg] : backgrounds(set.colours)) {
            EXPECT_GE(contrast_ratio(set.colours.text, bg), 4.5f) << label(set) << " text on " << name;
            EXPECT_GE(contrast_ratio(set.colours.text_muted, bg), 4.5f)
                << label(set) << " muted text on " << name;
        }
        for (const auto& [name, bg] : interactive_backgrounds(set.colours)) {
            EXPECT_GE(contrast_ratio(set.colours.text, bg), 4.5f) << label(set) << " text on " << name;
        }
    }
}

TEST(ThemeTokens, BordersAndCheckmarksAreSeenAgainstEveryBackground) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        for (const auto& [name, bg] : backgrounds(c)) {
            if (std::string(name) == "chrome" || std::string(name) == "title" ||
                std::string(name) == "explain" || std::string(name) == "control on") {
                continue;  // no outlined controls are drawn on these
            }
            EXPECT_GE(contrast_ratio(c.control_border, bg), 3.0f) << label(set) << " border on " << name;
            EXPECT_GE(contrast_ratio(c.check_fill, bg), 3.0f) << label(set) << " ticked box on " << name;
        }
    }
}

TEST(ThemeTokens, TheAccentCarriesItsLabelAndTheViewedDotIsSeenOnThePanel) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The Export button: the label on the accent, hovered or pressed too.
        EXPECT_GE(contrast_ratio(c.text_on_accent, c.accent), 4.5f) << label(set);
        EXPECT_GE(contrast_ratio(c.text_on_accent, c.accent_active), 4.5f) << label(set);
        // The viewed stage's dot: its border (the ring colour) against the panel it sits on.
        EXPECT_GE(contrast_ratio(c.accent_ring, c.panel), 3.0f) << label(set);
        // The fill and the switch-on colour are seen on the panel and on a control too.
        EXPECT_GE(contrast_ratio(c.fill, c.panel), 3.0f) << label(set);
        EXPECT_GE(contrast_ratio(c.fill, c.control), 3.0f) << label(set);
    }
}

TEST(ThemeTokens, FaintIsSeenOnThePanel) {
    for (const TokenSet& set : token_sets()) {
        // An unlit dot's border and a disabled icon: not read, but must be seen.
        EXPECT_GE(contrast_ratio(set.colours.faint, set.colours.panel), 3.0f) << label(set);
    }
}

TEST(ThemeTokens, HoverAndPressAreAStepFromTheControl) {
    const TokenColours& dark = token_set(Theme::dark).colours;
    const TokenColours& light = token_set(Theme::light).colours;
    // A step lighter in Dark, a step darker in Light.
    EXPECT_GT(lightness(dark.control_hovered), lightness(dark.control));
    EXPECT_GT(lightness(dark.control_active), lightness(dark.control_hovered));
    EXPECT_LT(lightness(light.control_hovered), lightness(light.control));
    EXPECT_LT(lightness(light.control_active), lightness(light.control_hovered));
}

TEST(ThemeTokens, TheTickIsSeenOnTheTickedBox) {
    for (const TokenSet& set : token_sets()) {
        EXPECT_GE(contrast_ratio(set.colours.check_mark, set.colours.check_fill), 3.0f) << label(set);
    }
}

TEST(ThemeTokens, LitClipChipsAreReadableAndStillToldApart) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        EXPECT_GE(contrast_ratio(c.chip_text, c.chip_shadows), 4.5f) << label(set);
        EXPECT_GE(contrast_ratio(c.chip_text, c.chip_highlights), 4.5f) << label(set);
        // Blue for shadows, orange for highlights: not two similar muted tones.
        EXPECT_GT(c.chip_shadows.b, c.chip_shadows.r + 0.25f) << label(set);
        EXPECT_GT(c.chip_highlights.r, c.chip_highlights.b + 0.25f) << label(set);
    }
}

// `top` (with its alpha) over a flat `under`.
Rgba composite(const Rgba& top, const Rgba& under) {
    return {top.r * top.a + under.r * (1 - top.a), top.g * top.a + under.g * (1 - top.a),
            top.b * top.a + under.b * (1 - top.a), 1};
}

TEST(ThemeTokens, StripChipTextIsReadableOnAnyPhoto) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The chip sits on the picture: the worst cases are pure white and pure black.
        for (const float photo : {0.0f, 0.5f, 1.0f}) {
            EXPECT_GE(contrast_ratio(c.strip_chip_text, composite(c.strip_chip_bg, grey(photo))),
                      7.0f)
                << label(set) << " photo " << photo;
        }
    }
}

TEST(ThemeTokens, ZoomChipIsDarkInBothThemesAndItsTextIsReadable) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The chip sits on the picture: the worst cases are pure white and pure black.
        for (const float photo : {0.0f, 0.5f, 1.0f}) {
            const Rgba chip = composite(c.zoom_chip_bg, grey(photo));
            EXPECT_LT(lightness(chip), 45.0f) << label(set) << " photo " << photo;
            EXPECT_GE(contrast_ratio(c.zoom_chip_text, chip), 4.5f) << label(set) << " photo " << photo;
            // The choice that is not current, also while hovered.
            EXPECT_GE(contrast_ratio(c.zoom_chip_idle_text, chip), 4.5f)
                << label(set) << " photo " << photo;
            EXPECT_GE(contrast_ratio(c.zoom_chip_idle_text, composite(c.zoom_chip_hovered, chip)), 4.5f)
                << label(set) << " photo " << photo;
            // The current choice: white on its wash, which can be seen against the chip.
            const Rgba current = composite(c.zoom_chip_current, chip);
            EXPECT_GE(contrast_ratio(c.zoom_chip_current_text, current), 4.5f)
                << label(set) << " photo " << photo;
            EXPECT_GT(contrast_ratio(current, chip), 1.1f) << label(set) << " photo " << photo;
        }
    }
}

TEST(ThemeTokens, StripLinesAreSeenOnTheirOwnColours) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The divider between bands on a mid grey and on black; the hovered band's edge on both.
        for (const float photo : {0.0f, 0.25f}) {
            EXPECT_GE(contrast_ratio(composite(c.strip_divider, grey(photo)), grey(photo)), 3.0f)
                << label(set);
            EXPECT_GE(contrast_ratio(c.strip_hover_edge, grey(photo)), 3.0f) << label(set);
        }
        EXPECT_GT(c.strip_hover_fill.a, 0.0f) << label(set);
        EXPECT_LT(c.strip_hover_fill.a, 0.3f) << label(set);  // a wash, not a cover
    }
}

TEST(ThemeTokens, StripBarFollowsTheThemeAndItsTextIsReadable) {
    const TokenColours& dark = token_set(Theme::dark).colours;
    const TokenColours& light = token_set(Theme::light).colours;
    // Dark text on a light bar in Light, light text on a dark bar in Dark.
    EXPECT_LT(lightness(dark.strip_bar_bg), 30.0f);
    EXPECT_GT(lightness(light.strip_bar_bg), 70.0f);
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        EXPECT_GE(contrast_ratio(c.strip_bar_text, c.strip_bar_bg), 4.5f) << label(set);
        // The chosen step's pill and the hovered one sit on the bar; the text is read on them.
        const Rgba pill = composite(c.strip_bar_pill, c.strip_bar_bg);
        const Rgba hover = composite(c.strip_bar_hover, c.strip_bar_bg);
        EXPECT_GE(contrast_ratio(c.strip_bar_text, pill), 4.5f) << label(set) << " pill";
        EXPECT_GE(contrast_ratio(c.strip_bar_text, hover), 4.5f) << label(set) << " hover";
        // ... and the pill can be seen against the bar without being a hole in it.
        EXPECT_GT(contrast_ratio(pill, c.strip_bar_bg), 1.1f) << label(set);
        // The bar sits on the window: not a different world from it.
        EXPECT_LT(std::abs(lightness(c.strip_bar_bg) - lightness(c.panel)), 20.0f) << label(set);
    }
}

TEST(ThemeTokens, ExplainAreaShowsAboutFiveLines) {
    EXPECT_EQ(explain_max_lines, 5);
}

TEST(ThemeTokens, ScopePlotIsDarkInBothThemes) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        EXPECT_LT(lightness(c.plot), 10.0f) << label(set);
        // Its hairline is quieter than a control's outline.
        EXPECT_LT(contrast_ratio(c.plot_border, c.plot), 1.6f) << label(set);
    }
}

TEST(ThemeTokens, ExplainAreaTextIsReadableInBothThemes) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The Explain area has its own background: summary and detail in the body colour, the
        // live line and the hint in the muted one.
        EXPECT_GE(contrast_ratio(c.text, c.explain), 4.5f) << label(set);
        EXPECT_GE(contrast_ratio(c.text_muted, c.explain), 4.5f) << label(set);
        EXPECT_GT(set.metrics.explain_pad.x, 0.0f) << label(set);
        EXPECT_GT(set.metrics.explain_pad.y, 0.0f) << label(set);
        EXPECT_GT(set.metrics.explain_gap, 0.0f) << label(set);
        // The small type the Explain area is set in stays readable.
        EXPECT_GE(set.type.small, 12.0f) << label(set);
        EXPECT_GE(set.type.explain_title, set.type.small) << label(set);
    }
}

TEST(ThemeTokens, CanvasIsNeutralAndNeitherBlackNorWhite) {
    for (const TokenSet& set : token_sets()) {
        const Rgba& canvas = set.colours.canvas;
        EXPECT_EQ(canvas.r, canvas.g) << label(set);
        EXPECT_EQ(canvas.g, canvas.b) << label(set);
        EXPECT_GE(lightness(canvas), 15.0f) << label(set);
        EXPECT_LE(lightness(canvas), 55.0f) << label(set);
    }
}

TEST(ThemeTokens, SurfacesAndTextAreOnlySlightlyTinted) {
    const auto spread = [](const Rgba& g) {
        return std::max({g.r, g.g, g.b}) - std::min({g.r, g.g, g.b});
    };
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // A cool tint of a few steps is intended; a stronger one would colour the photo's surround.
        for (const Rgba& g : {c.chrome, c.panel, c.well, c.plot, c.control, c.explain, c.hair, c.text,
                              c.control_border}) {
            EXPECT_LE(spread(g), 6.0f / 255.0f + 1e-5f) << label(set);
        }
        // The design's own muted, faint and active-segment colours lean a little further (7 to 8
        // steps), kept as drawn.
        for (const Rgba& g : {c.text_muted, c.faint, c.control_on}) {
            EXPECT_LE(spread(g), 8.0f / 255.0f + 1e-5f) << label(set);
        }
    }
}

TEST(ThemeTokens, TypeSizesAreAsDesigned) {
    for (const TokenSet& set : token_sets()) {
        EXPECT_EQ(set.type.body, 16.0f) << label(set);
        EXPECT_LT(set.type.section, set.type.body) << label(set);
        EXPECT_LE(set.type.small, set.type.body) << label(set);
    }
}

// Every field of TokenMetrics is a float or two; a field added without teaching scaled() about it
// would not scale, so the size is pinned here.
static_assert(sizeof(TokenMetrics) == 99 * sizeof(float));

TEST(ThemeTokens, EverySizeScalesLinearlyWithTheInterfaceScale) {
    for (const TokenSet& set : token_sets()) {
        const float* base = reinterpret_cast<const float*>(&set.metrics);
        constexpr std::size_t count = sizeof(TokenMetrics) / sizeof(float);
        for (const float scale : {0.75f, 1.0f, 1.5f, 2.0f}) {
            const TokenMetrics m = scaled(set.metrics, scale);
            const float* out = reinterpret_cast<const float*>(&m);
            for (std::size_t i = 0; i < count; ++i) {
                EXPECT_FLOAT_EQ(out[i], base[i] * scale) << label(set) << " metric " << i;
            }
            const TokenType t = scaled(set.type, scale);
            EXPECT_FLOAT_EQ(t.body, set.type.body * scale);
            EXPECT_FLOAT_EQ(t.section, set.type.section * scale);
            EXPECT_FLOAT_EQ(t.small, set.type.small * scale);
            EXPECT_FLOAT_EQ(t.tiny, set.type.tiny * scale);
            EXPECT_FLOAT_EQ(t.explain_title, set.type.explain_title * scale);
        }
    }
}

TEST(ThemeTokens, MockupSizesAreSixteenThirteenthsAndScaleLinearly) {
    // The mockup's body text is 13 px and the app's 16 points.
    EXPECT_FLOAT_EQ(mock_points(13.0f), 16.0f);
    EXPECT_FLOAT_EQ(mock_points(52.0f), 64.0f);
    for (const float px : {1.0f, 12.0f, 12.5f, 34.0f}) {
        for (const float scale : {0.75f, 1.0f, 1.5f, 2.0f}) {
            EXPECT_FLOAT_EQ(mock(px, scale), mock(px, 1.0f) * scale) << px << " at " << scale;
        }
    }
    // The type sizes taken from the mockup.
    for (const TokenSet& set : token_sets()) {
        EXPECT_FLOAT_EQ(set.type.section, mock_points(12.0f)) << label(set);
        EXPECT_FLOAT_EQ(set.type.small, mock_points(12.0f)) << label(set);
        EXPECT_FLOAT_EQ(set.type.tiny, mock_points(11.5f)) << label(set);
        EXPECT_FLOAT_EQ(set.type.explain_title, mock_points(12.5f)) << label(set);
        EXPECT_FLOAT_EQ(set.metrics.header_height, mock_points(52.0f)) << label(set);
        EXPECT_FLOAT_EQ(set.metrics.status_height, mock_points(30.0f)) << label(set);
    }
}

TEST(ThemeTokens, ContrastAndLightnessMatchKnownValues) {
    EXPECT_NEAR(contrast_ratio(grey(0), grey(1)), 21.0f, 0.01f);
    EXPECT_NEAR(contrast_ratio(grey(0.5f), grey(0.5f)), 1.0f, 1e-6f);
    EXPECT_NEAR(lightness(grey(1)), 100.0f, 0.01f);
    EXPECT_NEAR(lightness(grey(0)), 0.0f, 0.01f);
    EXPECT_NEAR(lightness(grey(118.0f / 255.0f)), 50.0f, 1.0f);
}

}  // namespace
}  // namespace prism::gui
