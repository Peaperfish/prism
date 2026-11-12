#include "prism/gui/recipe_ui.hpp"

#include <algorithm>
#include <utility>

#include "prism/gui/file_types.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view modified_suffix = " (modified)";
constexpr std::string_view prism_extension = ".prism";

// "A", "A and B", "A, B and C".
std::string join_names(const std::vector<StageId>& stages) {
    std::string text;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (i > 0) {
            text += i + 1 == stages.size() ? " and " : ", ";
        }
        text += stage_info(stages[i]).name;
    }
    return text;
}

bool ends_with_prism(std::string_view name) {
    if (name.size() < prism_extension.size()) {
        return false;
    }
    const std::string_view tail = name.substr(name.size() - prism_extension.size());
    for (std::size_t i = 0; i < tail.size(); ++i) {
        char c = tail[i];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c != prism_extension[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

RecipeStages recipe_stages(const Recipe& recipe) {
    RecipeStages stages;
    const auto add = [&](StageId id, bool present, bool on) {
        if (present) {
            stages.set.push_back(id);
            if (!on) {
                stages.switched_off.push_back(id);
            }
        }
    };
    add(StageId::exposure, recipe.exposure.has_value(), recipe.exposure && recipe.exposure->on);
    add(StageId::white_balance, recipe.white_balance.has_value(),
        recipe.white_balance && recipe.white_balance->on);
    add(StageId::matrix, recipe.matrix.has_value(), recipe.matrix && recipe.matrix->on);
    add(StageId::saturation, recipe.saturation.has_value(),
        recipe.saturation && recipe.saturation->on);
    add(StageId::contrast, recipe.contrast.has_value(), recipe.contrast && recipe.contrast->on);
    add(StageId::curve, recipe.curve.has_value(), recipe.curve && recipe.curve->on);
    return stages;
}

std::string stages_line(const RecipeStages& stages) {
    if (stages.set.empty()) {
        return "Sets no stages: the photo is unchanged.";
    }
    const std::string off = stages.switched_off.empty()
                                ? std::string()
                                : "switches " + join_names(stages.switched_off) + " off";
    // The five stages every saved recipe has, and White balance, which only a photo with a chosen
    // light adds: it is named on its own, so "all five" stays what it always was.
    const bool white_balance = std::find(stages.set.begin(), stages.set.end(),
                                         StageId::white_balance) != stages.set.end();
    if (stages.set.size() == (white_balance ? 6u : 5u)) {
        const std::string all =
            white_balance ? "Sets all five stages and White balance" : "Sets all five stages";
        return off.empty() ? all + "." : all + (white_balance ? ", and " : " and ") + off + ".";
    }
    std::string text = "Sets only " + join_names(stages.set);
    if (!off.empty()) {
        text += ", and " + off;
    }
    return text + "; the other stages are unchanged.";
}

std::string recipe_display_name(const Recipe& recipe, std::string_view file_name) {
    if (recipe.name && !recipe.name->empty()) {
        return *recipe.name;
    }
    if (ends_with_prism(file_name)) {
        file_name.remove_suffix(prism_extension.size());
    }
    return std::string(file_name);
}

LoadedRecipe make_loaded_recipe(const Recipe& recipe, std::string_view file_name,
                                const EditSettings& before, const EditSettings& after) {
    LoadedRecipe loaded;
    loaded.name = recipe_display_name(recipe, file_name);
    loaded.description = recipe.description.value_or("");
    loaded.file_name = std::string(file_name);
    loaded.stages = recipe_stages(recipe);
    loaded.before = before;
    loaded.after = after;
    return loaded;
}

std::string modified_name(std::string_view name) {
    if (name.size() >= modified_suffix.size() &&
        name.substr(name.size() - modified_suffix.size()) == modified_suffix) {
        return std::string(name);
    }
    return std::string(name) + std::string(modified_suffix);
}

std::optional<std::string> recipe_line(const LoadedRecipe& loaded, const EditSettings& current) {
    // Only what a recipe holds: a stroke or a frame made afterwards doesn't modify it (#58).
    if (same_recipe(current, loaded.after)) {
        return loaded.name;
    }
    if (same_recipe(current, loaded.before)) {
        return std::nullopt;
    }
    return modified_name(loaded.name);
}

std::optional<std::string> RecipeTracker::line(const EditSettings& settings) const {
    if (!loaded_) {
        return std::nullopt;
    }
    return recipe_line(*loaded_, settings);
}

LookRecipeView look_recipe_view(InputKind kind, const Preset& preset) {
    const std::span<const BuiltinLookFile> files =
        kind == InputKind::raw ? builtin_raw_look_files() : builtin_image_look_files();
    for (const BuiltinLookFile& file : files) {
        if (file.id == preset.id) {
            return {std::string(preset.name) +
                        (kind == InputKind::raw ? " recipe (RAW)" : " recipe (PNG and JPEG)"),
                    std::string(file.text),
                    "A built-in look leaves the colour matrix and the RAW options alone."};
        }
    }
    throw std::logic_error("no built-in file for the look " + std::string(preset.id));
}

RecipeNameDefault suggest_recipe_name(const LoadedRecipe* loaded, InputKind kind,
                                      const EditSettings& current) {
    if (loaded != nullptr) {
        if (std::optional<std::string> line = recipe_line(*loaded, current)) {
            return {std::move(*line), loaded->description, recipe_name_hint};
        }
    }
    if (const Preset* preset = matching_preset(kind, current)) {
        return {std::string(preset->name), std::string(preset->description), recipe_name_hint};
    }
    return {"", "", recipe_name_hint};
}

std::string suggested_recipe_file_name(std::string_view name) {
    std::string stem;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            continue;
        }
        if (std::string_view(R"(/\:*?"<>|)").find(c) != std::string_view::npos) {
            continue;
        }
        stem += c;
    }
    while (!stem.empty() && (stem.back() == '.' || stem.back() == ' ')) {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "recipe";
    }
    return stem + std::string(prism_extension);
}

fs::path recipe_start_folder(const fs::path& remembered, const fs::path& photo,
                             const std::function<bool(const fs::path&)>& folder_exists) {
    if (!remembered.empty() && folder_exists && folder_exists(remembered)) {
        return remembered;
    }
    return photo.parent_path();
}

DropAction classify_drop(std::span<const fs::path> paths, const DropContext& context) {
    DropAction action;
    if (context.busy || paths.empty()) {
        return action;
    }
    const fs::path* first_photo = nullptr;
    const fs::path* first_recipe = nullptr;
    std::size_t photos = 0;
    std::size_t recipes = 0;
    for (const fs::path& path : paths) {
        if (is_recipe_file(path)) {
            ++recipes;
            if (first_recipe == nullptr) {
                first_recipe = &path;
            }
        } else {
            ++photos;
            if (first_photo == nullptr) {
                first_photo = &path;
            }
        }
    }
    if (first_photo != nullptr) {
        action.kind = DropAction::Kind::open_photo;
        action.path = *first_photo;
        if (recipes > 0) {
            action.notice = "The recipe was not applied: drop it again once the photo is open.";
        } else if (photos > 1) {
            action.notice = "Prism opens one photo at a time.";
        }
        return action;
    }
    if (!context.has_photo) {
        action.kind = DropAction::Kind::notice_only;
        action.notice = "Open a photo first, then drop the recipe on it.";
        return action;
    }
    action.kind = DropAction::Kind::apply_recipe;
    action.path = *first_recipe;
    if (recipes > 1) {
        action.notice = "Prism applies one recipe at a time.";
    }
    return action;
}

std::string_view recipe_sheet_strokes_note(const EditSettings& settings) noexcept {
    const bool strokes = !settings.strokes.empty();
    const bool frame = settings.frame.has_value();
    return strokes && frame ? recipe_frame_strokes_note
           : frame          ? recipe_frame_note
           : strokes        ? recipe_strokes_note
                            : std::string_view();
}

RecipeErrorView recipe_error_view(const RecipeError& error, std::string_view file_name) {
    RecipeErrorView view;
    const std::size_t count = error.issues().size();
    view.header = "Nothing was changed. '" + std::string(file_name) + "' has " +
                  std::to_string(count) + (count == 1 ? " mistake:" : " mistakes:");
    for (const RecipeIssue& issue : error.issues()) {
        view.rows.push_back(
            {issue.line > 0 ? std::to_string(issue.line) : std::string(), issue.message});
    }
    if (count >= recipe_max_errors) {
        view.note =
            "Prism stops at " + std::to_string(recipe_max_errors) + ". Fix these and load again.";
    }
    view.copy_text = error.what();
    return view;
}

}  // namespace prism::gui
