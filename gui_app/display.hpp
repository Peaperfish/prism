#pragma once

#include <SDL3/SDL.h>

#include <functional>

#include "prism/gui/hdr_display.hpp"

struct ImDrawData;

namespace prism::gui_app {

// The window's renderer in the mode gui::display_mode chooses (#106, gui/hdr_display.hpp):
// - DisplayMode::sdr: SDL_CreateRenderer(window, nullptr) and today's frame, unchanged;
// - DisplayMode::hdr_ui_target: a renderer whose output is SDL_COLORSPACE_SRGB_LINEAR. Each frame
//   the canvas is drawn first, straight into the window, then Dear ImGui into an offscreen
//   RGBA64_FLOAT target tagged SDL_COLORSPACE_SRGB, which is laid over it 1:1 with
//   SDL_SCALEMODE_PIXELART and SDL_BLENDMODE_BLEND_PREMULTIPLIED.
// Every recreation destroys ImGui's SDL_Renderer backend and the old renderer, resets the
// window's CAMetalLayer (macos_metal_layer.hpp) and starts the backend again on the new one.
class Display {
public:
    explicit Display(SDL_Window* window) : window_(window) {}
    ~Display() { destroy(); }
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    // (Re)creates the renderer for `mode`. False, with SDL_GetError() set, if it can't.
    bool create(gui::DisplayMode mode);
    void destroy();

    [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }
    [[nodiscard]] gui::DisplayMode mode() const noexcept { return mode_; }

    // What the window's screen can show now: SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, and its headroom
    // (SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1 on an SDR screen).
    [[nodiscard]] bool screen_shows_hdr() const;
    [[nodiscard]] float screen_headroom() const;

    // Draws one frame (without presenting it): the clear colour, then in UI-target mode
    // `draw_canvas` (in window pixels, render scale 1) and the UI target over it; in SDR mode the
    // draw data straight onto the window, as before. `scale` is ImGui's framebuffer scale.
    void draw(ImDrawData* draw_data, float scale_x, float scale_y, const float clear_rgb[3],
              const std::function<void(SDL_Renderer*)>& draw_canvas);
    void present() { SDL_RenderPresent(renderer_); }

private:
    bool make_ui_target(int width, int height);

    SDL_Window* window_;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* ui_target_ = nullptr;
    int ui_width_ = 0;
    int ui_height_ = 0;
    gui::DisplayMode mode_ = gui::DisplayMode::sdr;
    bool backend_ = false;
};

// An SDL_CreateTexture for textures that Dear ImGui draws (the preview, overlays, scopes): with a
// linear renderer they are tagged SDL_COLORSPACE_SRGB_LINEAR, so SDL draws their 8-bit values into
// the sRGB-tagged UI target unchanged (#97); with today's renderer exactly SDL_CreateTexture.
[[nodiscard]] SDL_Texture* create_ui_texture(SDL_Renderer* renderer, SDL_PixelFormat format,
                                             SDL_TextureAccess access, int width, int height);

}  // namespace prism::gui_app
