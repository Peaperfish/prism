#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "imgui.h"
#include "prism/gui/histogram.hpp"
#include "prism/gui/histogram_paint.hpp"
#include "prism/gui/preferences.hpp"

namespace prism::gui_app {

// The Scopes section: a "Scopes" header with the Show clipping action, the row of four scopes, one
// plot box and the readout line under it. It is drawn from plain data and answers through callbacks,
// and holds no state of its own, so the tests can draw it without the app or a renderer: the plot's
// picture comes in as a texture id, made by the app (scope_view.hpp).
//
// Every part sits at a place worked out from the type and metric tokens alone, and nothing that is
// shown has a say in it, so the section is one height whichever scope is chosen, whatever the
// readout says, with or without data, under the mouse or not (#38).

struct ScopesData {
    gui::ScopeChoice scope = gui::ScopeChoice::histogram;
    gui::Theme theme = gui::Theme::dark;
    // Whose scopes these are, when that isn't simply the finished photo: "Exposure", "+1 stop".
    std::string detail;
    bool clip_overlay_on = false;
    // The histogram of the picture on screen; nothing until the first frame arrives.
    const gui::StageHistogram* histogram = nullptr;
    // The chosen scope's picture. Histogram: the whole plot box (scopes_histogram_size). The others:
    // their counts, as ScopeView makes them. 0: nothing to show yet, and the box is drawn empty.
    ImTextureID plot_texture = 0;
    // Screen pixels per ImGui unit, to line the histogram's edges up with its picture.
    float pixel_density = 1.0f;
    // The clipping summary sentence, for the readout's tooltip. Empty until a clip report arrives.
    std::string clip_sentence;
};

struct ScopesCallbacks {
    std::function<void(gui::ScopeChoice)> choose_scope;
    std::function<void()> toggle_clip_overlay;
    // As in light_path.hpp: right after a control is submitted, it being ImGui's last item.
    std::function<void(std::string_view id)> explain;
    std::function<void(std::string_view id)> describe;
    // The mouse is over something that isn't a control of its own but explains itself.
    std::function<void(std::string_view id)> hover;
};

// Where the parts were drawn, in ImGui's screen units.
struct ScopesRects {
    ImVec2 action_min, action_max;  // Show clipping, in the header
    ImVec2 tabs_min, tabs_max;
    ImVec2 plot_min, plot_max;
    ImVec2 readout_min, readout_max;
};

// The plot box's height, the same for all four scopes.
[[nodiscard]] float scopes_plot_height();

// The picture the histogram needs for a plot box `width` wide, for the app to paint.
[[nodiscard]] gui::HistogramPaintSize scopes_histogram_size(float width, float pixel_density);

// Draws the section at the cursor, across the available width, and leaves the cursor under it.
ScopesRects draw_scopes(const ScopesData& data, const ScopesCallbacks& callbacks);

}  // namespace prism::gui_app
