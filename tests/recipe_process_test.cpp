#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"
#include "run_command.hpp"

// A recipe run by prism_core (to_process_settings) does what the same values do as CLI flags.

namespace fs = std::filesystem;

namespace {

using prism::CurveShape;
using prism::Recipe;

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) / ("prism_recipe_process_" + std::string(test->name()) +
                                           "_" + std::to_string(pid) + "_" + name);
}

std::string read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::uint32_t bits(float v) {
    return std::bit_cast<std::uint32_t>(v);
}

// The curve's kind and parameter, bit for bit.
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

void expect_same(const prism::ProcessSettings& a, const prism::ProcessSettings& b) {
    EXPECT_EQ(a.exposure_stops, b.exposure_stops);
    EXPECT_EQ(a.matrix, b.matrix);
    EXPECT_EQ(a.saturation, b.saturation);
    EXPECT_EQ(a.contrast, b.contrast);
    ASSERT_EQ(a.curve.has_value(), b.curve.has_value());
    if (a.curve) {
        expect_same_curve(*a.curve, *b.curve);
    }
}

// What the CLI builds for these flags' values (src/main.cpp): strtod then float for numbers.
float cli_number(const char* text) {
    return static_cast<float>(std::strtod(text, nullptr));
}

// A recipe with every stage, as `prism --exposure +0.7 --matrix samples/grade.txt --saturation
// 1.2 --contrast 0.85 --curve <spec>` would give.
Recipe full_recipe(const prism::RecipeCurve& curve) {
    Recipe r;
    r.exposure = prism::RecipeExposure{0.7f, true};
    r.matrix = prism::RecipeMatrix{prism::load_matrix(PRISM_GRADE_SAMPLE), "grade.txt", true};
    r.saturation = prism::RecipeSaturation{1.2f, true};
    r.contrast = prism::RecipeContrast{0.85f, true};
    r.curve = curve;
    return r;
}

prism::ProcessSettings cli_settings(const char* curve_spec) {
    prism::ProcessSettings p;
    p.exposure_stops = cli_number("+0.7");
    p.matrix = prism::load_matrix(PRISM_GRADE_SAMPLE);
    p.saturation = cli_number("1.2");
    p.contrast = cli_number("0.85");
    if (curve_spec != nullptr) {
        p.curve = prism::parse_curve(curve_spec);
    }
    return p;
}

}  // namespace

TEST(RecipeProcess, EveryCurveIsTheOneCurveBuilds) {
    expect_same(prism::to_process_settings(
                    full_recipe({CurveShape::gamma, 2.4f, prism::reinhard_white_default, true})),
                cli_settings("gamma:2.4"));
    expect_same(prism::to_process_settings(full_recipe({CurveShape::reinhard, 2.2f, 6.5f, true})),
                cli_settings("reinhard:6.5"));
    // Reinhard's default white point is --curve reinhard's.
    expect_same(prism::to_process_settings(
                    full_recipe({CurveShape::reinhard, 2.2f, prism::reinhard_white_default, true})),
                cli_settings("reinhard"));
    expect_same(prism::to_process_settings(full_recipe({CurveShape::filmic, 2.2f, 4.0f, true})),
                cli_settings("filmic"));
    expect_same(prism::to_process_settings(full_recipe({CurveShape::none, 2.2f, 4.0f, true})),
                cli_settings(nullptr));
}

TEST(RecipeProcess, CurveSpecsAreTheCliSpellings) {
    EXPECT_EQ(prism::curve_spec({CurveShape::gamma, 2.2f, 4.0f, true}), "gamma:2.2");
    EXPECT_EQ(prism::curve_spec({CurveShape::reinhard, 2.2f, 4.0f, true}), "reinhard:4");
    EXPECT_EQ(prism::curve_spec({CurveShape::filmic, 2.2f, 4.0f, true}), "filmic");
    EXPECT_EQ(prism::curve_spec({CurveShape::none, 2.2f, 4.0f, true}), std::nullopt);
}

TEST(RecipeProcess, StagesThatAreOffOrLeftOutDoNothing) {
    Recipe r = full_recipe({CurveShape::filmic, 2.2f, 4.0f, false});
    r.exposure->on = false;
    r.contrast.reset();
    const prism::ProcessSettings p = prism::to_process_settings(r);
    EXPECT_FALSE(p.exposure_stops);
    EXPECT_TRUE(p.matrix);
    EXPECT_TRUE(p.saturation);
    EXPECT_FALSE(p.contrast);
    EXPECT_FALSE(p.curve);
    const prism::ProcessSettings none = prism::to_process_settings(Recipe{});
    EXPECT_FALSE(none.exposure_stops || none.matrix || none.saturation || none.contrast ||
                 none.curve);
}

TEST(RecipeProcess, ValuesOutOfRangeAreRefused) {
    Recipe r;
    r.saturation = prism::RecipeSaturation{12.0f, false};  // off, but turning it on must work
    EXPECT_THROW((void)prism::to_process_settings(r), std::invalid_argument);
    r.saturation.reset();
    r.curve = prism::RecipeCurve{CurveShape::gamma, 0.0f, 4.0f, true};
    EXPECT_THROW((void)prism::to_process_settings(r), std::invalid_argument);
    r.curve->shape = CurveShape::filmic;  // gamma's value does not matter for filmic
    EXPECT_NO_THROW((void)prism::to_process_settings(r));
}

TEST(RecipeProcess, ARecipeRendersAsTheCliDoesWithTheSameFlags) {
    // A small image of varied colours, through the real prism executable with flags, and through
    // the recipe's ProcessSettings here: the PNG files must be byte for byte the same.
    prism::Image img;
    img.width = 4;
    img.height = 2;
    img.pixels = {0.02f, 0.05f, 0.01f, 0.18f, 0.18f, 0.18f, 0.4f, 0.1f, 0.05f, 0.9f, 0.7f, 0.2f,
                  0.05f, 0.3f,  0.6f,  1.0f,  1.0f,  0.8f,  0.3f, 0.6f, 0.1f,  0.0f, 0.5f, 1.0f};
    const fs::path input = temp_file("in.png");
    const fs::path by_cli = temp_file("cli.png");
    const fs::path by_recipe = temp_file("recipe.png");
    const fs::path log = temp_file("log.txt");
    prism::save_image(input, img);

    const std::string command =
        "\"" + std::string(PRISM_EXE) + "\" \"" + input.string() + "\" -o \"" + by_cli.string() +
        "\" --exposure +0.7 --matrix \"" + PRISM_GRADE_SAMPLE +
        "\" --saturation 1.2 --contrast 0.85 --curve reinhard:6.5 > \"" + log.string() + "\" 2>&1";
    ASSERT_EQ(prism_test::run_command(command), 0) << read_bytes(log);

    prism::Image mine = prism::load_image(input);
    (void)prism::process(
        mine, prism::to_process_settings(full_recipe({CurveShape::reinhard, 2.2f, 6.5f, true})));
    prism::save_image(by_recipe, mine);
    EXPECT_EQ(read_bytes(by_recipe), read_bytes(by_cli));

    for (const fs::path& p : {input, by_cli, by_recipe, log}) {
        fs::remove(p);
    }
}
