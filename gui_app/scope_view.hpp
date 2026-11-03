#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

#include "prism/gui/preferences.hpp"
#include "prism/gui/scopes.hpp"
#include "theme.hpp"

namespace prism::gui_app {

// Draws the colour scopes (RGB parade, luma waveform, vectorscope) of the frame on screen. The
// counts become a small RGBA texture (log-scaled, so faint detail shows) that is stretched over
// the panel; the graticule is drawn on top with ImDrawList. Main thread only (SDL_Renderer).
class ScopeView {
public:
    explicit ScopeView(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~ScopeView() { reset_texture(); }

    ScopeView(const ScopeView&) = delete;
    ScopeView& operator=(const ScopeView&) = delete;

    // The scopes of the frame now on screen; nothing when it has none (or no photo is open).
    void set(std::optional<gui::Scopes> scopes);

    [[nodiscard]] bool has_data() const noexcept { return scopes_.has_value(); }

    // True if the last draw() had the mouse on the vectorscope's skin-tone line.
    [[nodiscard]] bool skin_line_hovered() const noexcept { return skin_hovered_; }

    // Draws `which` (not the histogram) into the next `avail_w` x scope_height(...) area of the
    // current ImGui window. Draws the empty panel when there is no data yet.
    void draw(gui::ScopeChoice which, gui::Theme theme, float avail_w, float scale);

private:
    void rebuild(gui::ScopeChoice which, gui::Theme theme);
    void reset_texture();

    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    int texture_width_ = 0;
    std::optional<gui::Scopes> scopes_;
    bool skin_hovered_ = false;
    bool dirty_ = true;  // the texture does not show scopes_ / the choice / the theme yet
    gui::ScopeChoice built_for_ = gui::ScopeChoice::waveform;
    gui::Theme built_theme_ = gui::Theme::dark;
};

// Heights of the scope panels in points (before the UI scale).
inline constexpr float scope_graph_height = 112.0f;
inline constexpr float vectorscope_max_side = 168.0f;

}  // namespace prism::gui_app
