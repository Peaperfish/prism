#include "prism/gui/recipe_ui.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "prism/gui/file_types.hpp"
#include "prism/gui/history.hpp"
#include "prism/gui/recipe_settings.hpp"
#include "prism/gui/stage_controls.hpp"

namespace prism::gui {
namespace {

namespace fs = std::filesystem;

Recipe complete_recipe() {
    EditSettings s;
    s.exposure_stops = 0.5f;
    s.saturation = 0.9f;
    s.contrast = 0.85f;
    s.curve = CurveKind::reinhard;
    Recipe r = recipe_from(s);
    r.name = "Warm low contrast";
    r.description = "Softer contrast and a warm grade";
    return r;
}

Recipe partial_recipe() {
    Recipe r;
    r.name = "Grade";
    r.matrix = RecipeMatrix{};
    r.matrix->values[0] = 1.05f;
    r.saturation = RecipeSaturation{0.9f, true};
    r.contrast = RecipeContrast{0.85f, true};
    return r;
}

// --- The stages line ---------------------------------------------------------------------------

TEST(RecipeStagesLine, ACompleteRecipeSetsAllFive) {
    EXPECT_EQ(stages_line(recipe_stages(complete_recipe())), "Sets all five stages.");
}

TEST(RecipeStagesLine, APartialRecipeNamesItsStagesInPipelineOrder) {
    EXPECT_EQ(stages_line(recipe_stages(partial_recipe())),
              "Sets only Colour matrix, Saturation and Contrast; the other stages are unchanged.");
}

TEST(RecipeStagesLine, OneStageAndTwoStagesReadAsASentence) {
    Recipe r;
    r.saturation = RecipeSaturation{1.2f, true};
    EXPECT_EQ(stages_line(recipe_stages(r)),
              "Sets only Saturation; the other stages are unchanged.");
    r.exposure = RecipeExposure{1.0f, true};
    EXPECT_EQ(stages_line(recipe_stages(r)),
              "Sets only Exposure and Saturation; the other stages are unchanged.");
}

TEST(RecipeStagesLine, ACompleteRecipeSaysWhichStagesItSwitchesOff) {
    Recipe r = complete_recipe();
    r.contrast->on = false;
    EXPECT_EQ(stages_line(recipe_stages(r)), "Sets all five stages and switches Contrast off.");
    r.exposure->on = false;
    EXPECT_EQ(stages_line(recipe_stages(r)),
              "Sets all five stages and switches Exposure and Contrast off.");
}

TEST(RecipeStagesLine, APartialRecipeSaysWhichStagesItSwitchesOff) {
    Recipe r = partial_recipe();
    r.contrast->on = false;
    EXPECT_EQ(stages_line(recipe_stages(r)),
              "Sets only Colour matrix, Saturation and Contrast, and switches Contrast off; the "
              "other stages are unchanged.");
}

TEST(RecipeStagesLine, ARecipeWithNoStagesSaysSo) {
    Recipe r;
    r.name = "Empty";
    const RecipeStages stages = recipe_stages(r);
    EXPECT_TRUE(stages.set.empty());
    EXPECT_EQ(stages_line(stages), "Sets no stages: the photo is unchanged.");
}

TEST(RecipeStagesLine, TheStagesAreThoseOfTheRecipe) {
    Recipe r = partial_recipe();
    r.saturation->on = false;
    const RecipeStages stages = recipe_stages(r);
    EXPECT_EQ(stages.set,
              (std::vector<StageId>{StageId::matrix, StageId::saturation, StageId::contrast}));
    EXPECT_EQ(stages.switched_off, (std::vector<StageId>{StageId::saturation}));
}

// --- The recipe line follows the settings -------------------------------------------------------

// Loads `r` the way the window does: one undo step, with the settings before and after.
LoadedRecipe load_into(History& history, const Recipe& r, std::string_view file) {
    history.end_gesture();
    const EditSettings before = history.current();
    EditSettings after = before;
    apply_recipe(after, r);
    history.set(after);
    return make_loaded_recipe(r, file, before, history.current());
}

TEST(RecipeLine, TheNameShowsRightAfterLoading) {
    History history(EditSettings{});
    const LoadedRecipe loaded = load_into(history, partial_recipe(), "grade.prism");
    EXPECT_EQ(recipe_line(loaded, history.current()), "Grade");
}

TEST(RecipeLine, UndoPastTheLoadHidesItAndRedoBringsItBack) {
    History history(EditSettings{});
    const LoadedRecipe loaded = load_into(history, partial_recipe(), "grade.prism");
    ASSERT_TRUE(history.undo());
    EXPECT_EQ(recipe_line(loaded, history.current()), std::nullopt);
    ASSERT_TRUE(history.redo());
    EXPECT_EQ(recipe_line(loaded, history.current()), "Grade");
}

TEST(RecipeLine, AnEditAfterTheLoadReadsModifiedAndUndoingItRestoresTheName) {
    History history(EditSettings{});
    const LoadedRecipe loaded = load_into(history, partial_recipe(), "grade.prism");
    EditSettings edited = history.current();
    edited.exposure_stops = 0.3f;
    history.set(edited);
    EXPECT_EQ(recipe_line(loaded, history.current()), "Grade (modified)");
    ASSERT_TRUE(history.undo());
    EXPECT_EQ(recipe_line(loaded, history.current()), "Grade");
}

TEST(RecipeLine, ANewPhotoForgetsTheRecipe) {
    History history(EditSettings{});
    RecipeTracker tracker;
    EXPECT_EQ(tracker.line(history.current()), std::nullopt);
    tracker.loaded(load_into(history, partial_recipe(), "grade.prism"));
    EXPECT_EQ(tracker.line(history.current()), "Grade");
    ASSERT_NE(tracker.current(), nullptr);
    tracker.photo_opened();
    EXPECT_EQ(tracker.current(), nullptr);
    EXPECT_EQ(tracker.line(history.current()), std::nullopt);
}

TEST(RecipeLine, ARecipeWithoutANameIsShownByItsFileName) {
    Recipe r = partial_recipe();
    r.name.reset();
    EXPECT_EQ(recipe_display_name(r, "teal and orange.PRISM"), "teal and orange");
    EXPECT_EQ(recipe_display_name(r, "notes.txt"), "notes.txt");
    r.name = "";
    EXPECT_EQ(recipe_display_name(r, "x.prism"), "x");
    const LoadedRecipe loaded = make_loaded_recipe(complete_recipe(), "w.prism", {}, {});
    EXPECT_EQ(loaded.name, "Warm low contrast");
    EXPECT_EQ(loaded.description, "Softer contrast and a warm grade");
    EXPECT_EQ(loaded.file_name, "w.prism");
}

// --- The recipe matrix label ---------------------------------------------------------------------

TEST(RecipeMatrixLabel, IsOnlyALabelAndAReSaveWritesNoFrom) {
    Recipe r;
    r.matrix = RecipeMatrix{};
    r.matrix->values[0] = 1.05f;  // a grade, with no `from`
    EditSettings s;
    apply_recipe(s, r);
    ASSERT_TRUE(s.matrix.has_value());
    EXPECT_EQ(stage_value_text(s, StageId::matrix), "recipe matrix");
    EXPECT_TRUE(s.matrix->path.empty());  // the label is not stored anywhere
    const Recipe again = recipe_from(s);
    ASSERT_TRUE(again.matrix.has_value());
    EXPECT_FALSE(again.matrix->from.has_value());
    EXPECT_EQ(write_recipe(again).find("from ="), std::string::npos);
    EXPECT_EQ(again.matrix->values, r.matrix->values);
    // A matrix with a `from` still shows that name.
    r.matrix->from = "grade.txt";
    apply_recipe(s, r);
    EXPECT_EQ(stage_value_text(s, StageId::matrix), "grade.txt");
}

// --- Name defaults for the save sheet ------------------------------------------------------------

TEST(RecipeNameDefault, UnchangedSinceLoadingGivesTheRecipesNameAndDescription) {
    History history(EditSettings{});
    const LoadedRecipe loaded = load_into(history, complete_recipe(), "w.prism");
    const RecipeNameDefault d = suggest_recipe_name(&loaded, InputKind::raw, history.current());
    EXPECT_EQ(d.name, "Warm low contrast");
    EXPECT_EQ(d.description, "Softer contrast and a warm grade");
}

TEST(RecipeNameDefault, ChangedSinceLoadingAddsModifiedOnce) {
    History history(EditSettings{});
    Recipe r = complete_recipe();
    r.name = "Vivid (modified)";  // saved earlier from a modified look
    const LoadedRecipe loaded = load_into(history, r, "v.prism");
    EditSettings edited = history.current();
    edited.contrast = 1.3f;
    history.set(edited);
    const RecipeNameDefault d = suggest_recipe_name(&loaded, InputKind::raw, history.current());
    EXPECT_EQ(d.name, "Vivid (modified)");
    EXPECT_EQ(modified_name("Vivid"), "Vivid (modified)");
    EXPECT_EQ(modified_name(modified_name("Vivid")), "Vivid (modified)");
}

TEST(RecipeNameDefault, AMatchingBuiltInLookGivesItsName) {
    EditSettings s;
    const Preset& vivid = presets_for(InputKind::raw)[2];
    apply_preset(s, vivid);
    const RecipeNameDefault d = suggest_recipe_name(nullptr, InputKind::raw, s);
    EXPECT_EQ(d.name, vivid.name);
    EXPECT_EQ(d.description, "");
}

TEST(RecipeNameDefault, OtherwiseItIsEmptyWithAHint) {
    EditSettings s;
    s.exposure_stops = 0.37f;
    const RecipeNameDefault d = suggest_recipe_name(nullptr, InputKind::raw, s);
    EXPECT_EQ(d.name, "");
    EXPECT_EQ(d.description, "");
    EXPECT_EQ(d.hint, "Name this look");
}

TEST(RecipeNameDefault, UndoneBackPastTheLoadCountsAsNoRecipe) {
    EditSettings start;
    start.exposure_stops = 0.37f;  // no preset's look
    History history(start);
    const LoadedRecipe loaded = load_into(history, complete_recipe(), "w.prism");
    ASSERT_TRUE(history.undo());
    EXPECT_EQ(suggest_recipe_name(&loaded, InputKind::raw, history.current()).name, "");
}

// --- A built-in look as a recipe -------------------------------------------------------------------

TEST(LookRecipeView, ShowsTheEmbeddedFileOfEachLook) {
    const auto check = [](InputKind kind, std::span<const BuiltinLookFile> files,
                          std::string_view kind_words) {
        const std::span<const Preset> presets = presets_for(kind);
        ASSERT_EQ(presets.size(), files.size());
        for (std::size_t i = 0; i < presets.size(); ++i) {
            const LookRecipeView view = look_recipe_view(kind, presets[i]);
            EXPECT_EQ(view.text, files[i].text) << files[i].source;
            EXPECT_NE(view.title.find(presets[i].name), std::string::npos) << view.title;
            EXPECT_NE(view.title.find(kind_words), std::string::npos) << view.title;
            EXPECT_EQ(view.note,
                      "A built-in look leaves the colour matrix and the RAW options alone.");
        }
    };
    check(InputKind::raw, builtin_raw_look_files(), "RAW");
    check(InputKind::image, builtin_image_look_files(), "PNG and JPEG");
}

TEST(LookRecipeView, ALookThatIsNotBuiltInHasNoFile) {
    Preset other = presets_for(InputKind::raw).front();
    other.id = "nothing";
    EXPECT_THROW((void)look_recipe_view(InputKind::raw, other), std::logic_error);
}

TEST(LookRecipeView, TheTwoKindsOfVividAreDifferentFiles) {
    const auto vivid = [](InputKind kind) -> const Preset& {
        for (const Preset& p : presets_for(kind)) {
            if (p.id == "vivid") return p;
        }
        throw std::logic_error("no vivid");
    };
    const LookRecipeView raw = look_recipe_view(InputKind::raw, vivid(InputKind::raw));
    const LookRecipeView image = look_recipe_view(InputKind::image, vivid(InputKind::image));
    EXPECT_NE(raw.text, image.text);
    EXPECT_NE(raw.title, image.title);
}

// --- File names ---------------------------------------------------------------------------------

TEST(RecipeFileName, IsTheNameWithPrismAdded) {
    EXPECT_EQ(suggested_recipe_file_name("Warm low contrast"), "Warm low contrast.prism");
    EXPECT_EQ(suggested_recipe_file_name("Vivid (modified)"), "Vivid (modified).prism");
}

TEST(RecipeFileName, CharactersFileSystemsRefuseAreTakenOut) {
    EXPECT_EQ(suggested_recipe_file_name(R"(a/b\c:d*e?f"g<h>i|j)"), "abcdefghij.prism");
    EXPECT_EQ(suggested_recipe_file_name(std::string("tab\there\nnew") + '\x7F'),
              "tabherenew.prism");
    EXPECT_EQ(suggested_recipe_file_name("Grün über Café"), "Grün über Café.prism");
}

TEST(RecipeFileName, TrailingDotsAndSpacesAreTrimmedAndNothingLeftIsRecipe) {
    EXPECT_EQ(suggested_recipe_file_name("Warm. . "), "Warm.prism");
    EXPECT_EQ(suggested_recipe_file_name(""), "recipe.prism");
    EXPECT_EQ(suggested_recipe_file_name("???"), "recipe.prism");
    EXPECT_EQ(suggested_recipe_file_name(" . "), "recipe.prism");
}

TEST(RecipeFileName, WithRecipeExtensionAddsPrismWhenMissing) {
    EXPECT_EQ(with_recipe_extension("warm"), fs::path("warm.prism"));
    EXPECT_EQ(with_recipe_extension("looks/warm.v2"), fs::path("looks/warm.v2.prism"));
    EXPECT_EQ(with_recipe_extension("warm.txt"), fs::path("warm.txt.prism"));
}

TEST(RecipeFileName, WithRecipeExtensionKeepsPrismInAnyCase) {
    EXPECT_EQ(with_recipe_extension("warm.prism"), fs::path("warm.prism"));
    EXPECT_EQ(with_recipe_extension("WARM.PRISM"), fs::path("WARM.PRISM"));
    EXPECT_EQ(with_recipe_extension("w.Prism"), fs::path("w.Prism"));
    EXPECT_TRUE(is_recipe_file("a.PrIsM"));
    EXPECT_FALSE(is_recipe_file("a.prism.jpg"));
    EXPECT_EQ(recipe_filter_pattern(), "prism");
}

// --- Where the dialogs start ---------------------------------------------------------------------

TEST(RecipeFolder, TheRememberedFolderWhenItExists) {
    std::vector<fs::path> asked;
    const auto exists = [&](const fs::path& p) {
        asked.push_back(p);
        return true;
    };
    EXPECT_EQ(recipe_start_folder("/looks", "/photos/a.jpg", exists), fs::path("/looks"));
    EXPECT_EQ(asked, (std::vector<fs::path>{"/looks"}));
}

TEST(RecipeFolder, ThePhotosFolderWhenTheRememberedOneIsGone) {
    const auto missing = [](const fs::path&) { return false; };
    EXPECT_EQ(recipe_start_folder("/looks", "/photos/a.jpg", missing), fs::path("/photos"));
}

TEST(RecipeFolder, ThePhotosFolderWhenNoneIsRemembered) {
    bool asked = false;
    const auto exists = [&](const fs::path&) {
        asked = true;
        return true;
    };
    EXPECT_EQ(recipe_start_folder("", "/photos/a.jpg", exists), fs::path("/photos"));
    EXPECT_FALSE(asked);
}

// --- Dropping files
// -------------------------------------------------------------------------------

DropAction drop(std::vector<fs::path> paths, bool has_photo, bool busy = false) {
    return classify_drop(paths, DropContext{has_photo, busy});
}

TEST(ClassifyDrop, ARecipeOnAnOpenPhotoIsApplied) {
    const DropAction a = drop({"/r/warm.prism"}, true);
    EXPECT_EQ(a.kind, DropAction::Kind::apply_recipe);
    EXPECT_EQ(a.path, fs::path("/r/warm.prism"));
    EXPECT_EQ(a.notice, "");
}

TEST(ClassifyDrop, SeveralRecipesApplyTheFirstAndSaySo) {
    const DropAction a = drop({"/r/a.PRISM", "/r/b.prism"}, true);
    EXPECT_EQ(a.kind, DropAction::Kind::apply_recipe);
    EXPECT_EQ(a.path, fs::path("/r/a.PRISM"));
    EXPECT_EQ(a.notice, "Prism applies one recipe at a time.");
}

TEST(ClassifyDrop, APhotoAndARecipeOpenThePhotoOnly) {
    for (const bool has_photo : {false, true}) {
        const DropAction a = drop({"/r/warm.prism", "/p/b.jpg"}, has_photo);
        EXPECT_EQ(a.kind, DropAction::Kind::open_photo);
        EXPECT_EQ(a.path, fs::path("/p/b.jpg"));
        EXPECT_EQ(a.notice, "The recipe was not applied: drop it again once the photo is open.");
    }
}

TEST(ClassifyDrop, ARecipeWithNoPhotoOpenOnlySaysWhatToDo) {
    const DropAction a = drop({"/r/warm.prism"}, false);
    EXPECT_EQ(a.kind, DropAction::Kind::notice_only);
    EXPECT_EQ(a.notice, "Open a photo first, then drop the recipe on it.");
}

TEST(ClassifyDrop, PhotosAreOpenedAsBefore) {
    DropAction a = drop({"/p/a.ARW"}, true);
    EXPECT_EQ(a.kind, DropAction::Kind::open_photo);
    EXPECT_EQ(a.path, fs::path("/p/a.ARW"));
    EXPECT_EQ(a.notice, "");
    a = drop({"/p/a.jpg", "/p/b.jpg"}, false);
    EXPECT_EQ(a.path, fs::path("/p/a.jpg"));
    EXPECT_EQ(a.notice, "Prism opens one photo at a time.");
}

TEST(ClassifyDrop, NothingHappensWhileSomethingModalIsInProgress) {
    for (const std::vector<fs::path>& paths :
         {std::vector<fs::path>{"/r/warm.prism"}, std::vector<fs::path>{"/p/a.jpg"},
          std::vector<fs::path>{"/p/a.jpg", "/r/w.prism"}}) {
        for (const bool has_photo : {false, true}) {
            const DropAction a = drop(paths, has_photo, /*busy=*/true);
            EXPECT_EQ(a.kind, DropAction::Kind::ignore);
            EXPECT_EQ(a.notice, "");
        }
    }
    EXPECT_EQ(drop({}, true).kind, DropAction::Kind::ignore);
}

// --- The error window
// -----------------------------------------------------------------------------

TEST(RecipeErrorView, RowsHaveTheLineNumberOrNothingForTheWholeFile) {
    const RecipeError error("looks/warm.prism",
                            {{0, "the file does not exist"}, {4, "unknown setting 'amout'"}});
    const RecipeErrorView view = recipe_error_view(error, "warm.prism");
    EXPECT_EQ(view.header, "Nothing was changed. 'warm.prism' has 2 mistakes:");
    EXPECT_EQ(view.rows, (std::vector<RecipeErrorRow>{{"", "the file does not exist"},
                                                      {"4", "unknown setting 'amout'"}}));
    EXPECT_EQ(view.note, "");
    EXPECT_EQ(view.copy_text, std::string(error.what()));
}

TEST(RecipeErrorView, OneMistakeIsSingular) {
    const RecipeError error("w.prism", {{3, "'0,9' is not a number"}});
    EXPECT_EQ(recipe_error_view(error, "w.prism").header,
              "Nothing was changed. 'w.prism' has 1 mistake:");
}

TEST(RecipeErrorView, AtTheLimitItSaysPrismStopsThere) {
    std::vector<RecipeIssue> issues;
    for (int i = 1; i <= static_cast<int>(recipe_max_errors); ++i) {
        issues.push_back({i + 1, "mistake " + std::to_string(i)});
    }
    const RecipeErrorView view = recipe_error_view(RecipeError("w.prism", issues), "w.prism");
    EXPECT_EQ(view.rows.size(), recipe_max_errors);
    EXPECT_EQ(view.header, "Nothing was changed. 'w.prism' has 10 mistakes:");
    EXPECT_EQ(view.note, "Prism stops at 10. Fix these and load again.");
}

}  // namespace
}  // namespace prism::gui
