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

PromptText prompt_text(Pending pending) noexcept {
    return {"You have edits that haven't been exported.", "Export...",
            pending == Pending::quit ? "Quit Without Exporting" : "Open Without Exporting",
            "Cancel"};
}

std::string_view waiting_text(Pending pending) noexcept {
    return pending == Pending::quit ? "Finishing the export before quitting..."
                                    : "Finishing the export before opening the other photo...";
}

}  // namespace prism::gui
