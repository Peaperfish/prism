#include "prism/gui/close_guard.hpp"

#include <utility>

namespace prism::gui {

void SavedBaseline::opened(const EditSettings& settings) {
    opened_ = settings;
    exported_.reset();
    recipe_.reset();
    last_.reset();
}

void SavedBaseline::exported(const EditSettings& settings, std::string file) {
    exported_ = settings;
    last_ = LastSave{SaveKind::export_image, std::move(file)};
}

void SavedBaseline::recipe_saved(const EditSettings& settings, std::string file) {
    recipe_ = settings;
    last_ = LastSave{SaveKind::recipe, std::move(file)};
}

bool SavedBaseline::dirty(const EditSettings& current) const {
    if (!exported_ && !recipe_) {
        return !(current == opened_);
    }
    return !(exported_ && current == *exported_) && !(recipe_ && current == *recipe_);
}

CloseStep close_requested(const CloseState& state) noexcept {
    if (!state.has_file) {
        return state.export_running ? CloseStep::wait_for_export : CloseStep::proceed;
    }
    if (state.export_running) {
        return CloseStep::wait_for_export;
    }
    return state.dirty ? CloseStep::ask : CloseStep::proceed;
}

CloseStep prompt_choice(PromptChoice choice) noexcept {
    switch (choice) {
        case PromptChoice::export_first: return CloseStep::choose_file;
        case PromptChoice::save_recipe: return CloseStep::name_recipe;
        case PromptChoice::discard: return CloseStep::proceed;
        case PromptChoice::cancel: return CloseStep::cancel;
    }
    return CloseStep::cancel;
}

CloseStep save_dialog_closed(bool file_chosen) noexcept {
    return file_chosen ? CloseStep::await_export : CloseStep::cancel;
}

CloseStep export_finished(ExportEnd end) noexcept {
    return end == ExportEnd::saved ? CloseStep::proceed : CloseStep::cancel;
}

CloseStep recipe_sheet_closed(bool confirmed) noexcept {
    return confirmed ? CloseStep::choose_recipe_file : CloseStep::cancel;
}

CloseStep recipe_save_finished(RecipeEnd end) noexcept {
    return end == RecipeEnd::saved ? CloseStep::proceed : CloseStep::cancel;
}

PromptText prompt_text(Pending pending) noexcept {
    return {"You have edits that haven't been exported or saved as a recipe.", "Export…",
            "Save Recipe…",
            pending == Pending::quit ? "Quit Without Saving" : "Open Without Saving", "Cancel"};
}

std::string prompt_detail(const std::optional<LastSave>& last) {
    if (!last) {
        return {};
    }
    return last->kind == SaveKind::export_image ? "Last saved: exported as " + last->file
                                                : "Last saved: recipe " + last->file;
}

PromptButtons prompt_buttons(bool mac) noexcept {
    if (mac) {
        return {{PromptChoice::discard, PromptChoice::cancel, PromptChoice::save_recipe,
                 PromptChoice::export_first},
                1};
    }
    return {{PromptChoice::export_first, PromptChoice::save_recipe, PromptChoice::discard,
             PromptChoice::cancel},
            0};
}

std::string_view waiting_text(Pending pending) noexcept {
    return pending == Pending::quit ? "Finishing the export before quitting…"
                                    : "Finishing the export before opening the other photo…";
}

}  // namespace prism::gui
