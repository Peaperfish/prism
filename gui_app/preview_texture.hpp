#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "prism/gui/detail_renderer.hpp"
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

    // The clipping overlay: a second texture, same size as the picture, with `over` where
    // clip_mask has the high bit and `under` where it has the low bit, and `gamut` where it has the
    // gamut bit, as diagonal stripes about 4 pixels wide (so the colour underneath shows between
    // them). With several bits set the order is high, then gamut, then low. Transparent elsewhere.
    // False if the mask does not match width x height (then there is none).
    bool upload_overlay(const std::vector<std::uint8_t>& clip_mask, int width, int height,
                        std::uint32_t over_abgr, std::uint32_t under_abgr,
                        std::uint32_t gamut_abgr);
    void reset_overlay();
    [[nodiscard]] SDL_Texture* overlay() const noexcept { return overlay_; }

    // The stroke mask's overlay: a third texture of the same kind, drawn the same way, from
    // PreviewFrame::stroke_mask (gui::stroke_mask_abgr with the two colours). False if the mask
    // does not match width x height (then there is none).
    bool upload_stroke_overlay(const std::vector<std::uint8_t>& stroke_mask, int width, int height,
                               std::uint32_t dodge_abgr, std::uint32_t burn_abgr);
    void reset_stroke_overlay();
    [[nodiscard]] SDL_Texture* stroke_overlay() const noexcept { return stroke_overlay_; }

    // The compare picture (the left half of a split): a second texture of the same kind, from a
    // frame's `compare`. Same behaviour as upload(): false (with SDL_GetError() set) if SDL
    // couldn't, and the compare picture is gone then.
    bool upload_compare(const gui::CompareFrame& compare);
    void reset_compare();
    [[nodiscard]] SDL_Texture* compare_texture() const noexcept { return compare_; }
    [[nodiscard]] int compare_width() const noexcept { return compare_width_; }
    [[nodiscard]] int compare_height() const noexcept { return compare_height_; }
    // The stage the compare picture shows.
    [[nodiscard]] gui::StageId compare_stage() const noexcept { return compare_stage_; }

    // Drops the textures: nothing to show.
    void reset();

    [[nodiscard]] SDL_Texture* texture() const noexcept { return texture_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

private:
    // Makes `texture` a width x height ABGR texture blended over the picture, as the clipping
    // overlay is, if it isn't one of that size already. False if SDL couldn't.
    bool make_overlay_texture(SDL_Texture*& texture, int& texture_width, int& texture_height,
                              int width, int height);

    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    SDL_Texture* overlay_ = nullptr;
    SDL_Texture* stroke_overlay_ = nullptr;
    int stroke_overlay_width_ = 0;
    int stroke_overlay_height_ = 0;
    SDL_Texture* compare_ = nullptr;
    gui::StageId compare_stage_ = gui::StageId::decode;
    int compare_width_ = 0;
    int compare_height_ = 0;
    std::vector<std::uint32_t> overlay_pixels_;
    int overlay_width_ = 0;
    int overlay_height_ = 0;
    int width_ = 0;
    int height_ = 0;
};

// A frame of real pixels on the GPU (#37): one SDL_Texture holding the DetailFrame last taken from
// the DetailRenderer, drawn over the enlarged preview. Main thread only, as PreviewTexture. The
// texture is reused while frames keep the same size; only its pixels are replaced.
class DetailTexture {
public:
    explicit DetailTexture(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~DetailTexture() { reset(); }

    DetailTexture(const DetailTexture&) = delete;
    DetailTexture& operator=(const DetailTexture&) = delete;

    // Copies the frame's RGB8 bytes into the texture. False (with SDL_GetError() set) if the frame
    // is empty or the wrong size, or SDL couldn't; there is no texture then.
    bool upload(const gui::DetailFrame& frame);
    // Drops the texture.
    void reset();

    [[nodiscard]] SDL_Texture* texture() const noexcept { return texture_; }

private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace prism::gui_app
