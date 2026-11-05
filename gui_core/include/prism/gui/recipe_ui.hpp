#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/presets.hpp"
#include "prism/gui/settings.hpp"
#include "prism/recipe.hpp"

namespace prism::gui {

// What the window says and decides about recipes (#18): saving the edit as a .prism file,
// loading one onto the open photo, and dropping one on the window. Pure functions, so the texts
// and the decisions are tested without a window.

// --- What a recipe sets --------------------------------------------------------------------------

// The edit stages a recipe has (in pipeline order) and which of those it switches off.
struct RecipeStages {
    std::vector<StageId> set;
    std::vector<StageId> switched_off;
    bool operator==(const RecipeStages&) const = default;
};
[[nodiscard]] RecipeStages recipe_stages(const Recipe& recipe);

// The muted line under the recipe's name, in plain words:
//   all five:        "Sets all five stages."
//   some:            "Sets only Colour matrix, Saturation and Contrast; the other stages are
//                     unchanged."
//   with one off:    "Sets all five stages and switches Contrast off."
//                    "Sets only Saturation and Contrast, and switches Contrast off; the other
//                     stages are unchanged."
//   none:            "Sets no stages: the photo is unchanged."
[[nodiscard]] std::string stages_line(const RecipeStages& stages);

// --- The recipe that was loaded ------------------------------------------------------------------

// A recipe applied to the open photo, with the settings just before and just after, so the
// window can tell whether what is on screen is still that recipe.
struct LoadedRecipe {
    std::string name;         // the recipe's name, else the file name without .prism
    std::string description;  // may be empty
    std::string file_name;    // without folders
    RecipeStages stages;
    EditSettings before;
    EditSettings after;
};

// The name a recipe is shown by: its own name when it has one, else `file_name` without its
// .prism extension (any case): "warm.prism" -> "warm".
[[nodiscard]] std::string recipe_display_name(const Recipe& recipe, std::string_view file_name);

[[nodiscard]] LoadedRecipe make_loaded_recipe(const Recipe& recipe, std::string_view file_name,
                                              const EditSettings& before,
                                              const EditSettings& after);

// "<name> (modified)", without ever doubling it: a name that already ends in " (modified)" is
// returned as it is.
[[nodiscard]] std::string modified_name(std::string_view name);

// The recipe line under the Look menu, which follows the settings through undo and redo:
//   current == after:   the name ("Warm low contrast");
//   current == before:  nothing (undone past the load; redo brings it back);
//   anything else:      "<name> (modified)".
// The window shows it as "Recipe: <line>".
[[nodiscard]] std::optional<std::string> recipe_line(const LoadedRecipe& loaded,
                                                     const EditSettings& current);

// The loaded recipe of the open photo, if any. Forgotten when another photo opens.
class RecipeTracker {
public:
    void loaded(LoadedRecipe recipe) { loaded_ = std::move(recipe); }
    void photo_opened() { loaded_.reset(); }
    [[nodiscard]] const LoadedRecipe* current() const noexcept {
        return loaded_ ? &*loaded_ : nullptr;
    }
    // recipe_line for `settings`, or nothing when no recipe was loaded.
    [[nodiscard]] std::optional<std::string> line(const EditSettings& settings) const;

private:
    std::optional<LoadedRecipe> loaded_;
};

// --- A built-in look as a recipe -------------------------------------------------------------------

// What the View recipe window shows. It owns its text: the window keeps showing the look it was
// opened for, whatever happens to the settings afterwards.
struct LookRecipeView {
    std::string title;  // the look's name and the kind of photo: the two kinds' files differ
    std::string text;   // the embedded file, exactly (builtin_looks.hpp)
    std::string note;
};

// `preset` must be one of presets_for(kind).
[[nodiscard]] LookRecipeView look_recipe_view(InputKind kind, const Preset& preset);

// --- Saving --------------------------------------------------------------------------------------

// What the save sheet starts with.
struct RecipeNameDefault {
    std::string name;
    std::string description;
    std::string_view hint;  // shown greyed in an empty Name field
};

inline constexpr std::string_view recipe_name_hint = "Name this look";

// The name and description the save sheet suggests for `current`:
//  - unchanged since a recipe was loaded: that recipe's name and description;
//  - changed since then: "<name> (modified)" and its description (undone back past the load
//    counts as no recipe, as for recipe_line);
//  - the look of a built-in preset (matching_preset): the preset's name and description;
//  - otherwise empty, with the hint "Name this look".
[[nodiscard]] RecipeNameDefault suggest_recipe_name(const LoadedRecipe* loaded, InputKind kind,
                                                    const EditSettings& current);

// The file name the save dialog suggests for a recipe named `name`: the name with the characters
// that are not allowed in file names somewhere (/ \ : * ? " < > | and control characters)
// taken out and trailing dots and spaces trimmed, falling back to "recipe", plus ".prism".
// "Warm: low/contrast." -> "Warm lowcontrast.prism".
[[nodiscard]] std::string suggested_recipe_file_name(std::string_view name);

// Where the recipe dialogs start: the remembered recipe folder if there is one and it still
// exists, else the open photo's folder. `folder_exists` is std::filesystem::is_directory in the
// window; tests pass their own.
[[nodiscard]] std::filesystem::path recipe_start_folder(
    const std::filesystem::path& remembered, const std::filesystem::path& photo,
    const std::function<bool(const std::filesystem::path&)>& folder_exists);

// --- Dropping files ------------------------------------------------------------------------------

struct DropContext {
    bool has_photo = false;
    // The close prompt, the save sheet, a file dialog or an export is in progress: a drop then
    // does nothing at all.
    bool busy = false;
};

struct DropAction {
    enum class Kind {
        ignore,        // nothing happens
        open_photo,    // open `path` as a photo (asking about unsaved edits first, as ever)
        apply_recipe,  // load the recipe at `path` onto the open photo: an edit, not a close
        notice_only,   // nothing opens; just show `notice`
    };
    Kind kind = Kind::ignore;
    std::filesystem::path path;
    std::string notice;  // a note for the banner; empty clears it
};

// What a drop of `paths` does. A .prism file (any case) is a recipe; anything else is taken for
// a photo, as before.
//  - busy, or nothing dropped: ignore;
//  - any photo: open the first photo; with a recipe in the drop too, the notice "The recipe was
//    not applied: drop it again once the photo is open.", else with several photos "Prism opens
//    one photo at a time.";
//  - only recipes, with a photo open: apply the first; with several, the notice "Prism applies
//    one recipe at a time.";
//  - only recipes and no photo: the notice "Open a photo first, then drop the recipe on it."
[[nodiscard]] DropAction classify_drop(std::span<const std::filesystem::path> paths,
                                       const DropContext& context);

// --- When a recipe can't be loaded ---------------------------------------------------------------

struct RecipeErrorRow {
    std::string line;  // "4"; empty for a mistake in the file as a whole (line 0)
    std::string message;
    bool operator==(const RecipeErrorRow&) const = default;
};

// What the "Recipe not loaded" window shows.
struct RecipeErrorView {
    std::string header;  // "Nothing was changed. 'warm.prism' has 3 mistakes:" (1 mistake)
    std::vector<RecipeErrorRow> rows;
    std::string note;  // "Prism stops at 10. Fix these and load again." at the limit, else empty
    std::string copy_text;  // what Copy puts on the clipboard: the error's what()
};
[[nodiscard]] RecipeErrorView recipe_error_view(const RecipeError& error,
                                                std::string_view file_name);

}  // namespace prism::gui
