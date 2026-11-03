#include "prism/gui/theme_tokens.hpp"

#include <gtest/gtest.h>

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
    return {{"window", c.window_bg}, {"menubar", c.menubar_bg}, {"popup", c.popup_bg},
            {"title", c.title_bg},   {"frame", c.frame_bg},     {"button", c.button},
            {"header", c.header}};
}

// ... and while hovered or pressed, where only the body text is drawn.
std::vector<std::pair<const char*, Rgba>> interactive_backgrounds(const TokenColours& c) {
    return {{"frame hovered", c.frame_bg_hovered},   {"frame active", c.frame_bg_active},
            {"button hovered", c.button_hovered},    {"button active", c.button_active},
            {"header hovered", c.header_hovered},    {"header active", c.header_active}};
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

TEST(ThemeTokens, BordersAccentAndCheckmarksAreSeenAgainstEveryBackground) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        for (const auto& [name, bg] : backgrounds(c)) {
            if (std::string(name) == "menubar" || std::string(name) == "title") {
                continue;  // no controls are drawn on these
            }
            EXPECT_GE(contrast_ratio(c.control_border, bg), 3.0f) << label(set) << " border on " << name;
            EXPECT_GE(contrast_ratio(c.accent, bg), 3.0f) << label(set) << " accent on " << name;
            EXPECT_GE(contrast_ratio(c.accent_active, bg), 3.0f)
                << label(set) << " active accent on " << name;
            EXPECT_GE(contrast_ratio(c.check_fill, bg), 3.0f) << label(set) << " ticked box on " << name;
        }
    }
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

TEST(ThemeTokens, StripPillTextIsReadableOnTheBar) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The bar is the overlay box (black at 180/255) over the window; the chosen pill adds a
        // light wash, and the bar's text is white.
        const Rgba bar = composite(grey(0.0f, 180.0f / 255.0f), c.window_bg);
        EXPECT_GE(contrast_ratio(grey(1.0f), composite(c.strip_pill_active, bar)), 4.5f)
            << label(set);
    }
}

TEST(ThemeTokens, PlotBoxSitsCalmlyInThePanel) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // A step off the window colour, not a white (or black) hole in it, and its hairline is
        // quieter than a control's outline.
        EXPECT_LT(std::abs(lightness(c.plot_bg) - lightness(c.window_bg)), 12.0f) << label(set);
        EXPECT_LT(contrast_ratio(c.plot_border, c.plot_bg), 1.6f) << label(set);
        EXPECT_GE(contrast_ratio(c.text, c.plot_bg), 4.5f) << label(set);
    }
}

TEST(ThemeTokens, ExplainAreaTextIsReadableInBothThemes) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        // The Explain area sits on the plot fill: summary and detail in the body colour, the live
        // line and the hint in the muted one.
        EXPECT_GE(contrast_ratio(c.text, c.plot_bg), 4.5f) << label(set);
        EXPECT_GE(contrast_ratio(c.text_muted, c.plot_bg), 4.5f) << label(set);
        EXPECT_GT(set.metrics.explain_pad.x, 0.0f) << label(set);
        EXPECT_GT(set.metrics.explain_pad.y, 0.0f) << label(set);
        EXPECT_GT(set.metrics.explain_gap, 0.0f) << label(set);
        // The small type the Explain area is set in stays readable.
        EXPECT_GE(set.type.small, 12.0f) << label(set);
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

TEST(ThemeTokens, ChromeGreysAreNeutral) {
    for (const TokenSet& set : token_sets()) {
        const TokenColours& c = set.colours;
        for (const Rgba& g : {c.window_bg, c.frame_bg, c.button, c.header, c.edge, c.text,
                              c.text_muted, c.control_border}) {
            EXPECT_EQ(g.r, g.g) << label(set);
            EXPECT_EQ(g.g, g.b) << label(set);
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
static_assert(sizeof(TokenMetrics) == 78 * sizeof(float));

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
        }
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
