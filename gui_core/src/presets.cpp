#include "prism/gui/presets.hpp"

#include <cstddef>
#include <utility>

#include "prism/gui/recipe_settings.hpp"
#include "prism/recipe.hpp"

namespace prism::gui {

namespace {

const LookTable& raw_looks() {
    static const LookTable looks(builtin_raw_look_files());
    return looks;
}

const LookTable& image_looks() {
    static const LookTable looks(builtin_image_look_files());
    return looks;
}

// What is wrong with `recipe` as a look, as messages for the whole file.
std::vector<RecipeIssue> look_issues(const Recipe& recipe) {
    std::vector<RecipeIssue> issues;
    const auto need = [&](bool ok, std::string message) {
        if (!ok) {
            issues.push_back({0, std::move(message)});
        }
    };
    const auto stage = [&](const auto& section, std::string_view name) {
        need(section.has_value(), "a look must have [" + std::string(name) + "]");
        if (section) {
            need(section->on, "a look can't switch off [" + std::string(name) + "]");
        }
    };
    need(!recipe.matrix.has_value(),
         "a look leaves the colour matrix alone: it can't have [matrix]");
    stage(recipe.exposure, "exposure");
    stage(recipe.saturation, "saturation");
    stage(recipe.contrast, "contrast");
    stage(recipe.curve, "curve");
    need(recipe.name && !recipe.name->empty(), "a look must have a name");
    need(recipe.description && !recipe.description->empty(), "a look must have a description");
    return issues;
}

// The look as it acts on the image: an off stage is its neutral value.
struct EffectiveLook {
    float exposure;
    float saturation;
    float contrast;
    CurveKind curve;
    float curve_parameter;  // gamma or Reinhard white; 0 for the curves without one

    bool operator==(const EffectiveLook&) const = default;
};

EffectiveLook effective(const EditSettings& s) {
    const CurveKind curve = s.curve_on ? s.curve : CurveKind::none;
    float parameter = 0.0f;
    if (curve == CurveKind::gamma) {
        parameter = s.gamma;
    } else if (curve == CurveKind::reinhard) {
        parameter = s.reinhard_white;
    }
    return {s.exposure_on ? s.exposure_stops : 0.0f, s.saturation_on ? s.saturation : 1.0f,
            s.contrast_on ? s.contrast : 1.0f, curve, parameter};
}

}  // namespace

LookTable::LookTable(std::span<const BuiltinLookFile> files) {
    // All the text first and only then the views of it: moving a short std::string, as a growing
    // vector would, moves its characters too.
    texts_.reserve(files.size());
    std::vector<EditSettings> settings;
    settings.reserve(files.size());
    for (const BuiltinLookFile& file : files) {
        const std::string source(file.source);
        const Recipe recipe = parse_recipe(file.text, source);
        std::vector<RecipeIssue> issues = look_issues(recipe);
        if (!issues.empty()) {
            throw RecipeError(source, std::move(issues));
        }
        EditSettings s;
        apply_recipe(s, recipe);
        for (std::string& problem : validate(s)) {
            issues.push_back({0, std::move(problem)});
        }
        if (!issues.empty()) {
            throw RecipeError(source, std::move(issues));
        }
        texts_.push_back({std::string(file.id), *recipe.name, *recipe.description});
        settings.push_back(s);
    }
    presets_.reserve(files.size());
    for (std::size_t i = 0; i < texts_.size(); ++i) {
        presets_.push_back({texts_[i].id, texts_[i].name, texts_[i].description, settings[i]});
    }
}

void load_builtin_looks() {
    (void)raw_looks();
    (void)image_looks();
}

std::span<const Preset> presets_for(InputKind kind) {
    return kind == InputKind::raw ? raw_looks().presets() : image_looks().presets();
}

const Preset& default_preset(InputKind kind) {
    return presets_for(kind).front();
}

void apply_preset(EditSettings& s, const Preset& preset) {
    const EditSettings& p = preset.settings;
    s.exposure_stops = p.exposure_stops;
    s.saturation = p.saturation;
    s.contrast = p.contrast;
    s.curve = p.curve;
    s.gamma = p.gamma;
    s.reinhard_white = p.reinhard_white;
    s.exposure_on = s.saturation_on = s.contrast_on = s.curve_on = true;
}

bool same_look(const EditSettings& a, const EditSettings& b) {
    return effective(a) == effective(b);
}

const Preset* matching_preset(InputKind kind, const EditSettings& s) {
    for (const Preset& p : presets_for(kind)) {
        if (same_look(p.settings, s)) {
            return &p;
        }
    }
    return nullptr;
}

}  // namespace prism::gui
