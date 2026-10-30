#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <optional>
#include <string>

#include "prism/gui/main_thread_queue.hpp"

namespace prism::gui_app {

// The answer of an Open or Save dialog, as it arrives on the main thread.
struct DialogResult {
    enum class Kind { open, save };
    Kind kind = Kind::open;
    std::optional<std::filesystem::path> path;  // nothing: cancelled, or the dialog failed
    std::string error;                          // why it failed (SDL's message), if it did
};

using DialogQueue = gui::MainThreadQueue<DialogResult>;

// Shows the system's Open dialog and returns at once. SDL's dialogs are asynchronous and may
// answer on another thread, so the answer is pushed to `queue` (with an SDL event to wake the
// main loop) for the main thread to take. `queue` must outlive the dialog.
void show_open_dialog(SDL_Window* window, DialogQueue& queue,
                      const std::filesystem::path& start_folder);

}  // namespace prism::gui_app
