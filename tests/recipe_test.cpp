#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "prism/recipe.hpp"

// The .prism recipe format (prism/recipe.hpp, docs/RECIPES.md).

namespace fs = std::filesystem;

namespace {

using prism::CurveShape;
using prism::Recipe;

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// The recipe tests/data/recipe_complete.prism holds.
Recipe complete_recipe() {
    Recipe r;
    r.name = "Golden \"test\" recipe";
    r.description = "Every stage, one of them off";
    r.exposure = prism::RecipeExposure{-1.25f, true};
    r.matrix = prism::RecipeMatrix{
        {1.05f, 0.03f, -0.01f, 0.01f, 1.0f, 0.0f, -0.02f, -0.02f, 0.94f}, "grade.txt", true};
    r.saturation = prism::RecipeSaturation{1.2f, false};
    r.contrast = prism::RecipeContrast{0.85f, true};
    r.curve = prism::RecipeCurve{CurveShape::gamma, 2.4f, prism::reinhard_white_default, true};
    return r;
}

// The text write_recipe gives for a recipe with only exposure at `stops`.
std::string stops_line(float stops) {
    Recipe r;
    r.exposure = prism::RecipeExposure{stops, true};
    const std::string text = prism::write_recipe(r);
    const std::size_t start = text.find("stops = ");
    return text.substr(start + 8, text.find('\n', start) - start - 8);
}

// The text written for saturation `amount` (no sign).
std::string amount_line(float amount) {
    Recipe r;
    r.saturation = prism::RecipeSaturation{amount, true};
    const std::string text = prism::write_recipe(r);
    const std::size_t start = text.find("amount = ");
    return text.substr(start + 9, text.find('\n', start) - start - 9);
}

}  // namespace

// --- Writing -------------------------------------------------------------------------------------

TEST(RecipeWrite, MatchesTheGoldenFileByteForByte) {
    const std::string golden = read_text(fs::path(PRISM_TEST_DATA) / "recipe_complete.prism");
    ASSERT_FALSE(golden.empty());
    EXPECT_EQ(prism::write_recipe(complete_recipe()), golden);
}

TEST(RecipeWrite, AnEmptyRecipeIsJustTheVersion) {
    EXPECT_EQ(prism::write_recipe(Recipe{}), "prism_recipe = 1\n");
}

TEST(RecipeWrite, NumbersAreShortestAndWholeNumbersGetADecimal) {
    EXPECT_EQ(amount_line(1.0f), "1.0");
    EXPECT_EQ(amount_line(10.0f), "10.0");
    EXPECT_EQ(amount_line(0.1f), "0.1");                // not 0.100000001
    EXPECT_EQ(amount_line(1.0f / 3.0f), "0.33333334");  // as many digits as it takes
    EXPECT_EQ(amount_line(std::nextafter(1.0f, 2.0f)), "1.0000001");
    EXPECT_EQ(amount_line(1e-7f), "1e-07");  // a valid TOML float as it is
    EXPECT_EQ(amount_line(3e+20f), "3e+20");
}

TEST(RecipeWrite, ExposureAlwaysHasASignAndMinusZeroIsZero) {
    EXPECT_EQ(stops_line(1.0f), "+1.0");
    EXPECT_EQ(stops_line(0.7f), "+0.7");
    EXPECT_EQ(stops_line(-2.0f), "-2.0");
    EXPECT_EQ(stops_line(0.0f), "+0.0");
    EXPECT_EQ(stops_line(-0.0f), "+0.0");
    EXPECT_EQ(amount_line(-0.0f), "0.0");
}

TEST(RecipeWrite, SectionsComeInPipelineOrderWithoutAlignment) {
    const std::string text = prism::write_recipe(complete_recipe());
    std::size_t last = 0;
    for (const char* section :
         {"[exposure]", "[matrix]", "[saturation]", "[contrast]", "[curve]"}) {
        const std::size_t at = text.find(section);
        ASSERT_NE(at, std::string::npos) << section;
        EXPECT_GT(at, last) << section;
        last = at;
    }
    EXPECT_EQ(text.find("  ="), std::string::npos) << "no aligned = signs";
    EXPECT_EQ(text.find("=  "), std::string::npos);
    EXPECT_EQ(text.find('\r'), std::string::npos);
    EXPECT_NE(text.substr(0, 3), "\xEF\xBB\xBF") << "no byte-order mark";
}

TEST(RecipeWrite, OnlyTheChosenCurvesParameterIsWritten) {
    Recipe r;
    r.curve = prism::RecipeCurve{CurveShape::reinhard, 3.0f, 6.0f, true};
    EXPECT_EQ(prism::write_recipe(r),
              "prism_recipe = 1\n\n[curve]\nshape = \"reinhard\"\nwhite = 6.0\n");
    r.curve->shape = CurveShape::filmic;
    r.curve->on = false;
    EXPECT_EQ(prism::write_recipe(r),
              "prism_recipe = 1\n\n[curve]\nshape = \"filmic\"\non = false\n");
}

TEST(RecipeWrite, TextIsEscapedAndStaysOnOneLine) {
    Recipe r;
    r.name = std::string("a \"b\" \\ c\td\ne\x01 f\x7F café 写真");
    EXPECT_EQ(
        prism::write_recipe(r),
        "prism_recipe = 1\nname = \"a \\\"b\\\" \\\\ c\\td\\ne\\u0001 f\\u007F café 写真\"\n");
}
