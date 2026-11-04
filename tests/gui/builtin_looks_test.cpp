#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/presets.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"

// What the built-in looks are, as literal numbers: the numbers they had as a table in presets.cpp,
// before they became the recipes in recipes/. Comparisons are exact, never within a tolerance:
// matching_preset compares exactly, so a look one float step away would read as Custom.

namespace {

using prism::gui::CurveKind;
using prism::gui::EditSettings;
using prism::gui::InputKind;
using prism::gui::Preset;

struct Row {
    std::string_view id;
    std::string_view name;
    std::string_view description;
    float exposure;
    float saturation;
    float contrast;
    CurveKind curve;
};

constexpr Row raw_rows[] = {
    {"natural", "Natural", "A stop brighter with a filmic curve: a neutral starting point", 1.0f,
     1.0f, 1.0f, CurveKind::filmic},
    {"original", "Original", "No adjustments: the linear image as the sensor saw it", 0.0f, 1.0f,
     1.0f, CurveKind::none},
    {"vivid", "Vivid", "More colour and contrast, closer to a camera JPEG", 1.0f, 1.25f, 1.15f,
     CurveKind::filmic},
    {"soft", "Soft", "Gentle contrast, muted colour and soft highlights", 1.0f, 0.9f, 0.85f,
     CurveKind::reinhard},
    {"bw", "Black & white", "Greyscale with a little extra contrast", 1.0f, 0.0f, 1.2f,
     CurveKind::filmic},
};

constexpr Row image_rows[] = {
    {"original", "Original", "No adjustments: the photo as it is", 0.0f, 1.0f, 1.0f,
     CurveKind::none},
    {"vivid", "Vivid", "More colour and contrast", 0.0f, 1.2f, 1.1f, CurveKind::none},
    {"soft", "Soft", "Gentler contrast and muted colour", 0.0f, 0.9f, 0.9f, CurveKind::none},
    {"bw", "Black & white", "Greyscale with a little extra contrast", 0.0f, 0.0f, 1.15f,
     CurveKind::none},
};

struct Kind {
    InputKind kind;
    std::string_view folder;
    std::span<const Row> rows;
};

const Kind kinds[] = {{InputKind::raw, "raw", raw_rows}, {InputKind::image, "image", image_rows}};

// The settings a look is: its four stages, and everything else at EditSettings' defaults.
EditSettings settings_of(const Row& row) {
    EditSettings s;
    s.exposure_stops = row.exposure;
    s.saturation = row.saturation;
    s.contrast = row.contrast;
    s.curve = row.curve;
    return s;
}

// The file's text, whatever line ends the checkout gave it.
std::string file_text(const Kind& kind, const Row& row) {
    const std::filesystem::path path = std::filesystem::path(PRISM_RECIPES_DIR) /
                                       std::filesystem::path(kind.folder) /
                                       std::filesystem::path(std::string(row.id) + ".prism");
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << row.id;
    std::stringstream text;
    text << file.rdbuf();
    std::string out;
    for (const char c : text.str()) {
        if (c != '\r') {
            out += c;
        }
    }
    return out;
}

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
    EXPECT_EQ(prism::process(img, p, 1), prism::ProcessResult::completed);
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

}  // namespace

TEST(BuiltinLooks, ThePresetsAreTheseRowsInThisOrder) {
    for (const Kind& kind : kinds) {
        const std::span<const Preset> presets = prism::gui::presets_for(kind.kind);
        ASSERT_EQ(presets.size(), kind.rows.size()) << kind.folder;
        for (std::size_t i = 0; i < presets.size(); ++i) {
            const Row& row = kind.rows[i];
            EXPECT_EQ(presets[i].id, row.id);
            EXPECT_EQ(presets[i].name, row.name);
            EXPECT_EQ(presets[i].description, row.description);
            EXPECT_EQ(presets[i].settings, settings_of(row)) << row.id;
        }
    }
}

TEST(BuiltinLooks, EachFileGivesItsRow) {
    for (const Kind& kind : kinds) {
        for (const Row& row : kind.rows) {
            const prism::Recipe recipe = prism::parse_recipe(file_text(kind, row), row.id);
            EXPECT_EQ(recipe.name, std::string(row.name));
            EXPECT_EQ(recipe.description, std::string(row.description));
            EditSettings s;
            prism::gui::apply_recipe(s, recipe);
            EXPECT_EQ(s, settings_of(row)) << kind.folder << "/" << row.id;
        }
    }
}

TEST(BuiltinLooks, EachFileIsExactlyWhatPrismWrites) {
    for (const Kind& kind : kinds) {
        for (const Row& row : kind.rows) {
            const std::string text = file_text(kind, row);
            EXPECT_EQ(prism::write_recipe(prism::parse_recipe(text, row.id)), text)
                << kind.folder << "/" << row.id;
        }
    }
}

TEST(BuiltinLooks, EachFileHasItsFourStagesOnAndNoMatrix) {
    for (const Kind& kind : kinds) {
        for (const Row& row : kind.rows) {
            const prism::Recipe recipe = prism::parse_recipe(file_text(kind, row), row.id);
            EXPECT_FALSE(recipe.matrix.has_value()) << row.id;
            ASSERT_TRUE(recipe.exposure && recipe.saturation && recipe.contrast && recipe.curve)
                << row.id;
            EXPECT_TRUE(recipe.exposure->on && recipe.saturation->on && recipe.contrast->on &&
                        recipe.curve->on)
                << row.id;
        }
    }
}

// Whatever state the photo was in, applying a look sets its four stages with their switches and
// both curve parameters, and nothing else.
TEST(BuiltinLooks, ApplyingOneOverAMessyStartIsExactlyTheLook) {
    EditSettings messy;
    messy.exposure_stops = -3.0f;
    messy.exposure_on = false;
    messy.matrix = prism::gui::MatrixFile{"grade.txt", {1, 0, 0, 0, 1, 0, 0, 0, 0.9f}};
    messy.matrix_on = false;
    messy.saturation = 4.0f;
    messy.saturation_on = false;
    messy.contrast = 3.0f;
    messy.contrast_on = false;
    messy.curve = CurveKind::gamma;
    messy.gamma = 1.4f;
    messy.reinhard_white = 8.0f;
    messy.curve_on = false;
    messy.raw_half_size = true;
    messy.raw_camera_matrix = prism::gui::MatrixFile{"cam.txt", prism::identity_matrix};

    for (const Kind& kind : kinds) {
        const std::span<const Preset> presets = prism::gui::presets_for(kind.kind);
        ASSERT_EQ(presets.size(), kind.rows.size());
        for (std::size_t i = 0; i < presets.size(); ++i) {
            const Row& row = kind.rows[i];
            EditSettings expected = messy;
            expected.exposure_stops = row.exposure;
            expected.saturation = row.saturation;
            expected.contrast = row.contrast;
            expected.curve = row.curve;
            expected.gamma = prism::gamma_default;
            expected.reinhard_white = prism::reinhard_white_default;
            expected.exposure_on = expected.saturation_on = expected.contrast_on =
                expected.curve_on = true;

            EditSettings s = messy;
            prism::gui::apply_preset(s, presets[i]);
            EXPECT_EQ(s, expected) << row.id;
            // The matrix is still there, and matching still ignores it.
            EXPECT_EQ(prism::gui::matching_preset(kind.kind, s), &presets[i]) << row.id;
        }
    }
}

// The CLI runs a recipe through prism::to_process_settings, the window runs its settings through
// gui::to_process_settings: for a look, the two must agree.
TEST(BuiltinLooks, TheCommandLineAndTheWindowProcessALookTheSame) {
    for (const Kind& kind : kinds) {
        for (const Row& row : kind.rows) {
            const prism::Recipe recipe = prism::parse_recipe(file_text(kind, row), row.id);
            const prism::ProcessSettings from_file = prism::to_process_settings(recipe);
            const EditSettings settings = settings_of(row);
            const prism::ProcessSettings from_window = prism::gui::to_process_settings(settings);

            EXPECT_EQ(from_file.exposure_stops, from_window.exposure_stops) << row.id;
            EXPECT_EQ(from_file.matrix, from_window.matrix) << row.id;
            EXPECT_EQ(from_file.saturation, from_window.saturation) << row.id;
            EXPECT_EQ(from_file.contrast, from_window.contrast) << row.id;
            EXPECT_EQ(from_file.curve.has_value(), from_window.curve.has_value()) << row.id;
            EXPECT_EQ(prism::curve_spec(*recipe.curve), prism::gui::curve_spec(settings))
                << row.id;
            EXPECT_EQ(render_bits(from_file), render_bits(from_window)) << row.id;
        }
    }
}
