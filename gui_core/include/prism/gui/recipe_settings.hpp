#pragma once

#include "prism/gui/settings.hpp"
#include "prism/recipe.hpp"

namespace prism::gui {

// Recipes (prism/recipe.hpp) and the GUI's EditSettings.
//
// A recipe holds the five edit stages with their switches. It does not hold where the photo came
// from, how it is being viewed, the sliders' positions (they are worked out from the
// settings) or the RAW options (raw_half_size, raw_camera_matrix), which describe how the file is
// decoded rather than the look. So applying a recipe never makes the file decode again. Nor does
// it hold the strokes or the frame (#58), which belong to this photo: applying a recipe leaves
// both as they are, and the format stays at version 2.
//
// The GUI's colour matrix is normally none. A recipe always has a [matrix], so none is saved as
// the identity matrix without a `from`, and such a section loads as none again.

// Applies `recipe` to `s` by the partial rule: each stage the recipe has is set completely (its
// value and its switch); a stage it leaves out stays as it is. Of the curve's two parameters only
// the chosen shape's is set: a recipe for Reinhard leaves the gamma value alone. A matrix becomes
// a MatrixFile whose path is the recipe's `from` label (empty without one); identity rows without
// `from` become no matrix. The RAW options never change.
void apply_recipe(EditSettings& s, const Recipe& recipe);

// A complete recipe of `s`: every stage, each with its switch. The curve's parameter is kept only
// for the chosen shape (the other one is not saved); the matrix's `from` is its file's name.
[[nodiscard]] Recipe recipe_from(const EditSettings& s);

}  // namespace prism::gui
