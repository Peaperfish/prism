#include "preview_texture.hpp"
#include "display.hpp"

#include <algorithm>

#include "prism/gui/brush.hpp"
#include "prism/gui/clipping.hpp"

namespace prism::gui_app {

namespace {

// An RGBA64_FLOAT texture in linear light for the HDR canvas (#106), made again only when its size
// changes; `texture` is destroyed (and the size zeroed) on any failure or for empty `rgba`.
bool upload_half_texture(SDL_Renderer* renderer, SDL_Texture*& texture, int& texture_width,
                         int& texture_height, std::span<const std::uint16_t> rgba, int width,
                         int height) {
    const auto drop = [&] {
        if (texture != nullptr) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        texture_width = 0;
        texture_height = 0;
    };
    if (rgba.empty()) {
        drop();
        return true;
    }
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        SDL_SetError("the HDR picture is the wrong size");
        drop();
        return false;
    }
    if (texture == nullptr || width != texture_width || height != texture_height) {
        drop();
        SDL_PropertiesID p = SDL_CreateProperties();
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                              SDL_PIXELFORMAT_RGBA64_FLOAT);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER,
                              SDL_COLORSPACE_SRGB_LINEAR);
        texture = SDL_CreateTextureWithProperties(renderer, p);
        SDL_DestroyProperties(p);
        if (texture == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        texture_width = width;
        texture_height = height;
    }
    if (!SDL_UpdateTexture(texture, nullptr, rgba.data(), width * 8)) {
        drop();
        return false;
    }
    return true;
}

}  // namespace

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
        texture_ = create_ui_texture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
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
        compare_ = create_ui_texture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
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
    overlay_pixels_ =
        gui::clip_overlay_pixels(clip_mask, width, height, over_abgr, under_abgr, gamut_abgr);
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
    texture = create_ui_texture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
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
    overlay_pixels_ = gui::stroke_overlay_pixels(stroke_mask, dodge_abgr, burn_abgr);
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

bool PreviewTexture::upload_hdr(std::span<const std::uint16_t> rgba, int width, int height) {
    return upload_half_texture(renderer_, hdr_, hdr_width_, hdr_height_, rgba, width, height);
}

bool PreviewTexture::upload_hdr_veiled(std::span<const std::uint16_t> rgba, int width, int height) {
    return upload_half_texture(renderer_, hdr_veiled_, hdr_veiled_width_, hdr_veiled_height_, rgba,
                               width, height);
}

void PreviewTexture::reset_hdr() {
    if (hdr_ != nullptr) {
        SDL_DestroyTexture(hdr_);
        hdr_ = nullptr;
    }
    hdr_width_ = 0;
    hdr_height_ = 0;
    if (hdr_veiled_ != nullptr) {
        SDL_DestroyTexture(hdr_veiled_);
        hdr_veiled_ = nullptr;
    }
    hdr_veiled_width_ = 0;
    hdr_veiled_height_ = 0;
}

void PreviewTexture::reset() {
    reset_overlay();
    reset_stroke_overlay();
    reset_compare();
    reset_hdr();
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

bool DetailTexture::upload(const gui::DetailFrame& frame) {
    const int w = frame.size.width;
    const int h = frame.size.height;
    if (w <= 0 || h <= 0 ||
        frame.rgb8.size() != static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3) {
        SDL_SetError("the detail frame is empty or the wrong size");
        reset();
        return false;
    }
    if (texture_ == nullptr || w != width_ || h != height_) {
        reset();
        texture_ =
            create_ui_texture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, w, h);
        if (texture_ == nullptr) {
            return false;
        }
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
        width_ = w;
        height_ = h;
    }
    if (!SDL_UpdateTexture(texture_, nullptr, frame.rgb8.data(), w * 3)) {
        reset();
        return false;
    }
    if (!upload_half_texture(renderer_, hdr_, hdr_width_, hdr_height_, frame.hdr_rgba, w, h)) {
        reset();
        return false;
    }
    return true;
}

void DetailTexture::reset() {
    if (hdr_ != nullptr) {
        SDL_DestroyTexture(hdr_);
        hdr_ = nullptr;
    }
    hdr_width_ = 0;
    hdr_height_ = 0;
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

}  // namespace prism::gui_app
