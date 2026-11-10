#include "preview_texture.hpp"

#include <algorithm>

#include "prism/gui/brush.hpp"

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

bool PreviewTexture::upload_compare(const gui::CompareFrame& compare) {
    if (compare.width <= 0 || compare.height <= 0 ||
        compare.rgb8.size() != static_cast<std::size_t>(compare.width) * compare.height * 3) {
        SDL_SetError("the compare picture is empty or the wrong size");
        reset_compare();
        return false;
    }
    if (compare_ == nullptr || compare.width != compare_width_ ||
        compare.height != compare_height_) {
        reset_compare();
        compare_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                                     compare.width, compare.height);
        if (compare_ == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(compare_, SDL_SCALEMODE_LINEAR);
        compare_width_ = compare.width;
        compare_height_ = compare.height;
    }
    compare_stage_ = compare.stage;
    if (!SDL_UpdateTexture(compare_, nullptr, compare.rgb8.data(), compare.width * 3)) {
        reset_compare();
        return false;
    }
    return true;
}

void PreviewTexture::reset_compare() {
    if (compare_ != nullptr) {
        SDL_DestroyTexture(compare_);
        compare_ = nullptr;
    }
    compare_width_ = 0;
    compare_height_ = 0;
}

bool PreviewTexture::upload_overlay(const std::vector<std::uint8_t>& clip_mask, int width,
                                    int height, std::uint32_t over_abgr, std::uint32_t under_abgr,
                                    std::uint32_t gamut_abgr) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(width, 0)) *
                               static_cast<std::size_t>(std::max(height, 0));
    if (pixels == 0 || clip_mask.size() != pixels) {
        reset_overlay();
        return false;
    }
    if (!make_overlay_texture(overlay_, overlay_width_, overlay_height_, width, height)) {
        return false;
    }
    overlay_pixels_.resize(pixels);
    const std::size_t across = static_cast<std::size_t>(width);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t m = clip_mask[i];
        // Gamut stripes run diagonally: on where (x + y) / 4 is even.
        const bool stripe = (((i % across) + (i / across)) / 4) % 2 == 0;
        overlay_pixels_[i] = (m & gui::clip_mask_high)    ? over_abgr
                             : (m & gui::clip_mask_gamut) ? (stripe ? gamut_abgr : 0u)
                             : (m & gui::clip_mask_low)   ? under_abgr
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

bool PreviewTexture::make_overlay_texture(SDL_Texture*& texture, int& texture_width,
                                          int& texture_height, int width, int height) {
    if (texture != nullptr && width == texture_width && height == texture_height) {
        return true;
    }
    if (texture != nullptr) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                width, height);
    if (texture == nullptr) {
        return false;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    texture_width = width;
    texture_height = height;
    return true;
}

bool PreviewTexture::upload_stroke_overlay(const std::vector<std::uint8_t>& stroke_mask, int width,
                                           int height, std::uint32_t dodge_abgr,
                                           std::uint32_t burn_abgr) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(width, 0)) *
                               static_cast<std::size_t>(std::max(height, 0));
    if (pixels == 0 || stroke_mask.size() != pixels) {
        reset_stroke_overlay();
        return false;
    }
    if (!make_overlay_texture(stroke_overlay_, stroke_overlay_width_, stroke_overlay_height_,
                              width, height)) {
        return false;
    }
    overlay_pixels_.resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        overlay_pixels_[i] = gui::stroke_mask_abgr(stroke_mask[i], dodge_abgr, burn_abgr);
    }
    if (!SDL_UpdateTexture(stroke_overlay_, nullptr, overlay_pixels_.data(), width * 4)) {
        reset_stroke_overlay();
        return false;
    }
    return true;
}

void PreviewTexture::reset_stroke_overlay() {
    if (stroke_overlay_ != nullptr) {
        SDL_DestroyTexture(stroke_overlay_);
        stroke_overlay_ = nullptr;
    }
}

void PreviewTexture::reset() {
    reset_overlay();
    reset_stroke_overlay();
    reset_compare();
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

}  // namespace prism::gui_app
