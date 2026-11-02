#include "preview_texture.hpp"

#include <algorithm>

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

bool PreviewTexture::upload_overlay(const std::vector<std::uint8_t>& clip_mask, int width,
                                    int height, std::uint32_t over_abgr, std::uint32_t under_abgr) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(width, 0)) *
                               static_cast<std::size_t>(std::max(height, 0));
    if (pixels == 0 || clip_mask.size() != pixels) {
        reset_overlay();
        return false;
    }
    if (overlay_ == nullptr || width != overlay_width_ || height != overlay_height_) {
        reset_overlay();
        overlay_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                     width, height);
        if (overlay_ == nullptr) {
            return false;
        }
        SDL_SetTextureBlendMode(overlay_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(overlay_, SDL_SCALEMODE_LINEAR);
        overlay_width_ = width;
        overlay_height_ = height;
    }
    overlay_pixels_.resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t m = clip_mask[i];
        overlay_pixels_[i] = (m & gui::clip_mask_high)  ? over_abgr
                             : (m & gui::clip_mask_low) ? under_abgr
                                                        : 0u;
    }
    if (!SDL_UpdateTexture(overlay_, nullptr, overlay_pixels_.data(), width * 4)) {
        reset_overlay();
        return false;
    }
    return true;
}

void PreviewTexture::reset_overlay() {
    if (overlay_ != nullptr) {
        SDL_DestroyTexture(overlay_);
        overlay_ = nullptr;
    }
}

void PreviewTexture::reset() {
    reset_overlay();
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

}  // namespace prism::gui_app
