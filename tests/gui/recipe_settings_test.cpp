#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/white_balance.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/gui/explanations.hpp"
#include "prism/gui/paper_grade.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"

// Recipes and EditSettings (recipe_settings.hpp): the partial rule, the no-matrix rule, and that
// saving and applying a recipe gives back the same settings and the same picture.

namespace fs = std::filesystem;
using prism::CurveShape;
using prism::Recipe;
using prism::gui::CurveKind;
using prism::gui::EditSettings;
using prism::gui::MatrixFile;

namespace {

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) / ("prism_recipe_settings_" + std::string(test->name()) +
                                           "_" + std::to_string(pid) + "_" + name);
}

const prism::Matrix3 grade = {1.05f,  0.03f,  -0.01f,  //
                              0.01f,  1.00f,  0.00f,   //
                              -0.02f, -0.02f, 0.94f};

// What happens when Prism saves `s` as a recipe and then applies that file to a fresh edit.
EditSettings saved_and_applied(const EditSettings& s, const EditSettings& onto = {}) {
    const Recipe read = prism::parse_recipe(prism::write_recipe(prism::gui::recipe_from(s)));
    EditSettings out = onto;
    prism::gui::apply_recipe(out, read);
    return out;
}

// A 16x8 image of varied colours, some above white, for comparing renders.
prism::Image test_image() {
    prism::Image img;
    img.width = 16;
    img.height = 8;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            img.pixels.push_back(0.02f + 0.11f * static_cast<float>(x));
            img.pixels.push_back(0.01f + 0.2f * static_cast<float>(y));
            img.pixels.push_back(0.18f + 0.05f * static_cast<float>((x + y) % 5));
        }
    }
    return img;
}

std::vector<std::uint32_t> render_bits(const prism::ProcessSettings& p) {
    prism::Image img = test_image();
    (void)prism::process(img, p, 1);
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

// Settings from every corner of what the GUI can reach without a matrix: each switch on and off,
// every curve, values inside and at the ends of their ranges. Each curve's own parameter varies;
// the other stays at its default (see SavingKeepsOnlyTheChosenCurvesParameter for why).
std::vector<EditSettings> reachable_without_matrix() {
    std::vector<EditSettings> all;
    const float exposures[] = {0.0f, -0.0f, 1.0f, -2.5f, 0.70000005f, 13.0f};
    const float saturations[] = {0.0f, 1.0f, 1.25f, 10.0f};
    const float contrasts[] = {0.1f, 0.6666667f, 1.0f, 1.15f, 10.0f};
    const CurveKind kinds[] = {CurveKind::none, CurveKind::gamma, CurveKind::reinhard,
                               CurveKind::filmic};
    const float parameters[] = {0.5f, 2.2f, 4.0f, 16.0f};
    for (int switches = 0; switches < 32; ++switches) {
        for (const CurveKind kind : kinds) {
            for (int i = 0; i < 4; ++i) {
                EditSettings s;
                s.exposure_on = (switches & 1) != 0;
                s.matrix_on = (switches & 2) != 0;
                s.saturation_on = (switches & 4) != 0;
                s.contrast_on = (switches & 8) != 0;
                s.curve_on = (switches & 16) != 0;
                s.exposure_stops = exposures[(switches + i) % 6];
                s.saturation = saturations[i];
                s.contrast = contrasts[(switches + i) % 5];
                s.curve = kind;
                if (kind == CurveKind::gamma) {
                    s.gamma = parameters[i];
                } else if (kind == CurveKind::reinhard) {
                    s.reinhard_white = parameters[i];
                }
                all.push_back(s);
            }
        }
    }
    // The presets, for both kinds of file.
    for (const prism::gui::InputKind kind :
         {prism::gui::InputKind::raw, prism::gui::InputKind::image}) {
        for (const prism::gui::Preset& preset : prism::gui::presets_for(kind)) {
            EditSettings s;
            prism::gui::apply_preset(s, preset);
            all.push_back(s);
        }
    }
    return all;
}

}  // namespace

// --- Applying a recipe ---------------------------------------------------------------------------

TEST(RecipeSettings, ACompleteRecipeSetsEveryStage) {
    Recipe r;
    r.exposure = prism::RecipeExposure{0.5f, true};
    r.matrix = prism::RecipeMatrix{grade, "grade.txt", true};
    r.saturation = prism::RecipeSaturation{0.9f, false};
    r.contrast = prism::RecipeContrast{0.85f, true};
    r.curve = prism::RecipeCurve{CurveShape::reinhard, 2.2f, 6.0f, true};
    EditSettings s;
    prism::gui::apply_recipe(s, r);

    EditSettings expected;
    expected.exposure_stops = 0.5f;
    expected.matrix = MatrixFile{"grade.txt", grade};
    expected.saturation = 0.9f;
    expected.saturation_on = false;
    expected.contrast = 0.85f;
    expected.curve = CurveKind::reinhard;
    expected.reinhard_white = 6.0f;
    EXPECT_EQ(s, expected);
}

TEST(RecipeSettings, StagesLeftOutStayAsTheyAre) {
    EditSettings s;
    prism::gui::apply_preset(s, prism::gui::default_preset(prism::gui::InputKind::raw));
    s.matrix = MatrixFile{"/photos/camera.txt", grade};
    s.contrast_on = false;
    EditSettings expected = s;

    Recipe r;
    r.saturation = prism::RecipeSaturation{1.3f, true};
    prism::gui::apply_recipe(s, r);
    expected.saturation = 1.3f;
    EXPECT_EQ(s, expected);

    prism::gui::apply_recipe(s, Recipe{});
    EXPECT_EQ(s, expected) << "an empty recipe changes nothing";
}

TEST(RecipeSettings, SwitchesAreAppliedAndOffStagesKeepTheirValues) {
    Recipe r;
    r.exposure = prism::RecipeExposure{-1.0f, false};
    r.curve = prism::RecipeCurve{CurveShape::filmic, 2.2f, 4.0f, false};
    EditSettings s;
    prism::gui::apply_recipe(s, r);
    EXPECT_FALSE(s.exposure_on);
    EXPECT_EQ(s.exposure_stops, -1.0f);
    EXPECT_FALSE(s.curve_on);
    EXPECT_EQ(s.curve, CurveKind::filmic);

    r.exposure->on = true;
    prism::gui::apply_recipe(s, r);
    EXPECT_TRUE(s.exposure_on) << "a present stage sets its switch either way";
}

TEST(RecipeSettings, IdentityRowsWithoutFromAreNoMatrix) {
    EditSettings s;
    s.matrix = MatrixFile{"grade.txt", grade};
    Recipe r;
    r.matrix = prism::RecipeMatrix{prism::identity_matrix, std::nullopt, true};
    prism::gui::apply_recipe(s, r);
    EXPECT_FALSE(s.matrix) << "a present [matrix] replaces the loaded one";

    // With a label it is a real (if neutral) matrix file.
    r.matrix->from = "identity.txt";
    prism::gui::apply_recipe(s, r);
    ASSERT_TRUE(s.matrix);
    EXPECT_EQ(s.matrix->values, prism::identity_matrix);
    EXPECT_EQ(s.matrix->path, fs::path("identity.txt"));

    // Other numbers without a label: a matrix with no file name.
    r.matrix = prism::RecipeMatrix{grade, std::nullopt, false};
    prism::gui::apply_recipe(s, r);
    ASSERT_TRUE(s.matrix);
    EXPECT_EQ(s.matrix->values, grade);
    EXPECT_TRUE(s.matrix->path.empty());
    EXPECT_FALSE(s.matrix_on);
}

TEST(RecipeSettings, NoMatrixIsSavedAsIdentityRowsWithoutFrom) {
    EditSettings s;
    s.matrix_on = false;
    const Recipe r = prism::gui::recipe_from(s);
    ASSERT_TRUE(r.matrix);
    EXPECT_EQ(r.matrix->values, prism::identity_matrix);
    EXPECT_FALSE(r.matrix->from);
    EXPECT_FALSE(r.matrix->on);
    EXPECT_EQ(saved_and_applied(s), s);
}

TEST(RecipeSettings, AMatrixIsSavedWithItsFileNameOnly) {
    EditSettings s;
    s.matrix = MatrixFile{fs::path("photos") / "grades" / "warm.txt", grade};
    const Recipe r = prism::gui::recipe_from(s);
    EXPECT_EQ(r.matrix->from, "warm.txt");
    EXPECT_EQ(r.matrix->values, grade);
    const EditSettings back = saved_and_applied(s);
    ASSERT_TRUE(back.matrix);
    EXPECT_EQ(back.matrix->values, grade);
    EXPECT_EQ(back.matrix->path, fs::path("warm.txt"));
}

TEST(RecipeSettings, TheOtherCurvesParameterIsKeptOnApply) {
    EditSettings s;
    s.gamma = 3.0f;
    s.reinhard_white = 8.0f;
    Recipe r;
    r.curve = prism::RecipeCurve{CurveShape::reinhard, prism::gamma_default, 6.0f, true};
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.reinhard_white, 6.0f);
    EXPECT_EQ(s.gamma, 3.0f);
    r.curve = prism::RecipeCurve{CurveShape::filmic, 1.5f, 1.5f, true};
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.gamma, 3.0f);
    EXPECT_EQ(s.reinhard_white, 6.0f);
}

TEST(RecipeSettings, TheRawOptionsNeverChange) {
    EditSettings s;
    s.raw_half_size = true;
    s.raw_camera_matrix = MatrixFile{"camera.txt", grade};
    const EditSettings before = s;
    Recipe r = prism::gui::recipe_from(EditSettings{});
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.raw_half_size, before.raw_half_size);
    EXPECT_EQ(s.raw_camera_matrix, before.raw_camera_matrix);
    EXPECT_EQ(s, before) << "the recipe of the defaults leaves only the RAW options";
}

// --- Capturing the current edit ------------------------------------------------------------------

TEST(RecipeSettings, CaptureWritesEveryStage) {
    const std::string text = prism::write_recipe(prism::gui::recipe_from(EditSettings{}));
    for (const char* section :
         {"[exposure]", "[matrix]", "[saturation]", "[contrast]", "[curve]"}) {
        EXPECT_NE(text.find(section), std::string::npos) << section << "\n" << text;
    }
}

// --- White balance -------------------------------------------------------------------------------

TEST(RecipeWhiteBalance, ApplyingARecipeSetsTheLightAndItsSwitch) {
    Recipe r;
    r.white_balance = prism::RecipeWhiteBalance{5200.0f, 10.0f, true};
    EditSettings s;
    s.exposure_stops = 0.5f;
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.white_balance, (prism::WhiteBalance{5200, 10}));
    EXPECT_TRUE(s.white_balance_on);
    EXPECT_EQ(s.exposure_stops, 0.5f);  // a stage the recipe leaves out stays

    r.white_balance = prism::RecipeWhiteBalance{3200.0f, -25.0f, false};
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.white_balance, (prism::WhiteBalance{3200, -25}));
    EXPECT_FALSE(s.white_balance_on);
    EXPECT_FALSE(prism::gui::to_process_settings(s).white_balance);
}

TEST(RecipeWhiteBalance, ARecipeWithoutTheSectionLeavesThePhotosChoiceAlone) {
    EditSettings s;
    s.white_balance = prism::WhiteBalance{4000, 20};
    s.white_balance_on = false;
    Recipe r;
    r.exposure = prism::RecipeExposure{1.0f, true};
    prism::gui::apply_recipe(s, r);
    EXPECT_EQ(s.white_balance, (prism::WhiteBalance{4000, 20}));
    EXPECT_FALSE(s.white_balance_on);
    // And an As shot photo stays As shot.
    EditSettings as_shot;
    prism::gui::apply_recipe(as_shot, r);
    EXPECT_FALSE(as_shot.white_balance);
}

TEST(RecipeWhiteBalance, AsShotWritesNoSectionAndTheRecipeStaysVersionOne) {
    for (const bool on : {true, false}) {
        EditSettings s;
        s.exposure_stops = 0.7f;
        s.white_balance_on = on;  // the switch alone has nothing to be saved with
        const Recipe r = prism::gui::recipe_from(s);
        EXPECT_FALSE(r.white_balance);
        const std::string text = prism::write_recipe(r);
        EXPECT_EQ(text.find("[white_balance]"), std::string::npos) << text;
        EXPECT_NE(text.find("prism_recipe = 1 "), std::string::npos) << text;
        EXPECT_EQ(text.find("prism_recipe = 2"), std::string::npos) << text;
        EXPECT_EQ(prism::parse_recipe(text).white_balance, std::nullopt);
    }
    // Byte for byte what it was before there was a white balance to save.
    EditSettings chosen_then_reset;
    chosen_then_reset.white_balance = prism::WhiteBalance{5200, 10};
    prism::gui::reset_white_balance(chosen_then_reset);
    EXPECT_EQ(prism::write_recipe(prism::gui::recipe_from(chosen_then_reset)),
              prism::write_recipe(prism::gui::recipe_from(EditSettings{})));
}

TEST(RecipeWhiteBalance, AChosenLightIsWrittenAsVersionTwoWithItsSwitch) {
    EditSettings s;
    s.white_balance = prism::WhiteBalance{5200, -10};
    const std::string text = prism::write_recipe(prism::gui::recipe_from(s));
    EXPECT_NE(text.find("prism_recipe = 2 "), std::string::npos) << text;
    const std::string section = [&] {
        const std::size_t start = text.find("[white_balance]");
        return start == std::string::npos ? std::string() : text.substr(start, text.find("\n[", start) - start);
    }();
    EXPECT_NE(section.find("kelvin = 5200"), std::string::npos) << text;
    EXPECT_NE(section.find("tint = -10"), std::string::npos) << text;
    EXPECT_EQ(section.find("on = false"), std::string::npos) << text;
    // Between [exposure] and [matrix], as the stage is.
    EXPECT_LT(text.find("[exposure]"), text.find("[white_balance]"));
    EXPECT_LT(text.find("[white_balance]"), text.find("[matrix]"));

    s.white_balance_on = false;
    const Recipe off = prism::gui::recipe_from(s);
    ASSERT_TRUE(off.white_balance);
    EXPECT_FALSE(off.white_balance->on);
    EXPECT_NE(prism::write_recipe(off).find("on = false"), std::string::npos);
}

TEST(RecipeWhiteBalance, SavingThenApplyingGivesTheSameSettingsForEveryChoiceTheSlidersMake) {
    for (double kelvin = 2000; kelvin <= 15000; kelvin += 130) {
        for (const double tint : {-100.0, -27.0, 0.0, 1.0, 100.0}) {
            for (const bool on : {true, false}) {
                EditSettings s;
                s.exposure_stops = 0.5f;
                s.curve = CurveKind::filmic;
                s.white_balance = prism::WhiteBalance{kelvin, tint};
                s.white_balance_on = on;
                ASSERT_EQ(saved_and_applied(s), s) << kelvin << ", " << tint;
                // Onto a photo with another choice: the recipe's replaces it.
                EditSettings other;
                other.white_balance = prism::WhiteBalance{9000, 55};
                other.white_balance_on = !on;
                ASSERT_EQ(saved_and_applied(s, other), s) << kelvin << ", " << tint;
            }
        }
    }
    // As shot onto a photo with a choice: the recipe has no section, so the choice stays.
    EditSettings chosen;
    chosen.white_balance = prism::WhiteBalance{9000, 55};
    EXPECT_EQ(saved_and_applied(EditSettings{}, chosen), chosen);
}

TEST(RecipeWhiteBalance, TheGuiPathAndTheCorePathRenderBitForBitTheSame) {
    EditSettings s;
    s.exposure_stops = 0.3f;
    s.white_balance = prism::WhiteBalance{4300, 12};
    const prism::Chromaticity as_shot = prism::white_of(prism::WhiteBalance{5600, -8});
    const Recipe recipe = prism::parse_recipe(prism::write_recipe(prism::gui::recipe_from(s)));
    prism::Image through_gui = test_image();
    prism::Image through_core = test_image();
    (void)prism::process(through_gui,
                         prism::gui::to_process_settings(s, prism::gui::final_stage, as_shot), 1);
    (void)prism::process(through_core, prism::to_process_settings(recipe, as_shot), 1);
    EXPECT_EQ(through_gui.pixels, through_core.pixels);
    EXPECT_NE(through_gui.pixels, test_image().pixels);
}

TEST(RecipeSettings, SavingThenApplyingGivesTheSameSettingsForEveryReachableStateWithoutAMatrix) {
    for (const EditSettings& s : reachable_without_matrix()) {
        // Onto the defaults, and onto settings that differ everywhere: a complete recipe sets
        // every stage, whatever was there before.
        EXPECT_EQ(saved_and_applied(s), s);
        EditSettings other;
        prism::gui::apply_preset(other, prism::gui::default_preset(prism::gui::InputKind::raw));
        other.matrix = MatrixFile{"grade.txt", grade};
        other.saturation_on = false;
        if (s.curve != CurveKind::gamma) {
            other.gamma = s.gamma;  // the unused parameter is not in the file: see below
        }
        if (s.curve != CurveKind::reinhard) {
            other.reinhard_white = s.reinhard_white;
        }
        EXPECT_EQ(saved_and_applied(s, other), s);
        if (::testing::Test::HasFailure()) {
            FAIL() << prism::write_recipe(prism::gui::recipe_from(s));
        }
    }
}

TEST(RecipeSettings, EverySimpleSliderPositionSurvivesSaving) {
    // Every position in steps of 1/4 (finer than a mouse moves the slider) for each slider, from
    // the defaults and from the RAW preset.
    EditSettings natural;
    prism::gui::apply_preset(natural, prism::gui::default_preset(prism::gui::InputKind::raw));
    for (const EditSettings& start : {EditSettings{}, natural}) {
        for (const prism::gui::SimpleControl control :
             {prism::gui::SimpleControl::brightness, prism::gui::SimpleControl::colour}) {
            for (int quarter = -400; quarter <= 400; ++quarter) {
                EditSettings s = start;
                prism::gui::apply_simple_control(s, control, static_cast<float>(quarter) / 4.0f);
                const EditSettings back = saved_and_applied(s);
                ASSERT_EQ(back, s) << "position " << quarter / 4.0;
                ASSERT_EQ(prism::gui::simple_from(back), prism::gui::simple_from(s));
            }
        }
        // Contrast's slider: every half grade, from 0 to 5.
        for (int position = 0; position <= 10; ++position) {
            EditSettings s = start;
            prism::gui::apply_contrast(s, prism::gui::contrast_for_grade_position(
                                              static_cast<float>(position)));
            ASSERT_EQ(saved_and_applied(s), s) << "grade position " << position;
        }
    }
}

TEST(RecipeSettings, SavingKeepsOnlyTheChosenCurvesParameter) {
    // Decision: only the chosen curve's parameter is in the file. So a gamma value set while
    // Reinhard is chosen does not survive saving; the look, and the picture, do.
    EditSettings s;
    s.curve = CurveKind::reinhard;
    s.reinhard_white = 6.0f;
    s.gamma = 3.0f;
    const EditSettings back = saved_and_applied(s);
    EXPECT_EQ(back.gamma, prism::gamma_default);
    EXPECT_EQ(back.reinhard_white, 6.0f);
    EXPECT_TRUE(prism::gui::same_look(back, s));
    EXPECT_EQ(render_bits(prism::gui::to_process_settings(back)),
              render_bits(prism::gui::to_process_settings(s)));
}

TEST(RecipeSettings, ValidateAcceptsEveryRecipeThatReads) {
    std::mt19937 rng(17);
    const auto uniform = [&](float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    };
    for (int i = 0; i < 2000; ++i) {
        Recipe r;
        r.exposure = prism::RecipeExposure{uniform(-1e6f, 1e6f), (rng() & 1) != 0};
        r.saturation = prism::RecipeSaturation{uniform(0.0f, 10.0f), true};
        r.contrast = prism::RecipeContrast{uniform(0.1f, 10.0f), true};
        r.curve = prism::RecipeCurve{static_cast<CurveShape>(rng() % 4), uniform(1e-6f, 1e6f),
                                     uniform(1e-6f, 1e6f), true};
        // Through the text, so only what parse_recipe accepts is applied.
        const Recipe read = prism::parse_recipe(prism::write_recipe(r));
        EditSettings s;
        prism::gui::apply_recipe(s, read);
        ASSERT_TRUE(prism::gui::validate(s).empty()) << prism::write_recipe(r);
    }
    // The ends of every range read, and validate accepts them.
    const Recipe ends = prism::parse_recipe(
        "prism_recipe = 1\n[saturation]\namount = 0\n[contrast]\namount = 0.1\n"
        "[curve]\nshape = \"gamma\"\ngamma = 1e-45\n");
    EditSettings s;
    prism::gui::apply_recipe(s, ends);
    EXPECT_TRUE(prism::gui::validate(s).empty());
}

// --- Rendering -----------------------------------------------------------------------------------

TEST(RecipeSettings, TheGuiPathAndTheCorePathRenderBitForBitTheSame) {
    std::vector<Recipe> recipes;
    for (const EditSettings& s : reachable_without_matrix()) {
        recipes.push_back(prism::gui::recipe_from(s));
    }
    EditSettings graded;
    graded.matrix = MatrixFile{"grade.txt", grade};
    graded.exposure_stops = 0.7f;
    graded.curve = CurveKind::filmic;
    recipes.push_back(prism::gui::recipe_from(graded));
    for (const Recipe& r : recipes) {
        EditSettings s;
        prism::gui::apply_recipe(s, r);
        ASSERT_EQ(render_bits(prism::gui::to_process_settings(s)),
                  render_bits(prism::to_process_settings(r)))
            << prism::write_recipe(r);
    }
}

// --- Through the Session -------------------------------------------------------------------------

TEST(RecipeSettings, ApplyingThroughTheSessionIsOneUndoStep) {
    prism::Image img = test_image();
    const fs::path png = temp_file("photo.png");
    prism::save_image(png, img);
    prism::gui::Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    const EditSettings before = session.settings();
    const std::shared_ptr<const prism::Image> base = session.preview_base();

    Recipe r;
    r.exposure = prism::RecipeExposure{1.0f, true};
    r.contrast = prism::RecipeContrast{1.2f, true};
    ASSERT_EQ(session.apply_recipe(r), std::nullopt);
    EXPECT_EQ(session.settings().exposure_stops, 1.0f);
    EXPECT_EQ(session.settings().contrast, 1.2f);
    EXPECT_EQ(session.preview_base(), base) << "a recipe never decodes the file again";

    // Applying it again changes nothing, so it adds no step: one undo goes back to the start.
    ASSERT_EQ(session.apply_recipe(r), std::nullopt);
    ASSERT_TRUE(session.history().can_undo());
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), before);
    EXPECT_FALSE(session.history().can_undo());

    // A recipe of the current settings changes nothing at all.
    ASSERT_EQ(session.apply_recipe(prism::gui::recipe_from(session.settings())), std::nullopt);
    EXPECT_FALSE(session.history().can_undo());
    fs::remove(png);
}

TEST(RecipeSettings, AnInvalidRecipeIsRefusedByTheSession) {
    prism::gui::Session session;
    Recipe r;
    r.saturation = prism::RecipeSaturation{50.0f, true};  // parse_recipe never gives this
    const prism::gui::MaybeError error = session.apply_recipe(r);
    ASSERT_TRUE(error);
    EXPECT_EQ(error->kind, prism::gui::ErrorKind::invalid_settings);
    EXPECT_EQ(session.settings(), EditSettings{});
}

// --- Every field accounted for -------------------------------------------------------------------

namespace {

// Each field of EditSettings, and whether a recipe holds it. Adding a field to EditSettings
// breaks the build in recipe_settings.cpp and here (the structured binding below must name every
// field) until it is listed and the reason written down.
struct Field {
    std::string_view name;
    bool in_recipe;
    std::string_view why;
};

constexpr Field fields[] = {
    {"exposure_stops", true, "[exposure] stops"},
    {"exposure_on", true, "[exposure] on"},
    {"white_balance", true, "[white_balance] kelvin and tint; As shot (none) writes no section"},
    {"white_balance_on", true, "[white_balance] on, only with a chosen white balance"},
    {"matrix", true, "[matrix] red, green, blue and from; none is identity rows without from"},
    {"matrix_on", true, "[matrix] on"},
    {"saturation", true, "[saturation] amount"},
    {"saturation_on", true, "[saturation] on"},
    {"contrast", true, "[contrast] amount"},
    {"contrast_on", true, "[contrast] on"},
    {"curve", true, "[curve] shape"},
    {"gamma", true, "[curve] gamma, only while the gamma curve is chosen"},
    {"reinhard_white", true, "[curve] white, only while the Reinhard curve is chosen"},
    {"curve_on", true, "[curve] on"},
    {"raw_half_size", false, "a RAW decoding option: how big to decode, not how the photo looks"},
    {"raw_camera_matrix", false,
     "a RAW decoding option tied to the camera; changing it decodes the file again"},
    {"strokes", false, "Dodge & burn strokes: the recipe format has no section for them in 0.5.0"},
    {"dodge_burn_on", false, "Dodge & burn's switch: without strokes in a recipe it has no use"},
};

// The settings with field `i` changed from its default (the curve chosen so that its parameter
// counts).
EditSettings changed(std::size_t i) {
    EditSettings s;
    auto& [exposure_stops, exposure_on, strokes, dodge_burn_on, white_balance, white_balance_on,
           matrix, matrix_on, saturation, saturation_on, contrast, contrast_on, curve, gamma,
           reinhard_white, curve_on, raw_half_size, raw_camera_matrix] = s;
    switch (i) {
        case 0: exposure_stops = 1.0f; break;
        case 1: exposure_on = false; break;
        case 2: white_balance = prism::WhiteBalance{5200, 10}; break;
        case 3: white_balance = prism::WhiteBalance{5200, 10}, white_balance_on = false; break;
        case 4: matrix = MatrixFile{"grade.txt", grade}; break;
        case 5: matrix_on = false; break;
        case 6: saturation = 2.0f; break;
        case 7: saturation_on = false; break;
        case 8: contrast = 2.0f; break;
        case 9: contrast_on = false; break;
        case 10: curve = CurveKind::filmic; break;
        case 11: curve = CurveKind::gamma, gamma = 3.0f; break;
        case 12: curve = CurveKind::reinhard, reinhard_white = 8.0f; break;
        case 13: curve_on = false; break;
        case 14: raw_half_size = true; break;
        case 15: raw_camera_matrix = MatrixFile{"camera.txt", grade}; break;
        case 16: {
            auto field = std::make_shared<prism::StrokeField>();
            field->aspect = 1.5f;
            field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
                prism::FieldStroke{{{0.5f, 0.5f}}, 0.05f, 0.5f, 1.0f}));
            strokes = prism::gui::Strokes(std::move(field));
            break;
        }
        case 17: dodge_burn_on = false; break;
        default: break;
    }
    return s;
}

}  // namespace

TEST(RecipeSettings, EveryFieldIsEitherInARecipeOrLeftOutForAReason) {
    ASSERT_EQ(std::size(fields), 18u);
    const EditSettings defaults;
    for (std::size_t i = 0; i < std::size(fields); ++i) {
        const Field& f = fields[i];
        const EditSettings s = changed(i);
        ASSERT_NE(s, defaults) << f.name;
        EXPECT_FALSE(f.why.empty()) << f.name;
        // Gamma and the white point only differ from their curve-only baseline.
        EditSettings baseline;
        if (f.name == "gamma") {
            baseline.curve = CurveKind::gamma;
        } else if (f.name == "reinhard_white") {
            baseline.curve = CurveKind::reinhard;
        } else if (f.name == "white_balance_on") {
            baseline.white_balance = prism::WhiteBalance{5200, 10};  // the switch needs a section
        }
        const bool saved = prism::write_recipe(prism::gui::recipe_from(s)) !=
                           prism::write_recipe(prism::gui::recipe_from(baseline));
        EXPECT_EQ(saved, f.in_recipe) << f.name << ": " << f.why;
        // A field that is saved also comes back; one that is not is left as it was.
        const EditSettings back = saved_and_applied(s, defaults);
        if (f.in_recipe) {
            EXPECT_TRUE(back == s || (f.name == "matrix" && back.matrix &&
                                      back.matrix->values == s.matrix->values))
                << f.name;
        } else {
            EXPECT_EQ(back, defaults) << f.name;
        }
    }
}

// --- The comments' numbers -----------------------------------------------------------------------

namespace {

// The part of a saved recipe from [section] to the next section.
std::string section_text(const std::string& text, const std::string& section) {
    const std::size_t start = text.find("[" + section + "]");
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = text.find("\n[", start + 1);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// The first group of `pattern` in `text`, or "(none)" if it does not match.
std::string first_match(const std::string& text, const std::string& pattern) {
    std::smatch m;
    if (std::regex_search(text, m, std::regex(pattern))) {
        return m[1].str();
    }
    return "(none)";
}

const std::string times = "\xC3\x97";  // ×, as the Explain area writes it

// A stage's live line without "Off, so no change. When on: " in front: the file's comment says
// what the value does, and its `on = false` line says that the stage is off.
std::string live_when_on(std::string_view id, const EditSettings& s) {
    const std::string line = prism::gui::live_line(id, s);
    const std::string off = "Off, so no change. When on: ";
    return line.rfind(off, 0) == 0 ? line.substr(off.size()) : line;
}

}  // namespace

TEST(RecipeComments, EachNumberIsTheOneTheExplainAreaShows) {
    std::vector<EditSettings> all = reachable_without_matrix();
    for (int position = -100; position <= 100; position += 5) {
        for (const prism::gui::SimpleControl control :
             {prism::gui::SimpleControl::brightness, prism::gui::SimpleControl::colour}) {
            EditSettings s;
            prism::gui::apply_simple_control(s, control, static_cast<float>(position));
            all.push_back(s);
        }
    }
    for (int position = 0; position <= 10; ++position) {
        EditSettings s;
        prism::gui::apply_contrast(s, prism::gui::contrast_for_grade_position(
                                          static_cast<float>(position)));
        all.push_back(s);
    }
    for (const EditSettings& s : all) {
        const std::string text = prism::write_recipe(prism::gui::recipe_from(s));
        SCOPED_TRACE(text);

        // Exposure: 2^stops, "1.62x the light" in the file and "1.62× the light" in the window.
        EXPECT_EQ(
            first_match(section_text(text, "exposure"), "# ([0-9.]+|inf)x the light"),
            first_match(live_when_on("stage.exposure", s), "([0-9.]+|inf)" + times + " the light"));

        // Saturation: the factor, or black and white.
        const std::string saturation = section_text(text, "saturation");
        const std::string saturation_live = live_when_on("stage.saturation", s);
        if (s.saturation == 0.0f) {
            EXPECT_NE(saturation.find("# black and white"), std::string::npos);
            EXPECT_NE(saturation_live.find("black and white"), std::string::npos);
        } else {
            EXPECT_EQ(first_match(saturation, "# ([0-9.]+)x the colourfulness"),
                      first_match(saturation_live, times + "([0-9.]+) colour"));
        }

        // Contrast: how much wider or narrower each stop near middle grey becomes.
        const std::string contrast = section_text(text, "contrast");
        const std::string contrast_live = live_when_on("stage.contrast", s);
        EXPECT_EQ(first_match(contrast, "# each stop near middle grey is ([0-9]+% [a-z]+)"),
                  first_match(contrast_live, "each stop near middle grey is ([0-9]+% [a-z]+)"));
        EXPECT_EQ(contrast.find("no change") != std::string::npos,
                  contrast_live.find("no change") != std::string::npos);

        // The curve: what 18% grey becomes.
        const std::string curve = section_text(text, "curve");
        const std::string curve_live = live_when_on("stage.curve", s);
        EXPECT_EQ(first_match(curve, "# 18% grey becomes ([0-9]+%)"),
                  first_match(curve_live, "18% grey becomes ([0-9]+%)"));
        if (s.curve == CurveKind::none) {
            EXPECT_NE(curve.find("# no tone curve"), std::string::npos);
        } else {
            EXPECT_NE(first_match(curve, "# 18% grey becomes ([0-9]+%)"), "(none)");
        }
        if (::testing::Test::HasFailure()) {
            return;
        }
    }
}
