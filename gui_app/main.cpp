// Prism's desktop app: sets up SDL, the renderer and Dear ImGui, then runs the main loop. What the
// window shows and does is in App (app.hpp).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // on Windows: provides WinMain and hands main() UTF-8 arguments

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

#include "app.hpp"
#include "prism/utf8_path.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

int main(int argc, char** argv) {
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
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "Prism: couldn't create a renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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

    // ImGui's scalable font (ProggyForever) rather than its 13 px bitmap font, which would be
    // blurry when scaled up (interface size above 100%, or a display at 150% on Windows). ImGui
    // rasterises it at the size and pixel density in use, so text is crisp on a Retina screen
    // too, and stays so when the window moves between displays.
    io.Fonts->AddFontDefaultVector();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Declared before the App and outliving it: a file dialog still open when the window closes
    // may answer into it late.
    prism::gui_app::DialogQueue dialogs;
    {
        prism::gui_app::App app(window, renderer, dialogs, preferences_file);
        // "Open with" on Windows, or prism_gui photo.jpg: SDL's argv is UTF-8 on every OS.
        if (argc > 1) {
            app.open_utf8(argv[1]);
        }

        bool done = false;
        while (!done) {
            // Sleep until something happens while nothing is moving, instead of redrawing the
            // same frame 60 times a second.
            if (!app.animating()) {
                SDL_WaitEventTimeout(nullptr, 100);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                     event.window.windowID == SDL_GetWindowID(window))) {
                    done = true;
                }
                app.handle_event(event);
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(10);
                continue;
            }

            app.before_frame();
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();

            SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x,
                               io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColorFloat(renderer, 0.1f, 0.1f, 0.1f, 1.0f);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
    }  // the App (and its textures) goes before the renderer

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
