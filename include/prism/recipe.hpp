#pragma once

#include <optional>
#include <string>

#include "prism/pipeline.hpp"
#include "prism/ranges.hpp"

namespace prism {

// A recipe: an edit written down as plain text (a .prism file), so it can be read, changed by
// hand, shared and diffed. docs/RECIPES.md describes the format for people; this header is the
// same thing for code.
//
// A recipe holds one optional section per edit stage. A stage that is present is set completely
// when the recipe is applied; a stage that is absent stays as it was (the partial rule). Each
// stage has an on/off switch, as in the GUI: a stage that is off keeps its values but is skipped.

// The recipe format version this Prism writes.
inline constexpr int recipe_format_version = 1;

struct RecipeExposure {
    float stops = 0.0f;  // any finite number
    bool on = true;
    bool operator==(const RecipeExposure&) const = default;
};

struct RecipeMatrix {
    // Row-major, as Matrix3: row i is output channel i (red, green, blue) as weights of the input
    // R, G and B.
    Matrix3 values = identity_matrix;
    // The file the numbers came from: a file name without folders, kept only as a label. Prism
    // never opens it.
    std::optional<std::string> from;
    bool on = true;
    bool operator==(const RecipeMatrix&) const = default;
};

struct RecipeSaturation {
    float amount = 1.0f;  // saturation_range, the same number as --saturation
    bool on = true;
    bool operator==(const RecipeSaturation&) const = default;
};

struct RecipeContrast {
    float amount = 1.0f;  // contrast_range, the same number as --contrast
    bool on = true;
    bool operator==(const RecipeContrast&) const = default;
};

enum class CurveShape { none, gamma, reinhard, filmic };

struct RecipeCurve {
    CurveShape shape = CurveShape::none;
    // Each is used, and written, only for its own shape; the other keeps its default.
    float gamma = gamma_default;           // above 0
    float white = reinhard_white_default;  // above 0
    bool on = true;
    bool operator==(const RecipeCurve&) const = default;
};

struct Recipe {
    std::optional<std::string> name;         // one line of UTF-8 text
    std::optional<std::string> description;  // one line of UTF-8 text

    // In pipeline order. Empty = not in the recipe: that stage is left as it is.
    std::optional<RecipeExposure> exposure;
    std::optional<RecipeMatrix> matrix;
    std::optional<RecipeSaturation> saturation;
    std::optional<RecipeContrast> contrast;
    std::optional<RecipeCurve> curve;

    // Floats compare by value (so -0 equals +0).
    bool operator==(const Recipe&) const = default;
};

// The recipe as text, in the format of docs/RECIPES.md: `prism_recipe = 1`, the name and
// description if there are any, then each section the recipe has, in pipeline order with a fixed
// key order. Numbers are written in the shortest form that reads back as exactly the same float,
// with ".0" added to whole numbers; exposure always has a sign, and -0 is written as 0. `on =
// false` is written only for a stage that is off. UTF-8 without a byte-order mark, LF line ends.
[[nodiscard]] std::string write_recipe(const Recipe& recipe);

}  // namespace prism
