#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

#include "prism/gui/histogram.hpp"
#include "prism/gui/histogram_paint.hpp"
#include "prism/gui/preferences.hpp"
#include "prism/gui/scopes.hpp"
#include "theme.hpp"

namespace prism::gui_app {

// The pictures of the Scopes section, as textures for scopes_panel.hpp to draw. Main thread only
// (SDL_Renderer).

// The colour scopes (RGB parade, luma waveform, vectorscope) of the frame on screen. The counts
// become a small RGBA texture (log-scaled, so faint detail shows) that is stretched over the plot.
class ScopeView {
public:
    explicit ScopeView(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~ScopeView() { reset_texture(); }

    ScopeView(const ScopeView&) = delete;
    ScopeView& operator=(const ScopeView&) = delete;

    // The scopes of the frame now on screen; nothing when it has none (or no photo is open).
    void set(std::optional<gui::Scopes> scopes);

    // The picture of `which` (not the histogram), or nullptr when there is no data yet.
    [[nodiscard]] SDL_Texture* texture(gui::ScopeChoice which, gui::Theme theme);

private:
    void rebuild(gui::ScopeChoice which, gui::Theme theme);
    void reset_texture();

    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int texture_width_ = 0;
    std::optional<gui::Scopes> scopes_;
    bool dirty_ = true;  // the texture does not show scopes_ / the choice / the theme yet
    gui::ScopeChoice built_for_ = gui::ScopeChoice::waveform;
    gui::Theme built_theme_ = gui::Theme::dark;
};

// The histogram's picture, one texture pixel to one screen pixel. It is painted and uploaded only
// when the histogram, the size or the colours change.
class HistogramTexture {
public:
    explicit HistogramTexture(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~HistogramTexture() { reset(); }

    HistogramTexture(const HistogramTexture&) = delete;
    HistogramTexture& operator=(const HistogramTexture&) = delete;

    // The picture for these, or nullptr if it can't be made.
    [[nodiscard]] SDL_Texture* update(const gui::StageHistogram& histogram,
                                      const gui::HistogramPaintSize& size,
                                      const gui::HistogramPaintColours& colours);

private:
    void reset();

    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    gui::HistogramPaintCache cache_;
};

}  // namespace prism::gui_app
