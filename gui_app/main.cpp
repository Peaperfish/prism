// Prism's desktop app: sets up SDL, the renderer and Dear ImGui, then runs the main loop. What the
// window shows and does is in App (app.hpp).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>  // on Windows: provides WinMain and hands main() UTF-8 arguments

#include <cstdio>
#include <string>

#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

namespace {

// ImGui's sizes are in pixels: scale the style to the display (e.g. 2 on a Retina screen, 1.5 at
// 150% on Windows). The style can't be rescaled in place, so it is rebuilt from scratch each time.
void apply_scale(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "Prism: couldn't start SDL: %s\n", SDL_GetError());
        return 1;
    }

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags flags =
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("Prism", static_cast<int>(1280 * scale),
                                          static_cast<int>(800 * scale), flags);
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
    if (char* pref = SDL_GetPrefPath("Prism", "Prism")) {
        ini_path = std::string(pref) + "imgui.ini";
        SDL_free(pref);
        io.IniFilename = ini_path.c_str();
    } else {
        io.IniFilename = nullptr;
    }
    apply_scale(scale);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    {
        prism::gui_app::App app;

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
                } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                    apply_scale(SDL_GetWindowDisplayScale(window));
                }
                app.handle_event(event);
            }
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(10);
                continue;
            }

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
