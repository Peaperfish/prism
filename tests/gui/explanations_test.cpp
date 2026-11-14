#include "prism/gui/explanations.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/detail_view.hpp"
#include "prism/gui/light_path.hpp"
#include "prism/gui/pixel_sample.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/theme_tokens.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/white_balance.hpp"

namespace prism::gui {
namespace {

constexpr StageId all_stages[] = {StageId::decode,   StageId::exposure,   StageId::dodge_burn,
                                  StageId::white_balance,
                                  StageId::matrix,   StageId::saturation, StageId::contrast,
                                  StageId::curve,    StageId::encode};
constexpr ScopeChoice all_scopes[] = {ScopeChoice::histogram, ScopeChoice::parade,
                                      ScopeChoice::waveform, ScopeChoice::vectorscope};

// Every id something in the window can ask for.
std::set<std::string> reachable_ids() {
    std::set<std::string> ids;
    for (const StageId s : all_stages) ids.insert(std::string(explain_id(s)));
    for (const CurveKind k : curve_kinds) ids.insert(std::string(explain_id(k)));
    for (const ScopeChoice s : all_scopes) ids.insert(std::string(explain_id(s)));
    for (const InputKind kind : {InputKind::image, InputKind::raw}) {
        for (const Preset& p : presets_for(kind)) ids.insert(explain_id(p));
    }
    for (const std::string_view id : other_explain_ids()) ids.insert(std::string(id));
    return ids;
}

TEST(Explanations, EveryControlHasAnEntry) {
    for (const std::string& id : reachable_ids()) {
        EXPECT_NE(explanation_for(id), nullptr) << id;
    }
}

TEST(Explanations, NoEntryIsUnusedAndNoIdRepeats) {
    const std::set<std::string> ids = reachable_ids();
    std::set<std::string> seen;
    for (const Explanation& e : explanations()) {
        EXPECT_TRUE(ids.count(std::string(e.id))) << e.id << " is not asked for by any control";
        EXPECT_TRUE(seen.insert(std::string(e.id)).second) << e.id << " appears twice";
    }
    EXPECT_EQ(seen.size(), ids.size());
}

TEST(Explanations, IdsOfTheEnumsAreDistinct) {
    std::set<std::string> ids;
    int count = 0;
    for (const StageId s : all_stages) { ids.insert(std::string(explain_id(s))); ++count; }
    for (const CurveKind k : curve_kinds) { ids.insert(std::string(explain_id(k))); ++count; }
    for (const ScopeChoice s : all_scopes) { ids.insert(std::string(explain_id(s))); ++count; }
    EXPECT_EQ(static_cast<int>(ids.size()), count);
}

TEST(Explanations, ThePickerExplainsItselfWithItsKeyAndHowToLeave) {
    const Explanation* e = explanation_for(explain::pick);
    ASSERT_NE(e, nullptr);
    EXPECT_LE(e->summary.size(), explain_summary_max);
    EXPECT_LE(e->detail.size(), explain_detail_max);
    EXPECT_NE(e->summary.find("(W)"), std::string_view::npos);
    EXPECT_NE(e->detail.find("Esc"), std::string_view::npos);
    EXPECT_NE(e->detail.find("the same small piece of the photo at every zoom"),
              std::string_view::npos);
    EXPECT_NE(e->detail.find("5 by 5 preview pixels"), std::string_view::npos);
    EXPECT_NE(e->detail.find("the photo's pixels at 100% and above"), std::string_view::npos);
    for (const std::string_view text : {e->summary, e->detail}) {
        EXPECT_EQ(std::string(text).find("rightness"), std::string::npos);
    }
}

TEST(Explanations, TheBrushExplainsItselfWithItsKeyAndHowToLeave) {
    const Explanation* e = explanation_for(explain::dodge_burn_brush);
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->summary.find("(B)"), std::string_view::npos);
    EXPECT_NE(e->detail.find("Esc"), std::string_view::npos);
    EXPECT_NE(e->detail.find("Space"), std::string_view::npos);
    // W leaves the Brush only where Pick neutral can start, so the text says where.
    EXPECT_NE(e->detail.find("W from White balance on"), std::string_view::npos);
}

TEST(Explanations, TheToneChipNamesEveryKeyOfTheBrush) {
    const Explanation* e = explanation_for(explain::dodge_burn_tone);
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->summary.find("(X)"), std::string_view::npos);
    for (const char* key : {"Alt", "]", "[", "Shift+[", "Shift+]", "M"}) {
        EXPECT_NE(e->detail.find(key), std::string_view::npos) << key;
    }
}

TEST(Explanations, UnknownIdsHaveNoEntryAndNoLiveLine) {
    EXPECT_EQ(explanation_for("nope"), nullptr);
    EXPECT_EQ(explanation_for(""), nullptr);
    EXPECT_EQ(live_line("nope", EditSettings{}), "");
}

TEST(Explanations, TextsAreWithinTheirLimitsAndFinished) {
    for (const Explanation& e : explanations()) {
        EXPECT_FALSE(e.summary.empty()) << e.id;
        EXPECT_FALSE(e.detail.empty()) << e.id;
        // Lengths in characters, not bytes: × and “ ” take two or three bytes.
        const auto chars = [](std::string_view s) {
            std::size_t n = 0;
            for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
            return n;
        };
        EXPECT_LE(chars(e.summary), explain_summary_max) << e.id;
        EXPECT_LE(chars(e.detail), explain_detail_max) << e.id;
        for (const std::string_view text : {e.summary, e.detail}) {
            for (const char* bad : {"{", "}", "TODO", "TBD", "XXX", "???", "(new)", "  "}) {
                EXPECT_EQ(text.find(bad), std::string_view::npos) << e.id << " has " << bad;
            }
        }
    }
}

TEST(Explanations, MultiplicationUsesTheSignNotTheLetter) {
    for (const Explanation& e : explanations()) {
        for (const std::string_view text : {e.summary, e.detail}) {
            for (std::size_t i = 1; i + 1 < text.size(); ++i) {
                if (text[i] == 'x' && (std::isdigit(static_cast<unsigned char>(text[i + 1])) ||
                                       (std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                                        text[i + 1] == ' '))) {
                    ADD_FAILURE() << e.id << ": ASCII x as a times sign in \"" << text << "\"";
                }
            }
        }
    }
    EXPECT_EQ(format_times(1.2f), "×1.20");
}

// A look's numbers are filled in from the recipes. What the texts still say in words must be true
// of the looks.
TEST(Explanations, LookTextsInWordsAgreeWithTheLooks) {
    const auto find = [](InputKind kind, std::string_view id) -> const Preset& {
        for (const Preset& p : presets_for(kind)) {
            if (p.id == id) return p;
        }
        ADD_FAILURE() << id;
        return presets_for(kind).front();
    };
    EXPECT_EQ(find(InputKind::raw, "natural").settings.exposure_stops, 1.0f);  // "A stop brighter"
    EXPECT_EQ(find(InputKind::raw, "natural").settings.curve, CurveKind::filmic);
    EXPECT_EQ(find(InputKind::raw, "vivid").settings.curve, CurveKind::filmic);
    EXPECT_EQ(find(InputKind::raw, "soft").settings.curve, CurveKind::reinhard);
    EXPECT_EQ(find(InputKind::raw, "bw").settings.saturation, 0.0f);  // "Saturation 0"
    EXPECT_EQ(find(InputKind::image, "bw").settings.saturation, 0.0f);
    // ... and the ranges the curve texts quote.
    EXPECT_NE(explanation_for("curve.white")->detail.find("Range 1 to 16, default 4"),
              std::string_view::npos);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::reinhard)->min, 1.0f);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::reinhard)->max, 16.0f);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::reinhard)->default_value, 4.0f);
    EXPECT_NE(explanation_for("curve.gamma_value")->detail.find("Range 0.5 to 4, default 2.2"),
              std::string_view::npos);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::gamma)->min, 0.5f);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::gamma)->max, 4.0f);
    EXPECT_FLOAT_EQ(curve_parameter(CurveKind::gamma)->default_value, 2.2f);
}

TEST(Explanations, LookDetailsShowTheRecipesNumbers) {
    EXPECT_EQ(explanation_for("look.natural")->detail,
              "RAW files are linear, so they look dark and flat as decoded. +1.00 stops and the "
              "filmic curve give a neutral starting point.");
    EXPECT_EQ(explanation_for("look.vivid")->detail,
              "Saturation ×1.25 and contrast ×1.15 for RAW (×1.20 and ×1.10 for JPEG and PNG). RAW "
              "also gets +1.00 stops and the filmic curve.");
    EXPECT_EQ(explanation_for("look.soft")->detail,
              "Saturation ×0.90 and contrast ×0.85 for RAW (×0.90 and ×0.90 for JPEG and PNG). For "
              "RAW the Reinhard curve rolls highlights off more softly than filmic.");
    EXPECT_EQ(explanation_for("look.bw")->detail,
              "Saturation 0 removes colour but keeps each pixel's luminance. Contrast ×1.20 (RAW) "
              "or ×1.15 (JPEG, PNG) stops the grey looking flat.");
}

TEST(Explanations, ChangingALookChangesTheNumbersInItsText) {
    const auto copy = [](InputKind kind) {
        const std::span<const Preset> presets = presets_for(kind);
        return std::vector<Preset>(presets.begin(), presets.end());
    };
    std::vector<Preset> raw = copy(InputKind::raw);
    std::vector<Preset> image = copy(InputKind::image);
    for (Preset& p : raw) {
        if (p.id == "vivid") {
            p.settings.exposure_stops = -0.5f;
            p.settings.saturation = 3.0f;
            p.settings.contrast = 2.5f;
        }
    }
    for (Preset& p : image) {
        if (p.id == "vivid") {
            p.settings.contrast = 0.75f;
        }
    }
    EXPECT_EQ(expand_look_text("{raw.vivid.exposure}, {raw.vivid.saturation}, {raw.vivid.contrast}, "
                               "{image.vivid.contrast}.",
                               raw, image),
              "-0.50 stops, ×3.00, ×2.50, ×0.75.");
    EXPECT_EQ(expand_look_text("No tokens.", raw, image), "No tokens.");
    for (const std::string_view bad : {"{raw.nothing.contrast}", "{raw.vivid.nothing}",
                                       "{other.vivid.contrast}", "{vivid}", "{raw.vivid.contrast"}) {
        EXPECT_THROW((void)expand_look_text(bad, raw, image), std::logic_error) << bad;
    }
}

// --- Live lines: the numbers are the engine's ---------------------------------------------------

EditSettings with_exposure_at(float position) {
    EditSettings s;
    apply_simple_control(s, SimpleControl::brightness, position);
    return s;
}

TEST(LiveLines, ExposureIsStopsAndTheFactorOnTheLight) {
    EXPECT_EQ(live_line("stage.exposure", with_exposure_at(35)), "+0.70 stops, 1.62× the light");
    EXPECT_EQ(live_line("stage.exposure", with_exposure_at(50)), "+1.00 stops, 2.00× the light");
    EXPECT_EQ(live_line("stage.exposure", with_exposure_at(-50)), "-1.00 stops, 0.50× the light");
    EXPECT_EQ(live_line("stage.exposure", EditSettings{}), "+0.00 stops, 1.00× the light");
}

TEST(Explanations, WhiteBalanceSaysWhatKelvinAndTintMeanAndNeverBrightness) {
    const Explanation* e = explanation_for("stage.white_balance");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(explain_id(StageId::white_balance), "stage.white_balance");
    const std::string text = std::string(e->summary) + " " + std::string(e->detail);
    for (const char* word : {"Kelvin", "light", "bluer", "warmed", "Tint", "green", "magenta",
                             "As shot"}) {
        EXPECT_NE(text.find(word), std::string::npos) << word;
    }
    for (const char* bad : {"brightness", "Brightness", "brighter", "darker"}) {
        EXPECT_EQ(text.find(bad), std::string::npos) << bad;
    }
    EXPECT_EQ(e->live, LiveKind::white_balance);
    // It sits with the other stages, in pipeline order.
    const std::span<const Explanation> all = explanations();
    const auto index_of = [&](std::string_view id) {
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i].id == id) {
                return i;
            }
        }
        return all.size();
    };
    EXPECT_EQ(index_of("stage.dodge_burn"), index_of("stage.exposure") + 1);
    EXPECT_EQ(index_of("stage.white_balance"), index_of("stage.dodge_burn") + 1);
    EXPECT_EQ(index_of("stage.matrix"), index_of("stage.white_balance") + 1);
}

TEST(LiveLines, WhiteBalanceSaysTheLightAndWhetherItIsAsShot) {
    LiveContext raw;
    raw.as_shot_white = white_of(WhiteBalance{5328, 26.7});
    const LiveContext plain;

    EditSettings s;
    EXPECT_EQ(live_line("stage.white_balance", s, raw),
              "As shot: 5330 K, tint +27, the light the camera balanced for");
    EXPECT_EQ(live_line("stage.white_balance", s, plain),
              "As shot: taken as 6500 K, as this file names no light");
    // As shot is no change on or off, so off adds nothing to say.
    s.white_balance_on = false;
    EXPECT_EQ(live_line("stage.white_balance", s, raw),
              "As shot: 5330 K, tint +27, the light the camera balanced for");

    s = EditSettings{};
    s.white_balance = WhiteBalance{5200, -10};
    EXPECT_EQ(live_line("stage.white_balance", s, raw),
              "Chosen: 5200 K, tint \xE2\x88\x92" "10. As shot is 5330 K, tint +27");
    EXPECT_EQ(live_line("stage.white_balance", s, plain),
              "Chosen: 5200 K, tint \xE2\x88\x92" "10. As shot is taken as 6500 K");
    s.white_balance_on = false;
    EXPECT_EQ(live_line("stage.white_balance", s, raw),
              "Off, so no change. When on: Chosen: 5200 K, tint \xE2\x88\x92"
              "10. As shot is 5330 K, tint +27");
}

TEST(LiveLines, WhiteBalanceReadsAsTheRowDoes) {
    // The same rounding as the numbers beside the row's sliders, for every light.
    for (const WhiteBalance wb : {WhiteBalance{5328, 26.7}, WhiteBalance{2004, -99.6},
                                  WhiteBalance{14996, 0.4}}) {
        LiveContext context;
        context.as_shot_white = white_of(wb);
        const LightPathRow::WhiteBalanceSliders sliders =
            *light_path_row({EditSettings{}, InputKind::raw, "RAW", final_stage, std::nullopt,
                             nullptr, context.as_shot_white},
                            StageId::white_balance)
                 .white_balance;
        const std::string row = sliders.kelvin_text + ", tint " + sliders.tint_text;
        const std::string numbers = row;
        EXPECT_NE(live_line("stage.white_balance", EditSettings{}, context).find(numbers),
                  std::string::npos)
            << row;
    }
}

TEST(LiveLines, ExposureFactorMatchesTheEngineOnAPixel) {
    for (const float position : {-80.0f, -35.0f, 10.0f, 35.0f, 90.0f}) {
        const EditSettings s = with_exposure_at(position);
        Image img;
        img.width = 1;
        img.height = 1;
        img.pixels = {0.25f, 0.25f, 0.25f};
        exposure(img, s.exposure_stops, 1);
        char expected[32];
        std::snprintf(expected, sizeof expected, "%.2f× the light", img.pixels[0] / 0.25f);
        EXPECT_NE(live_line("stage.exposure", s).find(expected), std::string::npos)
            << position << ": " << live_line("stage.exposure", s);
    }
}

TEST(LiveLines, ContrastLineShowsTheGradeAndTheMultiplier) {
    EditSettings s;
    s.contrast = std::pow(1.5f, 0.5f);
    EXPECT_EQ(live_line("stage.contrast", s),
              "Grade 2.8 · ×1.22, each stop near middle grey is 22% wider");
    s.contrast = std::pow(1.5f, -1.0f);
    EXPECT_EQ(live_line("stage.contrast", s),
              "Softer than grade 0 · ×0.67, each stop near middle grey is 33% narrower");
    s.contrast = 1.0f;
    EXPECT_EQ(live_line("stage.contrast", s), "Grade 2 · ×1.00, no change");
}

// "Each stop near middle grey is N% wider" is the engine's slope at 0.18.
TEST(LiveLines, ContrastPercentIsTheEnginesSlopeAtMiddleGrey) {
    for (const float position : {-100.0f, -40.0f, 25.0f, 50.0f, 100.0f}) {
        EditSettings s;
        s.contrast = std::pow(1.5f, position / 100.0f);
        const double h = 0.01;
        const double hi = std::log2(contrast_reference(static_cast<float>(0.18 * std::exp2(h)), s.contrast) / 0.18);
        const double lo = std::log2(contrast_reference(static_cast<float>(0.18 * std::exp2(-h)), s.contrast) / 0.18);
        const double slope = (hi - lo) / (2 * h);
        const long pct = std::lround(std::fabs(slope - 1.0) * 100.0);
        EXPECT_NE(live_line("stage.contrast", s).find(std::to_string(pct) + "% "), std::string::npos)
            << position << ": slope " << slope << " in " << live_line("stage.contrast", s);
    }
}

TEST(LiveLines, ColourIsTheScaleSaturationUses) {
    EditSettings s;
    apply_simple_control(s, SimpleControl::colour, 20);
    EXPECT_EQ(live_line("stage.saturation", s), "×1.20 colour");
    apply_simple_control(s, SimpleControl::colour, -100);
    EXPECT_EQ(live_line("stage.saturation", s), "black and white");
    // The engine: a colour's distance from its luminance is scaled by the same number.
    EditSettings scaled;
    apply_simple_control(scaled, SimpleControl::colour, 40);
    Image img;
    img.width = 1;
    img.height = 1;
    img.pixels = {0.5f, 0.3f, 0.2f};
    const float y = 0.2126f * 0.5f + 0.7152f * 0.3f + 0.0722f * 0.2f;
    saturation(img, scaled.saturation, 1);
    EXPECT_NEAR((img.pixels[0] - y) / (0.5f - y), 1.4f, 1e-4f);
    EXPECT_NE(live_line("stage.saturation", scaled).find("×1.40"), std::string::npos);
}

TEST(LiveLines, AStageThatIsOffSaysSoInItsRow) {
    EditSettings s = with_exposure_at(35);
    s.exposure_on = false;
    EXPECT_EQ(live_line("stage.exposure", s),
              "Off, so no change. When on: +0.70 stops, 1.62× the light");
    s.exposure_on = true;
    EXPECT_EQ(live_line("stage.exposure", s), "+0.70 stops, 1.62× the light");
}

TEST(LiveLines, TheControlsMoveWhatTheRowsSay) {
    EditSettings s;
    s.contrast = std::pow(1.5f, 0.3f);
    apply_simple_control(s, SimpleControl::colour, -20);
    EXPECT_NE(live_line("stage.contrast", s).find(" · ×1.13,"), std::string::npos);
    EXPECT_EQ(live_line("stage.saturation", s), "×0.80 colour");
    s.saturation = 0.0f;
    EXPECT_EQ(live_line("stage.saturation", s), "black and white");
}

TEST(LiveLines, CurvesGiveTheGreyCardOutputsOfTheEnginesCurves) {
    EditSettings s;
    s.gamma = 2.2f;
    EXPECT_EQ(live_line("curve.gamma", s), "gamma 2.2: 18% grey becomes 46%");
    s.reinhard_white = 4.0f;
    EXPECT_EQ(live_line("curve.reinhard", s),
              "white 4: a scene value of 4 maps to white; 18% grey becomes 15%");
    EXPECT_EQ(live_line("curve.filmic", s), "18% grey becomes 13%");
    EXPECT_EQ(live_line("curve.white", s), "white = 4");
    EXPECT_EQ(live_line("curve.gamma_value", s), "gamma 2.2: 18% grey becomes 46%");
    EXPECT_EQ(live_line("curve.none", s), "");
    // Straight from the curves themselves, over a few values.
    for (const float gamma : {0.5f, 1.0f, 1.8f, 2.2f, 4.0f}) {
        s.gamma = gamma;
        const int expected = static_cast<int>(
            std::lround(parse_curve("gamma:" + std::to_string(gamma)).map(0.18f) * 100.0));
        EXPECT_NE(live_line("curve.gamma", s).find("becomes " + std::to_string(expected) + "%"),
                  std::string::npos)
            << gamma;
    }
    for (const float white : {1.0f, 2.0f, 4.0f, 16.0f}) {
        s.reinhard_white = white;
        const int expected = static_cast<int>(
            std::lround(parse_curve("reinhard:" + std::to_string(white)).map(0.18f) * 100.0));
        EXPECT_NE(live_line("curve.reinhard", s).find("becomes " + std::to_string(expected) + "%"),
                  std::string::npos)
            << white;
    }
}

TEST(LiveLines, TheCurveRowNamesTheChosenCurve) {
    EditSettings s;
    EXPECT_EQ(live_line("stage.curve", s), "none");
    set_curve_kind(s, CurveKind::filmic);
    EXPECT_EQ(live_line("stage.curve", s), "filmic: 18% grey becomes 13%");
    set_curve_kind(s, CurveKind::gamma);
    EXPECT_EQ(live_line("stage.curve", s), "gamma 2.2: 18% grey becomes 46%");
    s.curve_on = false;
    EXPECT_EQ(live_line("stage.curve", s),
              "Off, so no change. When on: gamma 2.2: 18% grey becomes 46%");
}

TEST(LiveLines, TheLookLineNamesThePresetOrCustom) {
    EditSettings s;
    LiveContext context;
    context.kind = InputKind::raw;
    apply_preset(s, presets_for(InputKind::raw)[2]);  // Vivid
    EXPECT_EQ(live_line("look", s, context),
              "Vivid: +1.00 stops, colour ×1.25, contrast ×1.15, filmic");
    s.exposure_stops = 0.3f;
    EXPECT_EQ(live_line("look", s, context).substr(0, 8), "Custom: ");
}

const Preset& look(InputKind kind, std::string_view id) {
    for (const Preset& p : presets_for(kind)) {
        if (p.id == id) return p;
    }
    ADD_FAILURE() << id;
    return presets_for(kind).front();
}

TEST(LookValues, EachBuiltInLookReadsLikeThis) {
    const struct {
        InputKind kind;
        std::string_view id;
        std::string_view line;
    } rows[] = {
        {InputKind::raw, "natural", "+1.00 stops, filmic"},
        {InputKind::raw, "original", "no adjustments"},
        {InputKind::raw, "vivid", "+1.00 stops, colour ×1.25, contrast ×1.15, filmic"},
        {InputKind::raw, "soft", "+1.00 stops, colour ×0.90, contrast ×0.85, Reinhard (white 4)"},
        {InputKind::raw, "bw", "+1.00 stops, black and white, contrast ×1.20, filmic"},
        {InputKind::image, "original", "no adjustments"},
        {InputKind::image, "vivid", "colour ×1.20, contrast ×1.10"},
        {InputKind::image, "soft", "colour ×0.90, contrast ×0.90"},
        {InputKind::image, "bw", "black and white, contrast ×1.15"},
    };
    std::size_t count = 0;
    for (const auto& row : rows) {
        EXPECT_EQ(look_values_line(look(row.kind, row.id).settings), row.line) << row.id;
        ++count;
    }
    EXPECT_EQ(count, presets_for(InputKind::raw).size() + presets_for(InputKind::image).size());
}

TEST(LookValues, TheExplainLineSaysTheSame) {
    for (const InputKind kind : {InputKind::image, InputKind::raw}) {
        LiveContext context;
        context.kind = kind;
        for (const Preset& p : presets_for(kind)) {
            EXPECT_EQ(live_line("look", p.settings, context),
                      std::string(p.name) + ": " + look_values_line(p.settings))
                << p.id;
        }
        EditSettings custom = presets_for(kind).front().settings;
        custom.exposure_stops = 0.3f;
        EXPECT_EQ(live_line("look", custom, context), "Custom: " + look_values_line(custom));
    }
}

TEST(LookValues, AStageThatIsOffChangesNothing) {
    EditSettings s = look(InputKind::raw, "vivid").settings;
    s.saturation_on = false;
    EXPECT_EQ(look_values_line(s), "+1.00 stops, contrast ×1.15, filmic");
    s.exposure_on = s.contrast_on = s.curve_on = false;
    EXPECT_EQ(look_values_line(s), "no adjustments");
    EXPECT_EQ(matching_preset(InputKind::raw, s)->id, "original");
}

TEST(LookValues, AGammaCurveShowsItsNumber) {
    EditSettings s;
    s.curve = CurveKind::gamma;
    s.gamma = 2.2f;
    EXPECT_EQ(look_values_line(s), "gamma 2.2");
}

TEST(LiveLines, MatrixShowsTheFileNameOrNone) {
    EditSettings s;
    EXPECT_EQ(live_line("stage.matrix", s), "none");
}

TEST(LiveLines, ARecipeMatrixIsLabelledAsOne) {
    EditSettings s;
    s.matrix = MatrixFile{{}, identity_matrix};
    s.matrix->values[0] = 1.05f;
    EXPECT_EQ(live_line("stage.matrix", s), "From recipe");
}

TEST(LiveLines, TheLoadedRecipeLineNamesTheStagesItSets) {
    Recipe recipe;
    recipe.matrix = RecipeMatrix{};
    recipe.saturation = RecipeSaturation{0.9f, true};
    recipe.contrast = RecipeContrast{0.85f, false};
    const LoadedRecipe loaded = make_loaded_recipe(recipe, "grade.prism", {}, {});
    LiveContext context;
    context.recipe = &loaded;
    EXPECT_EQ(live_line("recipe.loaded", EditSettings{}, context),
              "Sets only Colour matrix, Saturation and Contrast, and switches Contrast off; the "
              "other stages are unchanged.");
}

TEST(LiveLines, WithoutALoadedRecipeTheRecipeLineIsEmpty) {
    EXPECT_EQ(live_line("recipe.loaded", EditSettings{}), "");
    EXPECT_EQ(live_line("recipe.save", EditSettings{}), "");
}

TEST(LiveLines, ViewingNamesTheStage) {
    LiveContext context;
    context.viewed = StageId::exposure;
    EXPECT_EQ(live_line("scope.parade", EditSettings{}, context), "Viewing: after Exposure");
    context.viewed = StageId::decode;
    EXPECT_EQ(live_line("scope.histogram", EditSettings{}, context), "Viewing: Original");
}

TEST(LiveLines, CompareNamesBothHalves) {
    LiveContext context;
    context.viewed = StageId::encode;
    EXPECT_NE(live_line("compare", EditSettings{}, context).find("No comparison"), std::string::npos);
    context.compare = StageId::decode;
    EXPECT_EQ(live_line("compare", EditSettings{}, context),
              "Left: Original. Right: after Output encode.");
}

TEST(LiveLines, ChipsShowTheShareOfEachChannel) {
    StageHistogram h;
    h.pixel_count = 1000;
    h.red_clips.over_white = 10;
    h.green_clips.over_white = 1;
    h.blue_clips.under_black = 250;
    LiveContext context;
    EXPECT_EQ(live_line("chip.over", EditSettings{}, context), "");  // no histogram yet
    context.histogram = &h;
    EXPECT_EQ(live_line("chip.over", EditSettings{}, context), "R 1.00%   G 0.10%   B 0.00%");
    EXPECT_EQ(live_line("chip.under", EditSettings{}, context), "R 0.00%   G 0.00%   B 25.00%");
}

TEST(LiveLines, TheClipNoteLineIsTheWholeSentence) {
    ClipReport report;
    LiveContext context;
    EXPECT_EQ(live_line("clip.note", EditSettings{}, context), "");
    context.clips = &report;
    EXPECT_EQ(live_line("clip.note", EditSettings{}, context), describe_clipping(report));
}

TEST(LiveLines, TheStripStepIsTheFactorBetweenBands) {
    LiveContext context;
    context.strip_step = StripStep::third;
    EXPECT_EQ(live_line("strip", EditSettings{}, context),
              "Steps of 1/3 stop: each band is 1.26× the light of the one before");
    context.strip_step = StripStep::half;
    EXPECT_NE(live_line("strip", EditSettings{}, context).find("1.41×"), std::string::npos);
    context.strip_step = StripStep::whole;
    EXPECT_NE(live_line("strip", EditSettings{}, context).find("2.00×"), std::string::npos);
}

TEST(Explanations, TheZoomTextNamesEachGesture) {
    const Explanation* zoom = explanation_for(explain::view_zoom);
    ASSERT_NE(zoom, nullptr);
    for (const char* word : {"Pinch", "wheel", "scroll", "drag"}) {
        EXPECT_NE(zoom->detail.find(word), std::string_view::npos) << word;
    }
}

TEST(LiveLines, ThePreviewSizeSaysHowManyFilePixelsEachStandsFor) {
    LiveContext context;
    context.preview_long_side = 2560;
    context.file_long_side = 9566;
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview 2560 px, file 9566 px on the long side: each preview pixel stands for about "
              "3.7 file pixels across");
}

TEST(LiveLines, AnEstimatedFileSizeSaysAbout) {
    LiveContext context;
    context.preview_long_side = 2560;
    context.file_long_side = 6000;
    context.file_size_exact = false;
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview 2560 px, file about 6000 px on the long side: each preview pixel stands for "
              "about 2.3 file pixels across");
}

TEST(LiveLines, APreviewOfTheWholeFileSaysOneToOne) {
    LiveContext context;
    context.preview_long_side = 1600;
    context.file_long_side = 1600;
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview and file are both 1600 px on the long side: each preview pixel is one file "
              "pixel");
}

TEST(LiveLines, TheZoomSaysWhatTheCanvasShowsZoomedIn) {
    LiveContext context;
    context.preview_long_side = 2560;
    context.file_long_side = 9566;
    context.view_state = real_pixels_shown_explain();
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview 2560 px, file 9566 px on the long side: each preview pixel stands for about "
              "3.7 file pixels across. Showing the photo's own pixels.");
    context.view_state = preview_chip_explain(PreviewChip::reading);
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview 2560 px, file 9566 px on the long side: each preview pixel stands for about "
              "3.7 file pixels across. Showing the preview, enlarged: the full-size photo is still "
              "being read.");
    context.measures_frame = true;  // framed: what the canvas shows replaces the softness clause
    EXPECT_EQ(
        live_line(explain::view_zoom, EditSettings{}, context),
        "Preview 2560 px, frame 9566 px on the long side: each preview pixel stands for "
        "about 3.7 frame pixels across. Showing the preview, enlarged: the full-size photo is "
        "still being read.");
    context.measures_frame = false;
    context.preview_long_side = 1600;
    context.file_long_side = 1600;
    context.view_state = real_pixels_shown_explain();
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview and file are both 1600 px on the long side: each preview pixel is one file "
              "pixel. Showing the photo's own pixels.");
}

TEST(Explanations, TheZoomAndHundredPercentSpeakOfThePhotosOwnPixels) {
    const Explanation* zoom = explanation_for(explain::view_zoom);
    ASSERT_NE(zoom, nullptr);
    EXPECT_EQ(zoom->summary, "How large the photo is on the screen, in % of its own pixels.");
    EXPECT_EQ(zoom->detail,
              "Pinch or use the wheel to zoom; scroll or drag to move. 100% is one pixel of the "
              "photo on each pixel of your screen, up to 800%. Zoomed in, the photo's own pixels "
              "replace the preview where they can.");
    const Explanation* actual = explanation_for(explain::view_actual);
    ASSERT_NE(actual, nullptr);
    EXPECT_EQ(actual->summary, "One pixel of the photo on each pixel of your screen (1).");
    // The words of real-pixel zoom's own explanation, then when the button is greyed out.
    EXPECT_EQ(actual->detail.substr(0, real_pixels_explain().size()), real_pixels_explain());
    EXPECT_EQ(actual->detail.substr(real_pixels_explain().size()),
              " Greyed out when the whole photo already fits above 100%.");
}

TEST(LiveLines, ThePreviewSizeIsEmptyWithoutAPhoto) {
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, LiveContext{}), "");
}

TEST(LiveLines, LiveLinesHaveNoPlaceholdersAndUseTheTimesSign) {
    EditSettings s = with_exposure_at(12);
    s.contrast = std::pow(1.5f, -0.33f);
    set_curve_kind(s, CurveKind::reinhard);
    StageHistogram h;
    h.pixel_count = 10;
    ClipReport report;
    LiveContext context;
    context.histogram = &h;
    context.clips = &report;
    context.compare = StageId::decode;
    Recipe recipe;
    recipe.saturation = RecipeSaturation{0.9f, true};
    const LoadedRecipe loaded = make_loaded_recipe(recipe, "r.prism", s, s);
    context.recipe = &loaded;
    context.preview_long_side = 2560;
    context.file_long_side = 9566;
    context.file_width = 9566;
    context.file_height = 6374;
    Image pixel;
    pixel.width = 1;
    pixel.height = 1;
    pixel.pixels = {0.4f, 0.3f, 0.2f};
    const std::optional<PixelTrace> trace = sample_trace(pixel, s, 0, 0);
    ASSERT_TRUE(trace.has_value());
    context.sample = &*trace;
    for (const Explanation& e : explanations()) {
        const std::string line = live_line(e.id, s, context);
        EXPECT_EQ(line.find('{'), std::string::npos) << e.id;
        EXPECT_EQ(line.find("nan"), std::string::npos) << e.id;
        EXPECT_EQ(line.find("inf"), std::string::npos) << e.id;
        if (e.live == LiveKind::none) EXPECT_TRUE(line.empty()) << e.id;
        else EXPECT_FALSE(line.empty()) << e.id;
        for (std::size_t i = 1; i + 1 < line.size(); ++i) {
            if (line[i] == 'x' && std::isdigit(static_cast<unsigned char>(line[i + 1]))) {
                ADD_FAILURE() << e.id << ": ASCII x as a times sign in \"" << line << "\"";
            }
        }
    }
}

TEST(Explanations, TheSampleEntriesAvoidTheWordBrightness) {
    for (const std::string_view id :
         {explain::sample, explain::sample_values, explain::sample_light, explain::sample_range}) {
        const Explanation* e = explanation_for(id);
        ASSERT_NE(e, nullptr) << id;
        EXPECT_EQ(e->summary.find("rightness"), std::string_view::npos) << id;
        EXPECT_EQ(e->detail.find("rightness"), std::string_view::npos) << id;
    }
    EXPECT_EQ(explanation_for(explain::sample)->live, LiveKind::sample);
    // The weights in the text are the ones the code uses.
    EXPECT_NE(explanation_for(explain::sample_light)->detail.find("0.2126 R + 0.7152 G + 0.0722 B"),
              std::string_view::npos);
}

// Prism's wording is "N% of white's light", never this word: no explanation may use it, in any
// casing, in its text or in a live line for any of these settings.
TEST(Explanations, NoTextEverSaysBrightness) {
    const auto lower = [](std::string_view text) {
        std::string out(text);
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };
    const auto clean = [&](std::string_view text) {
        return lower(text).find("brightness") == std::string::npos;
    };
    ASSERT_FALSE(clean("How many pixels sit at each BrightNess."));  // the check itself works

    std::vector<EditSettings> settings(1);
    settings.push_back(with_exposure_at(40));
    settings.push_back(with_exposure_at(-100));
    for (const CurveKind kind :
         {CurveKind::none, CurveKind::gamma, CurveKind::reinhard, CurveKind::filmic}) {
        EditSettings s;
        s.contrast = std::pow(1.5f, 0.33f);
        apply_simple_control(s, SimpleControl::colour, -50);
        set_curve_kind(s, kind);
        settings.push_back(s);
    }
    EditSettings all_off;
    all_off.exposure_on = false;
    settings.push_back(all_off);

    StageHistogram h;
    h.pixel_count = 1000;
    h.any_under_black = 5;
    h.any_over_white = 70;
    h.red_clips = {5, 70};
    Image base;
    base.width = 2;
    base.height = 1;
    base.pixels = {1.0f, 0.5f, 0.2f, 0.4f, 0.3f, 0.2f};
    EditSettings pushed;
    pushed.exposure_stops = 2.0f;
    const ClipReport report = compute_clip_report(base, pushed, 1);
    Recipe recipe;
    recipe.saturation = RecipeSaturation{0.9f, true};
    const LoadedRecipe loaded = make_loaded_recipe(recipe, "r.prism", pushed, pushed);
    const std::optional<PixelTrace> trace = sample_trace(base, pushed, 0, 0);
    ASSERT_TRUE(trace.has_value());

    std::vector<LiveContext> contexts(1);
    for (const InputKind kind : {InputKind::image, InputKind::raw}) {
        for (const StageId viewed : {StageId::decode, StageId::exposure, final_stage}) {
            LiveContext context;
            context.kind = kind;
            context.viewed = viewed;
            context.compare = StageId::decode;
            context.histogram = &h;
            context.clips = &report;
            context.recipe = &loaded;
            context.preview_long_side = 2560;
            context.file_long_side = 9566;
            context.sample = &*trace;
            contexts.push_back(context);
        }
    }

    EXPECT_TRUE(clean(readout_overlay_note)) << readout_overlay_note;
    EXPECT_NE(readout_overlay_note.find("J "), std::string_view::npos);

    for (const Explanation& e : explanations()) {
        EXPECT_TRUE(clean(e.summary)) << e.id << ": " << e.summary;
        EXPECT_TRUE(clean(e.detail)) << e.id << ": " << e.detail;
        for (const EditSettings& s : settings) {
            for (const LiveContext& context : contexts) {
                const std::string line = live_line(e.id, s, context);
                EXPECT_TRUE(clean(line)) << e.id << ": " << line;
            }
        }
    }
}

TEST(LiveLines, TheSampleLineIsTheReadoutOfThePixel) {
    Image pixel;
    pixel.width = 1;
    pixel.height = 1;
    pixel.pixels = {0.42f, 0.27f, 0.19f};
    const std::optional<PixelTrace> trace = sample_trace(pixel, EditSettings{}, 0, 0);
    ASSERT_TRUE(trace.has_value());
    LiveContext context;
    EXPECT_EQ(live_line(explain::sample, EditSettings{}, context), "");  // nothing sampled yet
    context.sample = &*trace;
    context.viewed = StageId::encode;
    EXPECT_EQ(live_line(explain::sample, EditSettings{}, context),
              describe_sample(*trace, StageId::encode));
    context.compare = StageId::decode;
    context.preview_long_side = 2560;
    context.file_long_side = 9566;
    EXPECT_EQ(live_line(explain::sample, EditSettings{}, context),
              describe_sample(*trace, StageId::encode, StageId::decode, {2560, 9566}));
    // The same number of file pixels across as the zoom's own line.
    const std::string zoom = live_line(explain::view_zoom, EditSettings{}, context);
    EXPECT_NE(zoom.find("about 3.7 file pixels across"), std::string::npos);
    EXPECT_NE(live_line(explain::sample, EditSettings{}, context).find("about 3.7×3.7 file pixels"),
              std::string::npos);
}

// The Explain area's height at an interface size, whichever entry `id` is shown ("" is the
// hint). The height takes no text, so `id` is deliberately unused: this is the whole of what the
// window can ask of it.
float area_height_for(std::string_view /*id*/, float scale, float panel_height) {
    const TokenSet& set = token_set(Theme::dark);
    const TokenMetrics m = scaled(set.metrics, scale);
    const float line = scaled(set.type, scale).small;
    return explain_area_height(m, line, panel_height);
}

TEST(ExplainAreaHeight, IsTheSameForEveryEntryAndTheHintAtEveryInterfaceSize) {
    for (const float scale : {0.75f, 1.0f, 2.0f}) {
        const float hint = area_height_for("", scale, 2000.0f);
        EXPECT_GT(hint, 0.0f) << scale;
        for (const Explanation& e : explanations()) {
            EXPECT_EQ(area_height_for(e.id, scale, 2000.0f), hint) << e.id << " at " << scale;
        }
        for (const std::string_view id : other_explain_ids()) {
            EXPECT_EQ(area_height_for(id, scale, 2000.0f), hint) << id << " at " << scale;
        }
    }
}

TEST(ExplainAreaHeight, IsFiveLinesAndGrowsWithTheInterfaceSize) {
    const TokenSet& set = token_set(Theme::dark);
    float previous = 0.0f;
    for (const float scale : {0.75f, 1.0f, 2.0f}) {
        const TokenMetrics m = scaled(set.metrics, scale);
        const float line = scaled(set.type, scale).small;
        const float h = explain_area_height(m, line, 5000.0f);
        EXPECT_FLOAT_EQ(h, 2 * m.explain_pad.y + 2 * m.window_border + 5 * line + 4 * m.explain_gap);
        EXPECT_GT(h, previous);
        previous = h;
    }
}

TEST(ExplainAreaHeight, NeverTakesMoreThanAShortPanelsShare) {
    const TokenSet& set = token_set(Theme::dark);
    EXPECT_FLOAT_EQ(explain_area_height(set.metrics, set.type.small, 100.0f), 45.0f);
}

TEST(ExplainToShow, HoverBeatsFocusBeatsTheLastEntry) {
    EXPECT_EQ(explain_to_show("a", "b", "c"), "a");
    EXPECT_EQ(explain_to_show("", "b", "c"), "b");
    EXPECT_EQ(explain_to_show("", "", "c"), "c");
}

TEST(ExplainToShow, NothingEverHoveredOrFocusedShowsTheHint) {
    EXPECT_EQ(explain_to_show("", "", ""), "");
}

// The window's frame loop: the result becomes the next `last` when it is not empty.
struct Frames {
    std::string last;
    std::string frame(std::string_view hovered, std::string_view focused) {
        const std::string shown(explain_to_show(hovered, focused, last));
        if (!shown.empty()) {
            last = shown;
        }
        return shown;
    }
};

TEST(ExplainToShow, AGapAfterAHoverKeepsTheHoveredEntry) {
    Frames f;
    EXPECT_EQ(f.frame("", ""), "");
    EXPECT_EQ(f.frame("stage.exposure", ""), "stage.exposure");
    EXPECT_EQ(f.frame("", ""), "stage.exposure");  // over a gap
    EXPECT_EQ(f.frame("", ""), "stage.exposure");  // and still
}

TEST(ExplainToShow, FocusWinsOverAStaleHoverAndANewHoverWinsOverFocus) {
    Frames f;
    EXPECT_EQ(f.frame("stage.exposure", ""), "stage.exposure");
    EXPECT_EQ(f.frame("", "stage.curve"), "stage.curve");  // the hover is stale: focus wins
    EXPECT_EQ(f.frame("", ""), "stage.curve");             // a gap keeps it
    EXPECT_EQ(f.frame("stage.matrix", "stage.curve"), "stage.matrix");  // a new hover wins
    EXPECT_EQ(f.frame("", "stage.curve"), "stage.curve");
}

}  // namespace
}  // namespace prism::gui

namespace prism::gui {
namespace {

TEST(Explanations, TheLookTooltipSaysALookKeepsYourDodgingAndBurning) {
    const Explanation* e = explanation_for(explain::look);
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->summary.find("Keeps your dodging and burning"), std::string_view::npos);
}

TEST(Explanations, DodgeAndBurnExplainsTheRowItsSwitchBrushStrengthAndClear) {
    for (const std::string_view id :
         {explain_id(StageId::dodge_burn), explain::dodge_burn_switch, explain::dodge_burn_brush,
          explain::dodge_burn_tone,
          explain::dodge_burn_strength, explain::dodge_burn_clear}) {
        const Explanation* e = explanation_for(id);
        ASSERT_NE(e, nullptr) << id;
        EXPECT_FALSE(e->summary.empty()) << id;
        for (const std::string_view text : {e->summary, e->detail}) {
            for (const char* bad : {"rightness", "brighter", "darker", "brighten", "darken"}) {
                EXPECT_EQ(text.find(bad), std::string_view::npos) << id << " has " << bad;
            }
        }
    }
    // The darkroom's words.
    const std::string_view row = explanation_for(explain_id(StageId::dodge_burn))->detail;
    EXPECT_NE(row.find("enlarger"), std::string_view::npos);
    EXPECT_NE(row.find("stops"), std::string_view::npos);
}

TEST(LiveLines, AFramedPreviewIsMeasuredAgainstTheFrame) {
    // With a frame the preview is the framed picture: its pixels stand for the frame's, and a tight
    // frame previews softer than it exports (#58, open question 2) wherever the preview is shown;
    // zoomed in (#37) view_state says what the canvas shows instead.
    LiveContext context;
    context.preview_long_side = 1600;
    context.file_long_side = 5980;
    context.measures_frame = true;
    EXPECT_EQ(
        live_line(explain::view_zoom, EditSettings{}, context),
        "Preview 1600 px, frame 5980 px on the long side: each preview pixel stands for about "
        "3.7 frame pixels across, so a tight frame looks softer here than it exports");
    context.file_size_exact = false;
    EXPECT_EQ(
        live_line(explain::view_zoom, EditSettings{}, context),
        "Preview 1600 px, frame about 5980 px on the long side: each preview pixel stands for "
        "about 3.7 frame pixels across, so a tight frame looks softer here than it exports");
    context.file_size_exact = true;
    context.file_long_side = 1600;
    EXPECT_EQ(live_line(explain::view_zoom, EditSettings{}, context),
              "Preview and frame are both 1600 px on the long side: each preview pixel is one frame "
              "pixel");
}

TEST(LiveLines, TheEaselSaysTheFrameAgainstThePhoto) {
    LiveContext context;
    context.file_width = 6000;
    context.file_height = 4000;
    EditSettings s;
    EXPECT_EQ(live_line(explain::easel, s, context), "The whole photo, upright: no frame.");
    prism::Frame frame;
    frame.left = 0.25f;
    frame.right = 0.75f;
    s.frame = frame;
    EXPECT_EQ(live_line(explain::easel, s, context),
              "Frame 3000 \xC3\x97 4000 of 6000 \xC3\x97 4000 (50% of the photo), upright.");
    context.file_size_exact = false;
    EXPECT_EQ(live_line(explain::framing_done, s, context),
              "Frame about 3000 \xC3\x97 4000 of about 6000 \xC3\x97 4000 (50% of the photo), "
              "upright.");
    // No photo: nothing to say.
    EXPECT_EQ(live_line(explain::easel, s, LiveContext{}), "");
}

}  // namespace
}  // namespace prism::gui
