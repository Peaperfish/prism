#include "preview_texture.hpp"

namespace prism::gui_app {

bool PreviewTexture::upload(const gui::PreviewFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.rgb8.size() != static_cast<std::size_t>(frame.width) * frame.height * 3) {
        SDL_SetError("the preview frame is empty or the wrong size");
        reset();
        return false;
    }
    if (texture_ == nullptr || frame.width != width_ || frame.height != height_) {
        reset();
        // Static: the pixels change only when a new frame arrives, a few times a second at most.
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                                     frame.width, frame.height);
        if (texture_ == nullptr) {
            return false;
        }
        // The preview is usually drawn smaller than it is: filter instead of skipping pixels.
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
        width_ = frame.width;
        height_ = frame.height;
    }
    if (!SDL_UpdateTexture(texture_, nullptr, frame.rgb8.data(), frame.width * 3)) {
        reset();
        return false;
    }
    return true;
}

void PreviewTexture::reset() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

}  // namespace prism::gui_app
