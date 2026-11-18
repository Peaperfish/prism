// Prism's desktop app: sets up SDL, the renderer and Dear ImGui, then runs the main loop. What the
// window shows and does is in App (app.hpp).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // on Windows: provides WinMain and hands main() UTF-8 arguments

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "app.hpp"
#include "display.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "prism/gui/file_types.hpp"
#include "prism/gui/hdr_display.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/wheel_scaling.hpp"
#include "prism/utf8_path.hpp"
#include "theme.hpp"

int main(int argc, char** argv) {
    // Before SDL, which can change the C locale the recipe parser depends on (presets.hpp).
    try {
        prism::gui::load_builtin_looks();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Prism: couldn't read the built-in looks: %s\n", e.what());
        return 1;
    }

    // Keep the trackpad's momentum scrolling after the fingers lift (off by default in SDL).
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "Prism: couldn't start SDL: %s\n", SDL_GetError());
        return 1;
    }

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    // 1280 x 800 at the display's scale, but never bigger than 90% of the space it has.
    int width = static_cast<int>(1280 * scale);
    int height = static_cast<int>(800 * scale);
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
        width = std::min(width, usable.w * 9 / 10);
        height = std::min(height, usable.h * 9 / 10);
    }
    SDL_Window* window = SDL_CreateWindow("Prism", width, height, flags);
    if (window == nullptr) {
        std::fprintf(stderr, "Prism: couldn't create the window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // The renderer: today's, or UI-target mode while the window's screen can show HDR and the
    // photo's HDR is on (#106, display.hpp). It also starts Dear ImGui's renderer backend. No photo
    // is open yet, so it starts as today's.
    const auto wanted_mode = [](const prism::gui_app::Display& d, bool hdr_on) {
#if defined(__APPLE__)
        const bool macos = true;
#else
        const bool macos = false;
#endif
        return d.ui_target_failed() ? prism::gui::DisplayMode::sdr
                                    : prism::gui::display_mode(d.screen_shows_hdr(), macos, hdr_on);
    };
    prism::gui_app::Display display(window);
    if (!display.create(prism::gui::DisplayMode::sdr)) {
        std::fprintf(stderr, "Prism: couldn't create a renderer: %s\n", SDL_GetError());
        ImGui::DestroyContext();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // ImGui would write imgui.ini into the working directory, which may be read-only (e.g. inside
    // a disk image): keep it in the per-user folder SDL picks for the app instead.
    std::string ini_path;
    std::filesystem::path preferences_file;
    if (char* pref = SDL_GetPrefPath("Prism", "Prism")) {
        ini_path = std::string(pref) + "imgui.ini";
        // SDL gives the folder as UTF-8: a user name in CJK must survive on Windows too.
        preferences_file = prism::path_from_utf8(pref) / "preferences.txt";
        SDL_free(pref);
        io.IniFilename = ini_path.c_str();
    } else {
        io.IniFilename = nullptr;
    }

    // Inter, compiled into the executable. ImGui rasterises the vector font at the size and pixel
    // density in use, so text is crisp when scaled up (interface size above 100%, or a display at
    // 150% on Windows), on a Retina screen, and when the window moves between displays.
    prism::gui_app::load_fonts(io);

    ImGui_ImplSDL3_InitForSDLRenderer(window, display.renderer());

    // Declared before the App and outliving it: a file dialog still open when the window closes
    // may answer into it late.
    prism::gui_app::DialogQueue dialogs;
    {
        prism::gui_app::App app(window, display.renderer(), dialogs, preferences_file);
        // "Open with" on Windows, or prism_gui photo.jpg: SDL's argv is UTF-8 on every OS.
        // macOS adds its own flags, which photo_from_arguments skips.
        if (const auto photo =
                prism::gui::photo_from_arguments(std::vector<std::string>(argv + 1, argv + argc))) {
            app.open_utf8(photo->c_str());
        }

        bool done = false;
        bool hdr_create_failed = false;  // creating UI-target mode failed (next_display_mode)
        prism::gui::WheelScaler wheel_scaler;
        while (!done) {
            // Sleep until something happens while nothing is moving, instead of redrawing the
            // same frame 60 times a second.
            if (!app.animating()) {
                SDL_WaitEventTimeout(nullptr, 100);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    // Before ImGui sees it: a trackpad's deltas are scaled to follow the fingers.
                    const prism::gui::WheelDelta raw{event.wheel.x, event.wheel.y};
                    const std::uint64_t now_ms = event.wheel.timestamp / 1'000'000;
                    const prism::gui::WheelDelta wheel =
                        wheel_scaler.scale(raw, ImGui::GetFontSize(), now_ms);
                    // The canvas gets SDL's own numbers: it pans in screen units, not ImGui's steps.
                    app.canvas_wheel(raw, wheel_scaler.last_was_trackpad(), now_ms);
                    event.wheel.x = wheel.x;
                    event.wheel.y = wheel.y;
                }
                ImGui_ImplSDL3_ProcessEvent(&event);
                // Quitting may first ask about unexported edits: the App says when it is time.
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(window))) {
                    app.request_quit();
                }
                app.handle_event(event);
            }
            if (app.quit_wanted()) {
                done = true;
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(10);
                continue;
            }

            // The renderer follows the screen (moved to another one, its HDR changed) and the
            // photo's HDR switch (#96 decision 10, #106): recreated when the mode they ask for
            // changes, with the app's textures dropped before and made again after. Once UI-target
            // mode couldn't be created, today's renderer stays (next_display_mode).
            if (const prism::gui::DisplayMode mode = prism::gui::next_display_mode(
                    display.mode(), wanted_mode(display, app.hdr_on()), hdr_create_failed);
                mode != display.mode()) {
                app.release_textures();
                if (!display.create(mode)) {
                    if (mode == prism::gui::DisplayMode::hdr_ui_target) {
                        hdr_create_failed = true;
                    }
                    if (!display.create(prism::gui::DisplayMode::sdr)) {
                        std::fprintf(stderr, "Prism: couldn't create a renderer: %s\n",
                                     SDL_GetError());
                        break;
                    }
                }
                app.renderer_changed(display.renderer());
            }
            app.set_display(display.mode(), display.screen_headroom());
            app.before_frame();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();

            const ImVec4 clear = app.clear_colour();
            const float clear_rgb[3] = {clear.x, clear.y, clear.z};
            display.draw(ImGui::GetDrawData(), io.DisplayFramebufferScale.x,
                         io.DisplayFramebufferScale.y, clear_rgb,
                         [&](SDL_Renderer* r) { app.draw_under_ui(r); });
            display.present();
        }
    }  // the App (and its textures) goes before the renderer

    display.destroy();  // ImGui's renderer backend, then the renderer
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
