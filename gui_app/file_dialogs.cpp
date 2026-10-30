#include "file_dialogs.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "prism/gui/file_types.hpp"
#include "prism/utf8_path.hpp"

namespace prism::gui_app {

namespace {

// Everything one dialog needs until its callback runs: SDL requires the filters to stay valid
// until then. Allocated per dialog and deleted by the callback.
struct Request {
    DialogQueue* queue = nullptr;
    DialogResult::Kind kind = DialogResult::Kind::open;
    std::vector<std::string> patterns;
    std::vector<SDL_DialogFileFilter> filters;
    std::string default_location;
};

void SDLCALL on_dialog_done(void* userdata, const char* const* filelist, int filter) {
    std::unique_ptr<Request> request(static_cast<Request*>(userdata));
    DialogResult result;
    result.kind = request->kind;
    if (filelist == nullptr) {
        result.error = SDL_GetError();
    } else if (filelist[0] != nullptr) {
        // SDL hands over UTF-8; copy it now, since filelist is freed when this returns.
        result.path = path_from_utf8(filelist[0]);
    }
    (void)filter;
    request->queue->push(std::move(result));

    // Wake the main loop, which may be asleep in SDL_WaitEventTimeout. Thread-safe.
    SDL_Event wake{};
    wake.type = SDL_EVENT_USER;
    SDL_PushEvent(&wake);
}

void finish_filters(Request& request, std::initializer_list<const char*> names) {
    auto name = names.begin();
    for (const std::string& pattern : request.patterns) {
        request.filters.push_back({*name++, pattern.c_str()});
    }
}

}  // namespace

void show_open_dialog(SDL_Window* window, DialogQueue& queue,
                      const std::filesystem::path& start_folder) {
    auto request = std::make_unique<Request>();
    request->queue = &queue;
    request->kind = DialogResult::Kind::open;
    request->patterns = {gui::open_filter_pattern()};
    finish_filters(*request, {"Photos"});
    request->default_location = start_folder.empty() ? "" : utf8(start_folder);

    Request* r = request.release();  // the callback owns it now
    SDL_ShowOpenFileDialog(on_dialog_done, r, window, r->filters.data(),
                           static_cast<int>(r->filters.size()),
                           r->default_location.empty() ? nullptr : r->default_location.c_str(),
                           false);
}

}  // namespace prism::gui_app
