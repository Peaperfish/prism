#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <variant>

#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"

// merge_recipe_and_flags: what prism runs for --recipe together with stage flags. Flags win stage
// by stage; the rest comes from the recipe; stages in neither do nothing.

namespace {

using prism::CurveShape;
using prism::ProcessSettings;
using prism::Recipe;

std::uint32_t bits(float v) {
    return std::bit_cast<std::uint32_t>(v);
}

void expect_same_curve(const prism::ToneCurve& a, const prism::ToneCurve& b) {
    EXPECT_EQ(a.name, b.name);
    ASSERT_EQ(a.shape.index(), b.shape.index());
    if (const auto* g = std::get_if<prism::GammaCurve>(&a.shape)) {
        EXPECT_EQ(bits(g->inv_gamma), bits(std::get<prism::GammaCurve>(b.shape).inv_gamma));
    } else if (const auto* r = std::get_if<prism::ReinhardCurve>(&a.shape)) {
        EXPECT_EQ(bits(r->inv_white_sq),
                  bits(std::get<prism::ReinhardCurve>(b.shape).inv_white_sq));
    } else {
        EXPECT_EQ(bits(std::get<prism::FilmicCurve>(a.shape).scale),
                  bits(std::get<prism::FilmicCurve>(b.shape).scale));
    }
}

void expect_same(const ProcessSettings& a, const ProcessSettings& b) {
    EXPECT_EQ(a.exposure_stops, b.exposure_stops);
    EXPECT_EQ(a.matrix, b.matrix);
    EXPECT_EQ(a.saturation, b.saturation);
    EXPECT_EQ(a.contrast, b.contrast);
    ASSERT_EQ(a.curve.has_value(), b.curve.has_value());
    if (a.curve) {
        expect_same_curve(*a.curve, *b.curve);
    }
}

constexpr prism::Matrix3 warm = {1.05f, 0.03f, -0.01f, 0.01f, 1.0f, 0.0f, -0.02f, -0.02f, 0.94f};
constexpr prism::Matrix3 cool = {0.95f, 0.0f, 0.05f, 0.0f, 1.0f, 0.0f, 0.02f, 0.0f, 0.98f};

// Every stage present and on.
Recipe full_recipe() {
    Recipe r;
    r.exposure = prism::RecipeExposure{-1.25f};
    r.matrix = prism::RecipeMatrix{warm, "grade.txt"};
    r.saturation = prism::RecipeSaturation{1.2f};
    r.contrast = prism::RecipeContrast{0.85f};
    r.curve = prism::RecipeCurve{CurveShape::gamma, 2.4f};
    return r;
}

// Every stage present but switched off.
Recipe all_off_recipe() {
    Recipe r = full_recipe();
    r.exposure->on = false;
    r.matrix->on = false;
    r.saturation->on = false;
    r.contrast->on = false;
    r.curve->on = false;
    return r;
}

// What full_recipe() gives with `change` applied on top.
template <typename Change>
ProcessSettings full_with(Change change) {
    ProcessSettings s = prism::to_process_settings(full_recipe());
    change(s);
    return s;
}

}  // namespace

TEST(RecipeFlags, ExposureFlagReplacesTheRecipeStage) {
    ProcessSettings flags;
    flags.exposure_stops = 0.5f;
    expect_same(prism::merge_recipe_and_flags(full_recipe(), flags),
                full_with([](ProcessSettings& s) { s.exposure_stops = 0.5f; }));
}

TEST(RecipeFlags, MatrixFlagReplacesTheRecipeStage) {
    ProcessSettings flags;
    flags.matrix = cool;
    expect_same(prism::merge_recipe_and_flags(full_recipe(), flags),
                full_with([](ProcessSettings& s) { s.matrix = cool; }));
}

TEST(RecipeFlags, SaturationFlagReplacesTheRecipeStage) {
    ProcessSettings flags;
    flags.saturation = 0.0f;
    expect_same(prism::merge_recipe_and_flags(full_recipe(), flags),
                full_with([](ProcessSettings& s) { s.saturation = 0.0f; }));
}

TEST(RecipeFlags, ContrastFlagReplacesTheRecipeStage) {
    ProcessSettings flags;
    flags.contrast = 1.5f;
    expect_same(prism::merge_recipe_and_flags(full_recipe(), flags),
                full_with([](ProcessSettings& s) { s.contrast = 1.5f; }));
}

TEST(RecipeFlags, CurveFlagReplacesShapeAndParameter) {
    // The recipe's gamma 2.4 must not leak into the flag's Reinhard curve, or the other way round.
    ProcessSettings flags;
    flags.curve = prism::parse_curve("reinhard:2");
    const ProcessSettings merged = prism::merge_recipe_and_flags(full_recipe(), flags);
    expect_same(merged,
                full_with([](ProcessSettings& s) { s.curve = prism::parse_curve("reinhard:2"); }));
    ASSERT_TRUE(merged.curve);
    EXPECT_TRUE(std::holds_alternative<prism::ReinhardCurve>(merged.curve->shape));
}

TEST(RecipeFlags, AFlagTurnsItsStageOnWhenTheRecipeHasItOff) {
    ProcessSettings flags;
    flags.exposure_stops = 1.0f;
    flags.matrix = cool;
    flags.saturation = 1.3f;
    flags.contrast = 1.1f;
    flags.curve = prism::parse_curve("filmic");
    expect_same(prism::merge_recipe_and_flags(all_off_recipe(), flags), flags);
}

TEST(RecipeFlags, AStageThatIsOffStaysOffWithoutAFlag) {
    ProcessSettings flags;
    flags.contrast = 1.1f;
    const ProcessSettings merged = prism::merge_recipe_and_flags(all_off_recipe(), flags);
    EXPECT_FALSE(merged.exposure_stops);
    EXPECT_FALSE(merged.matrix);
    EXPECT_FALSE(merged.saturation);
    EXPECT_EQ(merged.contrast, 1.1f);
    EXPECT_FALSE(merged.curve);
}

TEST(RecipeFlags, StagesLeftOutOfTheRecipeAndTheFlagsDoNothing) {
    Recipe recipe;
    recipe.saturation = prism::RecipeSaturation{0.9f};
    ProcessSettings flags;
    flags.exposure_stops = -0.5f;
    const ProcessSettings merged = prism::merge_recipe_and_flags(recipe, flags);
    EXPECT_EQ(merged.exposure_stops, -0.5f);
    EXPECT_FALSE(merged.matrix);
    EXPECT_EQ(merged.saturation, 0.9f);
    EXPECT_FALSE(merged.contrast);
    EXPECT_FALSE(merged.curve);
}

TEST(RecipeFlags, RecipeOnlyIsWhatTheRecipeDoesOnItsOwn) {
    for (const Recipe& recipe : {full_recipe(), all_off_recipe(), Recipe{}}) {
        expect_same(prism::merge_recipe_and_flags(recipe, {}), prism::to_process_settings(recipe));
    }
}

TEST(RecipeFlags, FlagsOnlyAreTheFlagsUnchanged) {
    // Without --recipe the CLI runs exactly what its flags say, as before recipes existed.
    ProcessSettings flags;
    flags.exposure_stops = 0.0f;  // a neutral value given as a flag is kept as given
    flags.matrix = warm;
    flags.contrast = 1.15f;
    flags.curve = prism::parse_curve("gamma:2.2");
    expect_same(prism::merge_recipe_and_flags(Recipe{}, flags), flags);
    expect_same(prism::merge_recipe_and_flags(Recipe{}, {}), ProcessSettings{});
}
