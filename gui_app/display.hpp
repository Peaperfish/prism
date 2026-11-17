#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
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
    // The UI target couldn't be made: the frame was drawn without it (the canvas left out), and
    // the app should go back to today's renderer. Stays true for this Display.
    [[nodiscard]] bool ui_target_failed() const noexcept { return ui_target_failed_; }

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
    bool ui_target_failed_ = false;
};

// The HDR canvas's pictures drawn under the UI (#106, Display::draw's `draw_canvas`), in window
// pixels at render scale 1. App::draw_under_ui draws with these.
//
// `picture` stretched over `dst`, clipped to `clip`, sampled nearest or linearly.
void draw_under_ui_picture(SDL_Renderer* renderer, SDL_Texture* picture, const SDL_FRect& dst,
                           const gui::WindowPixels& clip, bool nearest);
// The easel (#58): the canvas's colour over `canvas` (sRGB values 0 to 1: SDL takes draw colours as
// sRGB and converts them for the linear output itself, so it matches ImGui's grey), then `veiled`
// (the photo with the veil drawn into it) as the turned photo `quad`, then `plain` (the photo
// without it) as the same quad again, clipped to `frame` (inside the canvas): the veil only outside
// the frame.
void draw_under_ui_easel(SDL_Renderer* renderer, SDL_Texture* veiled, SDL_Texture* plain,
                         const std::array<gui::QuadVertex, 6>& quad,
                         const gui::WindowPixels& canvas, const gui::WindowPixels& frame,
                         const float canvas_srgb[3], bool nearest);

// An SDL_CreateTexture for textures that Dear ImGui draws (the preview, overlays, scopes): with a
// linear renderer they are tagged SDL_COLORSPACE_SRGB_LINEAR, so SDL draws their 8-bit values into
// the sRGB-tagged UI target unchanged (#97); with today's renderer exactly SDL_CreateTexture.
// In UI-target mode an RGB24 request gets an RGBA32 texture (gui::ui_texture_as_rgba): fill it
// with update_ui_texture_rgb8.
[[nodiscard]] SDL_Texture* create_ui_texture(SDL_Renderer* renderer, SDL_PixelFormat format,
                                             SDL_TextureAccess access, int width, int height);

// SDL_UpdateTexture of a whole texture from 8-bit RGB bytes (width * height * 3), widened to RGBA
// first when create_ui_texture made the texture RGBA32. False (with SDL_GetError() set) if SDL
// couldn't.
bool update_ui_texture_rgb8(SDL_Texture* texture, const std::uint8_t* rgb8, int width, int height);

}  // namespace prism::gui_app
