#pragma once

#include <SDL3/SDL.h>

#include "prism/gui/preview_renderer.hpp"

namespace prism::gui_app {

// The preview on the GPU: owns one SDL_Texture holding the latest PreviewFrame. SDL_Renderer may
// only be used on the main thread, so upload() must run there (the worker only produces bytes).
// The texture is recreated only when the frame size changes; otherwise its pixels are replaced.
class PreviewTexture {
public:
    explicit PreviewTexture(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~PreviewTexture() { reset(); }

    PreviewTexture(const PreviewTexture&) = delete;
    PreviewTexture& operator=(const PreviewTexture&) = delete;

    // Copies the frame's RGB8 bytes into the texture. False (with SDL_GetError() set) if SDL
    // couldn't; the previous image is gone then.
    bool upload(const gui::PreviewFrame& frame);

    // Drops the texture: nothing to show.
    void reset();

    [[nodiscard]] SDL_Texture* texture() const noexcept { return texture_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace prism::gui_app
