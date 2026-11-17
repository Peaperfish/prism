#include "display.hpp"

#include "imgui.h"
#include "imgui_impl_sdlrenderer3.h"
#ifdef __APPLE__
#include "macos_metal_layer.hpp"
#endif

namespace prism::gui_app {

bool Display::create(gui::DisplayMode mode) {
    destroy();
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window_);
    if (mode == gui::DisplayMode::hdr_ui_target) {
        SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER,
                              SDL_COLORSPACE_SRGB_LINEAR);
    }
    renderer_ = mode == gui::DisplayMode::sdr ? SDL_CreateRenderer(window_, nullptr)
                                              : SDL_CreateRendererWithProperties(p);
    SDL_DestroyProperties(p);
    if (renderer_ == nullptr) {
        return false;
    }
    mode_ = mode;
    SDL_SetRenderVSync(renderer_, 1);
    backend_ = ImGui_ImplSDLRenderer3_Init(renderer_);
    if (!backend_) {
        SDL_SetError("Dear ImGui's renderer backend didn't start");
        return false;
    }
    return true;
}

void Display::destroy() {
    if (ui_target_ != nullptr) {
        SDL_DestroyTexture(ui_target_);
        ui_target_ = nullptr;
        ui_width_ = 0;
        ui_height_ = 0;
    }
    if (backend_) {
        ImGui_ImplSDLRenderer3_Shutdown();  // its textures go; ImGui makes them again next frame
        backend_ = false;
    }
    if (renderer_ != nullptr) {
        void* layer = SDL_GetRenderMetalLayer(renderer_);
        SDL_DestroyRenderer(renderer_);  // the window's Metal view and its layer outlive it
        renderer_ = nullptr;
#ifdef __APPLE__
        reset_metal_layer(layer);
#else
        (void)layer;
#endif
    }
}

bool Display::screen_shows_hdr() const {
    return SDL_GetBooleanProperty(SDL_GetWindowProperties(window_),
                                  SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
}

float Display::screen_headroom() const {
    return SDL_GetFloatProperty(SDL_GetWindowProperties(window_),
                                SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
}

bool Display::make_ui_target(int width, int height) {
    if (ui_target_ != nullptr) {
        SDL_DestroyTexture(ui_target_);
        ui_target_ = nullptr;
    }
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA64_FLOAT);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    ui_target_ = SDL_CreateTextureWithProperties(renderer_, p);
    SDL_DestroyProperties(p);
    if (ui_target_ == nullptr) {
        return false;
    }
    SDL_SetTextureBlendMode(ui_target_, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureScaleMode(ui_target_, SDL_SCALEMODE_PIXELART);
    ui_width_ = width;
    ui_height_ = height;
    return true;
}

void Display::draw(ImDrawData* draw_data, float scale_x, float scale_y, const float clear_rgb[3],
                   const std::function<void(SDL_Renderer*)>& draw_canvas) {
    if (mode_ == gui::DisplayMode::sdr) {
        // Exactly the frame of before #106.
        SDL_SetRenderScale(renderer_, scale_x, scale_y);
        SDL_SetRenderDrawColorFloat(renderer_, clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer_);
        return;
    }
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
    SDL_SetRenderDrawColorFloat(renderer_, clear_rgb[0], clear_rgb[1], clear_rgb[2], 1.0f);
    SDL_RenderClear(renderer_);
    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);
    if ((width != ui_width_ || height != ui_height_ || ui_target_ == nullptr) &&
        !make_ui_target(width, height)) {
        // Without the target the UI is drawn straight on, with nothing under it (the canvas's
        // hole would only show the clear colour), and the app goes back to today's renderer.
        ui_target_failed_ = true;
        SDL_SetRenderScale(renderer_, scale_x, scale_y);
        ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer_);
        return;
    }
    if (draw_canvas) {
        draw_canvas(renderer_);
        SDL_SetRenderClipRect(renderer_, nullptr);
        SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
    }
    SDL_SetRenderTarget(renderer_, ui_target_);
    SDL_SetRenderScale(renderer_, scale_x, scale_y);
    SDL_SetRenderDrawColorFloat(renderer_, 0.0f, 0.0f, 0.0f, 0.0f);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, renderer_);
    SDL_SetRenderTarget(renderer_, nullptr);
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
    const SDL_FRect whole{0.0f, 0.0f, static_cast<float>(ui_width_),
                          static_cast<float>(ui_height_)};
    SDL_RenderTexture(renderer_, ui_target_, nullptr, &whole);
}

SDL_Texture* create_ui_texture(SDL_Renderer* renderer, SDL_PixelFormat format,
                               SDL_TextureAccess access, int width, int height) {
    const Sint64 output =
        SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),
                              SDL_PROP_RENDERER_OUTPUT_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB);
    if (output != SDL_COLORSPACE_SRGB_LINEAR) {
        return SDL_CreateTexture(renderer, format, access, width, height);
    }
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, format);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, access);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, SDL_COLORSPACE_SRGB_LINEAR);
    SDL_Texture* texture = SDL_CreateTextureWithProperties(renderer, p);
    SDL_DestroyProperties(p);
    return texture;
}

}  // namespace prism::gui_app
