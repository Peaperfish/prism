#include "prism/gui/recipe_ui.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "prism/gui/file_types.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/stage_controls.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view modified_suffix = " (modified)";
constexpr std::string_view prism_extension = ".prism";

// "A", "A and B", "A, B and C".
std::string join_texts(const std::vector<std::string>& names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            text += i + 1 == names.size() ? " and " : ", ";
        }
        text += names[i];
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
    // [hdr] sets Output encode's HDR (#105): on at its headroom, or off at 0. Output encode has no
    // switch of its own, so it is never "switched off".
    add(StageId::encode, recipe.hdr.has_value(), true);
    stages.hdr_headroom = recipe.hdr ? recipe.hdr->headroom : 0.0f;
    return stages;
}

std::string stages_line(const RecipeStages& stages) {
    if (stages.set.empty()) {
        return "Sets no stages: the photo is unchanged.";
    }
    const auto sets = [&](StageId id) {
        return std::find(stages.set.begin(), stages.set.end(), id) != stages.set.end();
    };
    // [hdr] sets Output encode's HDR: "HDR at +2.00 stops", or, at a headroom of 0, "turns HDR
    // off". Output encode is never named, since it has no switch of its own.
    const bool hdr_set = sets(StageId::encode);
    const bool hdr_on = hdr_set && stages.hdr_headroom > 0.0f;
    std::vector<std::string> names;  // the stages set, in pipeline order
    std::vector<std::string> clauses;
    std::vector<std::string> switched_off;
    for (const StageId id : stages.set) {
        if (id != StageId::encode) {
            names.emplace_back(stage_info(id).name);
        }
    }
    for (const StageId id : stages.switched_off) {
        switched_off.emplace_back(stage_info(id).name);
    }
    if (!switched_off.empty()) {
        clauses.push_back("switches " + join_texts(switched_off) + " off");
    }
    if (hdr_set && !hdr_on) {
        clauses.push_back("turns HDR off");
    }
    if (hdr_on) {
        names.push_back("HDR at " + headroom_text(stages.hdr_headroom));
    }
    const std::string off = join_texts(clauses);
    // The five stages every saved recipe has, and White balance, which only a photo with a chosen
    // light adds, and HDR, which only a recipe with [hdr] sets: they are named on their own, so
    // "all five" stays what it always was.
    std::vector<std::string> extras;
    if (sets(StageId::white_balance)) {
        extras.emplace_back(stage_info(StageId::white_balance).name);
    }
    if (hdr_on) {
        extras.push_back(names.back());
    }
    const std::size_t extra_count = (sets(StageId::white_balance) ? 1u : 0u) + (hdr_set ? 1u : 0u);
    if (stages.set.size() == 5u + extra_count) {
        const std::string all = extras.empty() ? "Sets all five stages"
                                               : "Sets all five stages and " + join_texts(extras);
        return off.empty() ? all + "." : all + (extras.empty() ? " and " : ", and ") + off + ".";
    }
    if (names.empty()) {
        return "Turns HDR off; the other stages are unchanged.";
    }
    std::string text = "Sets only " + join_texts(names);
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

RecipeMatch match_recipe(const LoadedRecipe& loaded, const EditSettings& current) {
    // Only what a recipe holds: a stroke or a frame made afterwards doesn't modify it (#58).
    const LoadedRecipe* e = &loaded;
    for (std::size_t depth = 0;; ++depth) {
        if (same_recipe(current, e->after)) {
            return {e, depth, true};
        }
        if (!e->earlier) {
            return same_recipe(current, e->before) ? RecipeMatch{nullptr, depth, true}
                                                   : RecipeMatch{};
        }
        if (same_recipe(current, e->before)) {
            // Undone past a look picked over it. The settings may also be where the earlier
            // entry started (a look picked before the recipe was loaded, then undone): that
            // match comes first, and only otherwise the earlier entry stands (a look picked over
            // a modified recipe).
            RecipeMatch earlier = match_recipe(*e->earlier, current);
            if (earlier.known) {
                earlier.depth += depth + 1;
                return earlier;
            }
            return {e->earlier.get(), depth + 1, true};
        }
        e = e->earlier.get();
    }
}

const LoadedRecipe* shown_recipe(const LoadedRecipe& loaded, const EditSettings& current,
                                 std::size_t active_depth) {
    const RecipeMatch match = match_recipe(loaded, current);
    if (match.known) {
        return match.entry;
    }
    const LoadedRecipe* e = &loaded;
    for (std::size_t depth = 0; depth < active_depth && e->earlier; ++depth) {
        e = e->earlier.get();
    }
    return e;
}

std::optional<std::string> recipe_line(const LoadedRecipe& loaded, const EditSettings& current,
                                       std::size_t active_depth) {
    const LoadedRecipe* shown = shown_recipe(loaded, current, active_depth);
    if (shown == nullptr) {
        return std::nullopt;
    }
    return same_recipe(current, shown->after) ? shown->name : modified_name(shown->name);
}

std::optional<std::string> RecipeTracker::line(const EditSettings& settings) const {
    if (!loaded_) {
        return std::nullopt;
    }
    return recipe_line(*loaded_, settings, active_);
}

const LoadedRecipe* RecipeTracker::shown(const EditSettings& settings) const {
    return loaded_ ? shown_recipe(*loaded_, settings, active_) : nullptr;
}

void RecipeTracker::observe(const EditSettings& settings) {
    if (loaded_) {
        const RecipeMatch match = match_recipe(*loaded_, settings);
        if (match.known) {
            active_ = match.depth;
        }
    }
}

void RecipeTracker::look_picked(const Preset& preset, const EditSettings& before,
                                const EditSettings& after) {
    if (!loaded_) {
        return;
    }
    // The look already shown, picked again: nothing to replace.
    if (loaded_->is_look && loaded_->name == preset.name && same_recipe(loaded_->after, after) &&
        same_recipe(before, after)) {
        return;
    }
    LoadedRecipe look;
    look.name = std::string(preset.name);
    look.description = std::string(preset.description);
    // A look sets these four and leaves the colour matrix alone (look_recipe_view).
    look.stages.set = {StageId::exposure, StageId::saturation, StageId::contrast, StageId::curve};
    look.before = before;
    look.after = after;
    look.is_look = true;
    look.earlier = std::make_shared<const LoadedRecipe>(std::move(*loaded_));
    loaded_ = std::move(look);
    active_ = 0;
}

std::string look_button_text(const RecipeTracker& tracker, const Preset* current,
                             const EditSettings& settings) {
    const std::optional<std::string> line = tracker.line(settings);
    const LoadedRecipe* shown = tracker.shown(settings);
    if (line && shown != nullptr && (current == nullptr || settings == shown->after)) {
        return shown->is_look && *line == shown->name ? *line : "Recipe: " + *line;
    }
    return current != nullptr ? std::string(current->name) : "Custom";
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
                                      const EditSettings& current, std::size_t active_depth) {
    if (loaded != nullptr) {
        if (const LoadedRecipe* shown = shown_recipe(*loaded, current, active_depth)) {
            return {recipe_line(*loaded, current, active_depth).value_or(""), shown->description,
                    recipe_name_hint};
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
