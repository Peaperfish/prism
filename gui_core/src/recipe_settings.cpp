#include "prism/gui/recipe_settings.hpp"

#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace {

CurveKind kind_of(CurveShape shape) {
    switch (shape) {
        case CurveShape::none: return CurveKind::none;
        case CurveShape::gamma: return CurveKind::gamma;
        case CurveShape::reinhard: return CurveKind::reinhard;
        case CurveShape::filmic: return CurveKind::filmic;
    }
    return CurveKind::none;
}

CurveShape shape_of(CurveKind kind) {
    switch (kind) {
        case CurveKind::none: return CurveShape::none;
        case CurveKind::gamma: return CurveShape::gamma;
        case CurveKind::reinhard: return CurveShape::reinhard;
        case CurveKind::filmic: return CurveShape::filmic;
    }
    return CurveShape::none;
}

}  // namespace

void apply_recipe(EditSettings& s, const Recipe& recipe) {
    if (const auto& e = recipe.exposure) {
        s.exposure_stops = e->stops;
        s.exposure_on = e->on;
    }
    if (const auto& m = recipe.matrix) {
        if (m->values == identity_matrix && !m->from) {
            s.matrix.reset();
        } else {
            s.matrix = MatrixFile{path_from_utf8(m->from.value_or("")), m->values};
        }
        s.matrix_on = m->on;
    }
    if (const auto& sat = recipe.saturation) {
        s.saturation = sat->amount;
        s.saturation_on = sat->on;
    }
    if (const auto& c = recipe.contrast) {
        s.contrast = c->amount;
        s.contrast_on = c->on;
    }
    if (const auto& c = recipe.curve) {
        s.curve = kind_of(c->shape);
        if (c->shape == CurveShape::gamma) {
            s.gamma = c->gamma;
        } else if (c->shape == CurveShape::reinhard) {
            s.reinhard_white = c->white;
        }
        s.curve_on = c->on;
    }
}

Recipe recipe_from(const EditSettings& s) {
    // Every field of EditSettings, by name and in order. A field added to EditSettings makes this
    // line fail to compile until it is named here and either saved below or left out on purpose;
    // recipe_settings_test.cpp then says which, and why.
    const auto& [exposure_stops, exposure_on, matrix, matrix_on, saturation, saturation_on,
                 contrast, contrast_on, curve, gamma, reinhard_white, curve_on, raw_half_size,
                 raw_camera_matrix] = s;
    // Not in a recipe: they say how the file is decoded, not how it looks.
    (void)raw_half_size;
    (void)raw_camera_matrix;

    Recipe r;
    r.exposure = RecipeExposure{exposure_stops, exposure_on};
    RecipeMatrix m;
    if (matrix) {
        m.values = matrix->values;
        if (!matrix->path.empty()) {
            m.from = utf8(matrix->path.filename());
        }
    }
    m.on = matrix_on;
    r.matrix = m;
    r.saturation = RecipeSaturation{saturation, saturation_on};
    r.contrast = RecipeContrast{contrast, contrast_on};
    RecipeCurve c;
    c.shape = shape_of(curve);
    if (curve == CurveKind::gamma) {
        c.gamma = gamma;
    } else if (curve == CurveKind::reinhard) {
        c.white = reinhard_white;
    }
    c.on = curve_on;
    r.curve = c;
    return r;
}

}  // namespace prism::gui
