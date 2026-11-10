#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "prism/gui/explanations.hpp"
#include "prism/gui/light_path.hpp"

using namespace prism::gui;

namespace {

using prism::Chromaticity;
using prism::WhiteBalance;
using prism::white_of;

constexpr StageId all_stages[] = {StageId::decode,   StageId::exposure,   StageId::white_balance,
                                  StageId::matrix,   StageId::saturation, StageId::contrast,
                                  StageId::curve,    StageId::encode};
constexpr ClipFate all_fates[] = {ClipFate::none, ClipFate::recoverable, ClipFate::lost};

// A row's sizes that no two sums can mistake for each other.
constexpr LightPathMetrics metrics{.pad_top = 11.0f,
                                   .pad_bottom = 13.0f,
                                   .line = 18.0f,
                                   .secondary = 15.0f,
                                   .slider = 20.0f,
                                   .tabs = 37.0f,
                                   .gap = 5.0f};

StageClip clip_with(StageId stage, ClipFate fate) {
    StageClip clip;
    clip.stage = stage;
    clip.ran = true;
    clip.over.any = clip.under.any = clip.gamut.any = 70;
    clip.over_fate = clip.under_fate = clip.gamut_fate = fate;
    return clip;
}

ClipReport report_with(ClipFate fate) {
    ClipReport report;
    report.sample_count = 1000;
    // Every stage the report holds, the hidden ones too: all_stages has only the visible ones.
    for (const StageInfo& info : pipeline_stages()) {
        report.stages[static_cast<std::size_t>(info.id)] = clip_with(info.id, fate);
    }
    return report;
}

LightPathRow row_for(StageId stage, const EditSettings& s, const ClipReport* clips = nullptr,
                     StageId viewed = final_stage, std::optional<StageId> compare = {},
                     InputKind kind = InputKind::raw) {
    return light_path_row({s, kind, "RAW", viewed, compare, clips, std::nullopt}, stage);
}

// The White balance row of a photo whose as-shot light is `as_shot`.
LightPathRow white_balance_row(const EditSettings& s, std::optional<Chromaticity> as_shot = {}) {
    return light_path_row(
        {s, InputKind::raw, "RAW", final_stage, std::nullopt, nullptr, as_shot},
        StageId::white_balance);
}

float height_of(const LightPathRow& row) {
    return light_path_row_layout(row, metrics).height;
}

// `burns` strokes that darken and `dodges` that lighten, as one stroke each.
Strokes strokes_of(std::size_t burns, std::size_t dodges) {
    auto field = std::make_shared<prism::StrokeField>();
    field->aspect = 1.5f;
    for (std::size_t i = 0; i < burns + dodges; ++i) {
        field->strokes.push_back(std::make_shared<const prism::FieldStroke>(prism::FieldStroke{
            {{0.5f, 0.5f}}, 0.05f, 0.5f, i < burns ? -0.5f : 0.5f}));
    }
    return Strokes(std::move(field));
}

EditSettings with_strokes(std::size_t burns, std::size_t dodges) {
    EditSettings s;
    s.strokes = strokes_of(burns, dodges);
    return s;
}

}  // namespace

// --- Heights ------------------------------------------------------------------------------------

TEST(LightPathLayout, ARowsHeightIsTheSameWithAndWithoutClipDataForEveryFate) {
    const EditSettings s;
    for (const StageId stage : all_stages) {
        const float without = height_of(row_for(stage, s));
        const LightPathRowLayout layout = light_path_row_layout(row_for(stage, s), metrics);
        for (const ClipFate fate : all_fates) {
            const ClipReport report = report_with(fate);
            const LightPathRow row = row_for(stage, s, &report);
            EXPECT_EQ(row.marks.empty(), fate == ClipFate::none) << static_cast<int>(stage);
            EXPECT_EQ(height_of(row), without) << static_cast<int>(stage);
            EXPECT_EQ(light_path_row_layout(row, metrics), layout) << static_cast<int>(stage);
        }
        // An empty report, and each direction alone.
        const ClipReport empty;
        EXPECT_EQ(height_of(row_for(stage, s, &empty)), without);
        for (const ClipFate fate : all_fates) {
            ClipReport one = report_with(ClipFate::none);
            one.stages[static_cast<std::size_t>(stage)].under_fate = fate;
            EXPECT_EQ(height_of(row_for(stage, s, &one)), without);
            one = report_with(ClipFate::none);
            one.stages[static_cast<std::size_t>(stage)].over_fate = fate;
            EXPECT_EQ(height_of(row_for(stage, s, &one)), without);
            one = report_with(ClipFate::none);
            one.stages[static_cast<std::size_t>(stage)].gamut_fate = fate;
            EXPECT_EQ(height_of(row_for(stage, s, &one)), without);
        }
    }
}

TEST(LightPathLayout, ARowsHeightDoesNotFollowItsValuesOrItsState) {
    const EditSettings plain;
    for (const StageId stage : all_stages) {
        const float expected = height_of(row_for(stage, plain));

        EditSettings off = plain;
        set_stage_enabled(off, stage, false);
        EXPECT_EQ(height_of(row_for(stage, off)), expected) << "off";

        // Values at their widest: past the sliders' ends, a matrix, a long file name.
        EditSettings extreme = plain;
        extreme.exposure_stops = 9.0f;
        extreme.saturation = 0.0f;
        extreme.contrast = 7.0f;
        extreme.white_balance = WhiteBalance{15000, -100};
        extreme.matrix = MatrixFile{};
        extreme.matrix->path = "a very long file name for a colour matrix that goes on and on.txt";
        extreme.raw_camera_matrix = MatrixFile{};
        EXPECT_EQ(height_of(row_for(stage, extreme)), expected) << "values";

        // Viewed, dimmed (viewing an earlier stage) and compared.
        for (const StageId viewed : all_stages) {
            EXPECT_EQ(height_of(row_for(stage, plain, nullptr, viewed)), expected) << "viewed";
            EXPECT_EQ(height_of(row_for(stage, plain, nullptr, viewed, StageId::contrast)),
                      expected)
                << "compared";
        }
        EXPECT_EQ(height_of(row_for(stage, plain, nullptr, final_stage, {}, InputKind::image)),
                  expected)
            << "kind";
    }
}

TEST(LightPathLayout, TheClipLineIsAlwaysReserved) {
    const EditSettings s;
    for (const StageId stage : all_stages) {
        const LightPathRow row = row_for(stage, s);
        ASSERT_TRUE(row.marks.empty());
        const LightPathRowLayout layout = light_path_row_layout(row, metrics);
        EXPECT_GE(layout.clip_height, metrics.secondary);
        EXPECT_EQ(layout.clip_y + layout.clip_height + metrics.pad_bottom, layout.height);
    }
}

TEST(LightPathLayout, OnlyAChoiceOfCurveOpensARow) {
    EditSettings s;
    float closed = 0;
    float open = 0;
    for (const CurveKind kind : curve_kinds) {
        set_curve_kind(s, kind);
        const LightPathRow row = row_for(StageId::curve, s);
        const bool takes_a_number = kind == CurveKind::gamma || kind == CurveKind::reinhard;
        EXPECT_EQ(row_expanded(row), takes_a_number);
        (takes_a_number ? open : closed) = height_of(row);
        // A number of any size, or the stage off, changes nothing.
        s.gamma = 3.3f;
        s.reinhard_white = 16.0f;
        s.curve_on = false;
        EXPECT_EQ(height_of(row_for(StageId::curve, s)), height_of(row));
        s.curve_on = true;
        // The other rows don't care.
        for (const StageId other : all_stages) {
            if (other != StageId::curve) {
                EXPECT_FALSE(row_expanded(row_for(other, s)));
                EXPECT_EQ(height_of(row_for(other, s)), height_of(row_for(other, EditSettings{})));
            }
        }
    }
    EXPECT_EQ(open, closed + metrics.secondary + metrics.slider + 2 * metrics.gap);
}

TEST(LightPathLayout, PartsFollowOneAnotherInsideTheRow) {
    EditSettings s;
    set_curve_kind(s, CurveKind::reinhard);
    const LightPathRowLayout curve = light_path_row_layout(row_for(StageId::curve, s), metrics);
    EXPECT_EQ(curve.line_y, metrics.pad_top);
    ASSERT_TRUE(curve.tabs_y && curve.parameter_y && curve.parameter_slider_y);
    EXPECT_FALSE(curve.slider_y || curve.subtitle_y);
    EXPECT_EQ(*curve.tabs_y, metrics.pad_top + metrics.line + metrics.gap);
    EXPECT_EQ(*curve.parameter_y, *curve.tabs_y + metrics.tabs + metrics.gap);
    EXPECT_EQ(*curve.parameter_slider_y, *curve.parameter_y + metrics.secondary + metrics.gap);
    EXPECT_EQ(curve.clip_y, *curve.parameter_slider_y + metrics.slider + metrics.gap);

    const LightPathRowLayout decode = light_path_row_layout(row_for(StageId::decode, s), metrics);
    EXPECT_TRUE(decode.subtitle_y);
    EXPECT_FALSE(decode.slider_y || decode.tabs_y);

    // The Test strip button shares Exposure's clip line and takes no room of its own: that line is
    // as tall as every other.
    const LightPathRowLayout exposure =
        light_path_row_layout(row_for(StageId::exposure, s), metrics);
    EXPECT_TRUE(exposure.slider_y);
    EXPECT_EQ(exposure.clip_height, metrics.secondary);
    EXPECT_EQ(exposure.height, light_path_row_layout(row_for(StageId::saturation, s), metrics).height);
    const LightPathRowLayout matrix = light_path_row_layout(row_for(StageId::matrix, s), metrics);
    EXPECT_EQ(matrix.clip_height, metrics.secondary);
    EXPECT_FALSE(matrix.slider_y || matrix.subtitle_y || matrix.tabs_y);
}

TEST(LightPathLayout, WhiteBalanceHasASubtitleAndTwoSlidersOneUnderTheOther) {
    const LightPathRowLayout wb = light_path_row_layout(white_balance_row(EditSettings{}), metrics);
    EXPECT_EQ(wb.line_y, metrics.pad_top);
    ASSERT_TRUE(wb.subtitle_y && wb.slider_y && wb.second_slider_y);
    EXPECT_FALSE(wb.tabs_y || wb.parameter_y || wb.parameter_slider_y);
    EXPECT_EQ(*wb.subtitle_y, metrics.pad_top + metrics.line + metrics.gap);
    EXPECT_EQ(*wb.slider_y, *wb.subtitle_y + metrics.secondary + metrics.gap);
    EXPECT_EQ(*wb.second_slider_y, *wb.slider_y + metrics.slider + metrics.gap);
    EXPECT_EQ(wb.clip_y, *wb.second_slider_y + metrics.slider + metrics.gap);
    EXPECT_EQ(wb.clip_height, metrics.secondary);
    EXPECT_EQ(wb.height, wb.clip_y + metrics.secondary + metrics.pad_bottom);
    // No other row has a second slider.
    for (const StageId stage : all_stages) {
        EXPECT_EQ(light_path_row_layout(row_for(stage, EditSettings{}), metrics)
                      .second_slider_y.has_value(),
                  stage == StageId::white_balance);
    }
}

TEST(LightPathLayout, WhiteBalanceIsTheSameAsShotChosenAndOffWithOrWithoutACamerasLight) {
    const LightPathRowLayout expected =
        light_path_row_layout(white_balance_row(EditSettings{}), metrics);
    EditSettings chosen;
    chosen.white_balance = WhiteBalance{2000, -100};
    EditSettings off = chosen;
    off.white_balance_on = false;
    EditSettings as_shot_off;
    as_shot_off.white_balance_on = false;
    for (const EditSettings& s : {EditSettings{}, chosen, off, as_shot_off}) {
        for (const std::optional<Chromaticity>& as_shot :
             {std::optional<Chromaticity>{}, std::optional<Chromaticity>(white_of({5328, 26.7})),
              std::optional<Chromaticity>(white_of({15000, 100}))}) {
            EXPECT_EQ(light_path_row_layout(white_balance_row(s, as_shot), metrics), expected);
        }
    }
}

// --- Clip marks ---------------------------------------------------------------------------------

TEST(LightPathMarks, AllThreeInOrderHollowSaysHereFilledDoesNot) {
    const std::vector<ClipMark> hollow =
        clip_marks(clip_with(StageId::exposure, ClipFate::recoverable), 1000);
    ASSERT_EQ(hollow.size(), 3u);
    EXPECT_EQ(hollow[0].kind, ClipMarkKind::below_black);
    EXPECT_EQ(hollow[1].kind, ClipMarkKind::above_white);
    EXPECT_EQ(hollow[2].kind, ClipMarkKind::out_of_gamut);
    EXPECT_EQ(hollow[0].text, "7.0% below black here");
    EXPECT_EQ(hollow[1].text, "7.0% above white here");
    EXPECT_EQ(hollow[2].text, "7.0% outside sRGB here");
    EXPECT_EQ(hollow[1].percent, "7.0%");
    for (const ClipMark& mark : hollow) {
        EXPECT_EQ(mark.fate, ClipFate::recoverable);
    }

    const std::vector<ClipMark> filled =
        clip_marks(clip_with(StageId::encode, ClipFate::lost), 1000);
    ASSERT_EQ(filled.size(), 3u);
    EXPECT_EQ(filled[0].text, "7.0% below black");
    EXPECT_EQ(filled[1].text, "7.0% above white");
    EXPECT_EQ(filled[2].text, "7.0% outside sRGB");
    for (const ClipMark& mark : filled) {
        EXPECT_EQ(mark.fate, ClipFate::lost);
    }
}

TEST(LightPathMarks, EachIsThereOnlyIfItsDirectionClips) {
    StageClip clip = clip_with(StageId::contrast, ClipFate::none);
    EXPECT_TRUE(clip_marks(clip, 1000).empty());
    clip.gamut_fate = ClipFate::lost;
    const std::vector<ClipMark> one = clip_marks(clip, 1000);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0].kind, ClipMarkKind::out_of_gamut);
    // A stage that didn't run has nothing to say, whatever its fates.
    StageClip idle = clip_with(StageId::contrast, ClipFate::lost);
    idle.ran = false;
    EXPECT_TRUE(clip_marks(idle, 1000).empty());
}

TEST(LightPathMarks, TheirTooltipSaysWhatTheFateMeans) {
    const std::vector<ClipMark> decode = clip_marks(clip_with(StageId::decode, ClipFate::lost), 1000);
    ASSERT_EQ(decode.size(), 3u);
    EXPECT_EQ(decode[1].what, "Highlights at white");
    EXPECT_NE(decode[1].note.find("Already clipped in the file"), std::string::npos);
    const std::vector<ClipMark> later =
        clip_marks(clip_with(StageId::exposure, ClipFate::recoverable), 1000);
    EXPECT_EQ(later[1].what, "Highlights above white");
    EXPECT_NE(later[1].note.find("later stage"), std::string::npos);
    EXPECT_NE(later[2].note.find("clipped later"), std::string::npos);
}

// --- What rows say ------------------------------------------------------------------------------

TEST(LightPathRows, DecodeSaysWhatTheFileIs) {
    EditSettings s;
    EXPECT_EQ(decode_subtitle(s, "RAW"), "RAW, camera colour");
    EXPECT_EQ(decode_subtitle(s, "JPEG"), "JPEG, read as sRGB");
    EXPECT_EQ(decode_subtitle(s, "PNG"), "PNG, read as sRGB");
    EXPECT_EQ(decode_subtitle(s, "HEIC"), "HEIC, converted from its own colours");
    s.raw_camera_matrix = MatrixFile{};
    EXPECT_EQ(decode_subtitle(s, "RAW"), "RAW, your camera matrix");
}

TEST(LightPathRows, TheMatrixIsNamedInWords) {
    EditSettings s;
    EXPECT_EQ(matrix_value(s, InputKind::raw), "Camera default");
    EXPECT_EQ(matrix_value(s, InputKind::image), "None");
    s.matrix = MatrixFile{};
    EXPECT_EQ(matrix_value(s, InputKind::raw), "From recipe");
    s.matrix->path = std::filesystem::path("some") / "folder" / "grade.txt";
    EXPECT_EQ(matrix_value(s, InputKind::raw), "grade.txt");
    const LightPathRow row = row_for(StageId::matrix, s);
    EXPECT_EQ(row.value, "grade.txt");
    EXPECT_TRUE(row.value_muted);
    EXPECT_NE(row.value_tooltip.find("folder"), std::string::npos);
}

TEST(LightPathRows, ValuesAreInUnits) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.saturation = 1.2f;
    s.contrast = 0.85f;
    EXPECT_EQ(row_for(StageId::exposure, s).value, "+1.00 stops");
    EXPECT_EQ(row_for(StageId::saturation, s).value, "×1.20");
    EXPECT_EQ(row_for(StageId::contrast, s).value, "Grade 1.2 · ×0.85");
    s.saturation = 0.0f;
    EXPECT_EQ(row_for(StageId::saturation, s).value, "black and white");
    s.exposure_stops = -0.5f;
    EXPECT_EQ(row_for(StageId::exposure, s).value, "-0.50 stops");
}

TEST(LightPathRows, AStageThatIsOffKeepsItsValueAndItsSliderGoesNeutral) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.exposure_on = false;
    const LightPathRow row = row_for(StageId::exposure, s);
    EXPECT_EQ(row.value, "+1.00 stops when on");
    EXPECT_TRUE(row.value_muted);
    EXPECT_FALSE(row.enabled);
    EXPECT_EQ(row.slider_position, 0.0f);
    s.exposure_on = true;
    const LightPathRow on = row_for(StageId::exposure, s);
    EXPECT_FALSE(on.value_muted);
    EXPECT_EQ(on.slider_position, 50.0f);
}

TEST(LightPathRows, ValuesPastTheSlidersEndAreFlagged) {
    EditSettings s;
    EXPECT_FALSE(row_for(StageId::saturation, s).beyond_slider);
    s.saturation = 3.0f;
    const LightPathRow row = row_for(StageId::saturation, s);
    EXPECT_TRUE(row.beyond_slider);
    EXPECT_EQ(row.value, "×3.00");
    EXPECT_EQ(row.slider_position, simple_max);
    EXPECT_FALSE(row_for(StageId::contrast, s).beyond_slider);
}

TEST(LightPathRows, NothingLiesPastTheSlidersEndWhileTheStageIsOff) {
    EditSettings s;
    s.exposure_stops = 10.0f;
    EXPECT_TRUE(row_for(StageId::exposure, s).beyond_slider);
    s.exposure_on = false;
    EXPECT_FALSE(row_for(StageId::exposure, s).beyond_slider);  // the slider sits at neutral
}

TEST(LightPathRows, OnlyTheStagesWithAnEffectHaveSwitchesAndSliders) {
    const EditSettings s;
    for (const StageId stage : all_stages) {
        const LightPathRow row = row_for(stage, s);
        EXPECT_EQ(row.name, stage_info(stage).name);
        EXPECT_EQ(row.can_toggle, stage != StageId::decode && stage != StageId::encode);
        EXPECT_EQ(row.slider.has_value(), stage_slider(stage).has_value());
        EXPECT_EQ(row.curve_tabs, stage == StageId::curve);
        EXPECT_EQ(row.test_strip, stage == StageId::exposure);
        EXPECT_EQ(!row.subtitle.empty(), stage == StageId::decode || stage == StageId::encode ||
                                             stage == StageId::white_balance);
        // White balance has its own two sliders, and no other row has them.
        EXPECT_EQ(row.white_balance.has_value(), stage == StageId::white_balance);
        EXPECT_EQ(row.slider_reset.empty(), !row.slider && !row.white_balance);
    }
    EXPECT_FALSE(stage_slider(StageId::white_balance));
    EXPECT_EQ(row_for(StageId::encode, s).subtitle, "sRGB for the screen");
    EXPECT_EQ(row_for(StageId::exposure, s).slider_reset, "Reset to +0.00 stops");
    EXPECT_EQ(row_for(StageId::contrast, s).slider_reset, "Reset to Grade 2 · ×1.00");
}

TEST(LightPathRows, TheCurveRowCarriesItsNumber) {
    EditSettings s;
    EXPECT_FALSE(row_for(StageId::curve, s).parameter);
    set_curve_kind(s, CurveKind::reinhard);
    s.reinhard_white = 4.0f;
    LightPathRow row = row_for(StageId::curve, s);
    ASSERT_TRUE(row.parameter);
    EXPECT_EQ(row.parameter->label, "White point");
    EXPECT_EQ(row.parameter_text, "4.00");
    EXPECT_EQ(row.parameter_reset, "Reset to 4");
    set_curve_kind(s, CurveKind::gamma);
    s.gamma = 2.5f;
    row = row_for(StageId::curve, s);
    EXPECT_EQ(row.parameter->label, "Gamma");
    EXPECT_EQ(row.parameter_text, "2.50");
    EXPECT_EQ(row.parameter_reset, "Reset to 2.2");
    set_curve_kind(s, CurveKind::filmic);
    EXPECT_FALSE(row_for(StageId::curve, s).parameter);
}

TEST(LightPathRows, RowsPastWhatIsOnScreenAreDimmedAndSayWhy) {
    const EditSettings s;
    for (const StageId stage : all_stages) {
        const LightPathRow row = row_for(stage, s, nullptr, StageId::saturation);
        const bool after = static_cast<int>(stage) > static_cast<int>(StageId::saturation);
        EXPECT_EQ(row.dimmed, after) << static_cast<int>(stage);
        EXPECT_EQ(row.viewed, stage == StageId::saturation);
        if (after) {
            EXPECT_EQ(row.tooltip, view_hint(StageId::saturation));
        } else {
            EXPECT_EQ(row.tooltip, explanation_for(explain_id(stage))->summary);
        }
    }
    // Comparing with a later stage shows that one too.
    EXPECT_FALSE(row_for(StageId::contrast, s, nullptr, StageId::exposure, StageId::contrast).dimmed);
    EXPECT_TRUE(row_for(StageId::curve, s, nullptr, StageId::exposure, StageId::contrast).dimmed);
    // At the final picture nothing is.
    for (const StageId stage : all_stages) {
        EXPECT_FALSE(row_for(stage, s).dimmed);
    }
}

TEST(LightPathRows, TheViewHasOneRowPerStageInOrderAndTheCompareWords) {
    const EditSettings s;
    const LightPathView view =
        light_path_view({s, InputKind::raw, "RAW", StageId::contrast, StageId::decode, nullptr,
                         std::nullopt});
    ASSERT_EQ(view.rows.size(), std::size(all_stages));
    for (std::size_t i = 0; i < view.rows.size(); ++i) {
        EXPECT_EQ(view.rows[i].stage, all_stages[i]);
    }
    EXPECT_EQ(view.compare, StageId::decode);
    EXPECT_EQ(view.compare_text, "Compare: Original");
    EXPECT_EQ(compare_action_text(StageId::encode, std::nullopt), "Compare: Off");
    EXPECT_EQ(compare_action_text(StageId::encode, StageId::exposure), "Compare: after Exposure");
    // Comparing a stage with itself is the same as Off.
    EXPECT_EQ(compare_action_text(StageId::exposure, StageId::exposure), "Compare: Off");
}

TEST(LightPathRows, ClipMarksReachTheirRows) {
    const EditSettings s;
    ClipReport report = report_with(ClipFate::none);
    report.stages[static_cast<std::size_t>(StageId::contrast)].over_fate = ClipFate::lost;
    for (const StageId stage : all_stages) {
        EXPECT_EQ(row_for(stage, s, &report).marks.size(), stage == StageId::contrast ? 1u : 0u);
    }
}

// --- White balance -----------------------------------------------------------------------------

TEST(LightPathWhiteBalance, AsShotOnARawFileReadsTheCamerasLightAndTheSlidersSitThere) {
    const LightPathRow row = white_balance_row(EditSettings{}, white_of({5328, 26.7}));
    EXPECT_EQ(row.name, "White balance");
    EXPECT_EQ(row.subtitle, "The light the photo was lit by");
    EXPECT_EQ(row.value, "As shot");
    EXPECT_FALSE(row.value_muted);
    EXPECT_TRUE(row.can_toggle);
    EXPECT_TRUE(row.enabled);
    EXPECT_EQ(row.slider_reset, "Reset to As shot");
    EXPECT_FALSE(row.slider);  // not one of the -100 to +100 sliders
    ASSERT_TRUE(row.white_balance);
    EXPECT_EQ(row.white_balance->kelvin_position, static_cast<float>(kelvin_slider_position(5330)));
    EXPECT_EQ(row.white_balance->tint_position, 27.0f);
    EXPECT_EQ(row.white_balance->kelvin_as_shot, row.white_balance->kelvin_position);
    EXPECT_EQ(row.white_balance->tint_as_shot, 27.0f);
    EXPECT_EQ(row.white_balance->kelvin_typed, "5330 K");
    EXPECT_EQ(row.white_balance->tint_typed, "+27");
    // The numbers beside the sliders: where they sit, which is the camera's light.
    EXPECT_EQ(row.white_balance->kelvin_text, "5330 K");
    EXPECT_EQ(row.white_balance->tint_text, "+27");
    EXPECT_FALSE(row.tooltip.empty());
}

TEST(LightPathWhiteBalance, AsShotWithoutACamerasLightSaysSoInTheSubtitle) {
    for (const InputKind kind : {InputKind::image, InputKind::raw}) {
        const EditSettings s;
        const LightPathRow row = light_path_row(
            {s, kind, kind == InputKind::raw ? "RAW" : "JPEG", final_stage, std::nullopt, nullptr,
             std::nullopt},
            StageId::white_balance);
        EXPECT_EQ(row.value, "As shot");
        EXPECT_EQ(row.subtitle, "Light not recorded, taken as 6500 K");
        ASSERT_TRUE(row.white_balance);
        EXPECT_EQ(row.white_balance->kelvin_text, "6500 K");
        EXPECT_EQ(row.white_balance->tint_text, "0");
        EXPECT_EQ(row.white_balance->kelvin_position,
                  static_cast<float>(kelvin_slider_position(6500)));
        EXPECT_EQ(row.white_balance->tint_position, 0.0f);
        EXPECT_EQ(row.white_balance->kelvin_typed, "6500 K");
        EXPECT_EQ(row.white_balance->tint_typed, "0");
    }
}

TEST(LightPathWhiteBalance, AChosenLightIsShownAndTheFillStillStartsAtAsShot) {
    EditSettings s;
    s.white_balance = WhiteBalance{5200, -10};
    const LightPathRow row = white_balance_row(s, white_of({5328, 26.7}));
    EXPECT_EQ(row.value, "Chosen");
    EXPECT_EQ(row.subtitle, "The light the photo was lit by");
    EXPECT_FALSE(row.value_muted);
    ASSERT_TRUE(row.white_balance);
    EXPECT_EQ(row.white_balance->kelvin_position, static_cast<float>(kelvin_slider_position(5200)));
    EXPECT_EQ(row.white_balance->tint_position, -10.0f);
    EXPECT_EQ(row.white_balance->kelvin_as_shot, static_cast<float>(kelvin_slider_position(5330)));
    EXPECT_EQ(row.white_balance->tint_as_shot, 27.0f);
    EXPECT_EQ(row.white_balance->kelvin_typed, "5200 K");
    EXPECT_EQ(row.white_balance->tint_typed, "\xE2\x88\x92" "10");
    EXPECT_EQ(row.white_balance->kelvin_text, "5200 K");
    EXPECT_EQ(row.white_balance->tint_text, "\xE2\x88\x92" "10");  // U+2212, not a hyphen
    s.white_balance = WhiteBalance{5200, 0};
    EXPECT_EQ(white_balance_row(s).value, "Chosen");
    EXPECT_EQ(white_balance_row(s).white_balance->tint_text, "0");
}

TEST(LightPathWhiteBalance, SwitchedOffItKeepsTheChoiceAndTheSlidersGoBackToAsShot) {
    EditSettings s;
    s.white_balance = WhiteBalance{5200, 10};
    s.white_balance_on = false;
    const LightPathRow row = white_balance_row(s, white_of({5328, 26.7}));
    EXPECT_FALSE(row.enabled);
    EXPECT_EQ(row.value, "Chosen when on");
    EXPECT_TRUE(row.value_muted);
    ASSERT_TRUE(row.white_balance);
    EXPECT_EQ(row.white_balance->kelvin_position, row.white_balance->kelvin_as_shot);
    EXPECT_EQ(row.white_balance->tint_position, 27.0f);
    // The numbers go with the sliders, which sit at the camera's light while the stage is off.
    EXPECT_EQ(row.white_balance->kelvin_text, "5330 K");
    EXPECT_EQ(row.white_balance->tint_text, "+27");
    EXPECT_EQ(row.white_balance->kelvin_typed, "5200 K");  // what it comes back with
    EXPECT_EQ(row.white_balance->tint_typed, "+10");

    // As shot is the same picture on or off: there is nothing to come back.
    s.white_balance.reset();
    const LightPathRow as_shot = white_balance_row(s, white_of({5328, 26.7}));
    EXPECT_EQ(as_shot.value, "As shot");
    EXPECT_TRUE(as_shot.value_muted);
}

TEST(LightPathWhiteBalance, AnAsShotLightPastTheSlidersIsReadInFullAndTheSlidersStopAtTheirEnds) {
    const LightPathRow row = white_balance_row(EditSettings{}, white_of({1800, 0}));
    EXPECT_EQ(row.value, "As shot");
    ASSERT_TRUE(row.white_balance);
    EXPECT_EQ(row.white_balance->kelvin_position, 0.0f);
    EXPECT_EQ(row.white_balance->kelvin_text, "2000 K");  // where the slider sits, at its end
    EXPECT_EQ(row.white_balance->kelvin_typed, "2000 K");
}

TEST(LightPathRowPick, OnlyWhiteBalanceHasThePickNeutralButtonAndItTakesNoRoom) {
    const EditSettings s;
    for (const StageInfo& stage : pipeline_stages()) {
        EXPECT_EQ(row_for(stage.id, s).pick_neutral, stage.id == StageId::white_balance) << stage.name;
    }
    LightPathMetrics metrics;
    metrics.pad_top = 9;
    metrics.pad_bottom = 10;
    metrics.line = 22;
    metrics.secondary = 13;
    metrics.slider = 20;
    metrics.tabs = 26;
    metrics.gap = 4;
    LightPathRow row = row_for(StageId::white_balance, s);
    const LightPathRowLayout with = light_path_row_layout(row, metrics);
    row.pick_neutral = false;
    EXPECT_EQ(light_path_row_layout(row, metrics), with);
}

// --- Dodge & burn's row -------------------------------------------------------------------------

TEST(LightPathDodgeBurn, TheValueSaysNoneDodgesBurnsOrBoth) {
    EXPECT_EQ(strokes_text(0, 0), "None");
    EXPECT_EQ(strokes_text(0, 1), "1 dodge");
    EXPECT_EQ(strokes_text(0, 3), "3 dodges");
    EXPECT_EQ(strokes_text(1, 0), "1 burn");
    EXPECT_EQ(strokes_text(3, 0), "3 burns");
    EXPECT_EQ(strokes_text(2, 1), "2 burns \xC2\xB7 1 dodge");  // burns first
    EXPECT_EQ(strokes_text(1, 2), "1 burn \xC2\xB7 2 dodges");
    EXPECT_EQ(strokes_text(999, 999), "999 burns \xC2\xB7 999 dodges");
}

TEST(LightPathDodgeBurn, TheRowCountsTheStrokesByTheirSign) {
    EXPECT_EQ(row_for(StageId::dodge_burn, EditSettings{}).value, "None");
    EXPECT_EQ(row_for(StageId::dodge_burn, with_strokes(0, 3)).value, "3 dodges");
    EXPECT_EQ(row_for(StageId::dodge_burn, with_strokes(1, 0)).value, "1 burn");
    EXPECT_EQ(row_for(StageId::dodge_burn, with_strokes(2, 1)).value, "2 burns \xC2\xB7 1 dodge");
    EXPECT_EQ(strokes_text(Strokes{}), "None");
}

TEST(LightPathDodgeBurn, SwitchedOffItKeepsTheCountAndSaysWhenOn) {
    EditSettings s = with_strokes(2, 1);
    s.dodge_burn_on = false;
    const LightPathRow off = row_for(StageId::dodge_burn, s);
    EXPECT_EQ(off.value, "2 burns \xC2\xB7 1 dodge when on");
    EXPECT_TRUE(off.value_muted);
    EXPECT_FALSE(off.enabled);
    EXPECT_TRUE(off.can_toggle);
    // Without strokes there is nothing to come back: just None.
    EditSettings none;
    none.dodge_burn_on = false;
    EXPECT_EQ(row_for(StageId::dodge_burn, none).value, "None");
    // The widest the value can read is what the panel's minimum width allows for.
    EditSettings most = with_strokes(999, 999);
    most.dodge_burn_on = false;
    EXPECT_EQ(row_for(StageId::dodge_burn, most).value, widest_strokes_value);
}

TEST(LightPathDodgeBurn, TheControlsLineCarriesTheStrengthAndWhetherThereIsAnythingToClear) {
    const LightPathRow plain = row_for(StageId::dodge_burn, EditSettings{});
    ASSERT_TRUE(plain.brush_controls.has_value());
    EXPECT_FLOAT_EQ(plain.brush_controls->strength, 0.5f);
    EXPECT_EQ(plain.brush_controls->strength_text, "0.5 stop");
    EXPECT_EQ(plain.brush_controls->strength_reset, "Reset to 0.5 stop");
    EXPECT_FALSE(plain.brush_controls->can_clear);
    const EditSettings drawn = with_strokes(1, 0);
    LightPathInputs in{drawn, InputKind::raw, "RAW", final_stage, std::nullopt, nullptr, std::nullopt, 1.5f};
    const LightPathRow strong = light_path_row(in, StageId::dodge_burn);
    EXPECT_EQ(strong.brush_controls->strength_text, "1.5 stops");
    EXPECT_FLOAT_EQ(strong.brush_controls->strength, 1.5f);
    EXPECT_TRUE(strong.brush_controls->can_clear);
    // Only this row has them; the strength is no part of the edit.
    for (const StageInfo& stage : pipeline_stages()) {
        EXPECT_EQ(row_for(stage.id, drawn).brush_controls.has_value(), stage.id == StageId::dodge_burn);
    }
}

TEST(LightPathDodgeBurn, TheControlsLineIsOneSliderHighAndTheRowHeightFollowsTheStageAlone) {
    const LightPathRowLayout layout = light_path_row_layout(row_for(StageId::dodge_burn, {}), metrics);
    ASSERT_TRUE(layout.controls_y.has_value());
    EXPECT_FALSE(layout.slider_y.has_value());
    EXPECT_FALSE(layout.subtitle_y.has_value());
    EXPECT_EQ(*layout.controls_y, metrics.pad_top + metrics.line + metrics.gap);
    EXPECT_EQ(layout.clip_y, *layout.controls_y + metrics.slider + metrics.gap);  // then the clip line
    EXPECT_EQ(layout.height, layout.clip_y + metrics.secondary + metrics.pad_bottom);
    for (const StageInfo& stage : pipeline_stages()) {
        EXPECT_EQ(light_path_row_layout(row_for(stage.id, {}), metrics).controls_y.has_value(),
                  stage.id == StageId::dodge_burn);
    }
    // 0, 1 and 500 strokes, on and off, viewed or dimmed, clipped or not: the same row.
    for (const std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{500}}) {
        for (const bool on : {true, false}) {
            EditSettings s = with_strokes(count / 2, count - count / 2);
            s.dodge_burn_on = on;
            for (const StageId viewed : all_stages) {
                const ClipReport report = report_with(ClipFate::lost);
                EXPECT_EQ(light_path_row_layout(row_for(StageId::dodge_burn, s, &report, viewed), metrics),
                          layout)
                    << count << (on ? " on " : " off ") << static_cast<int>(viewed);
            }
        }
    }
}
