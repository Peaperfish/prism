#include "scope_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "imgui.h"

namespace prism::gui_app {

namespace {

constexpr int N = gui::Scopes::size;

struct Rgb {
    std::uint8_t r, g, b;
};

Rgb unpack(ImU32 col) {
    return {static_cast<std::uint8_t>((col >> IM_COL32_R_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((col >> IM_COL32_G_SHIFT) & 0xFF),
            static_cast<std::uint8_t>((col >> IM_COL32_B_SHIFT) & 0xFF)};
}

std::uint64_t grid_max(const gui::Scopes::Grid& g) {
    return g.empty() ? 0 : *std::max_element(g.begin(), g.end());
}

// Writes one grid into `pixels` (RGBA, `row_pixels` wide) at column `x0`, top row = highest level.
// The count's log decides the opacity (a lone sample is still visible), the colour is `tint`; the
// panel's background shows through, so one tint works on a light or a dark panel.
void paint_grid(std::vector<std::uint32_t>& pixels, int row_pixels, int x0,
                const gui::Scopes::Grid& grid, std::uint64_t max_count, Rgb tint) {
    const double norm = max_count > 0 ? 1.0 / std::log1p(static_cast<double>(max_count)) : 0.0;
    for (int level = 0; level < N; ++level) {
        const int row = N - 1 - level;
        for (int col = 0; col < N; ++col) {
            const std::uint32_t count = grid[static_cast<std::size_t>(level) * N + col];
            std::uint32_t alpha = 0;
            if (count > 0) {
                const double t = std::log1p(static_cast<double>(count)) * norm;
                alpha = static_cast<std::uint32_t>(std::lround(48.0 + 207.0 * t));
            }
            pixels[static_cast<std::size_t>(row) * row_pixels + x0 + col] =
                IM_COL32(tint.r, tint.g, tint.b, alpha);
        }
    }
}

ImTextureRef texture_ref(SDL_Texture* t) {
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(t)));
}

}  // namespace

void ScopeView::set(std::optional<gui::Scopes> scopes) {
    scopes_ = std::move(scopes);
    dirty_ = true;
}

void ScopeView::reset_texture() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    texture_width_ = 0;
}

void ScopeView::rebuild(gui::ScopeChoice which, gui::Theme theme) {
    dirty_ = false;
    built_for_ = which;
    built_theme_ = theme;
    if (!scopes_) {
        return;
    }
    const Palette& colours = palette(theme);
    const int width = which == gui::ScopeChoice::parade ? 3 * N : N;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * N, 0u);
    const gui::Scopes& s = *scopes_;
    if (which == gui::ScopeChoice::parade) {
        // One brightness scale for the three, so a channel that holds more shows more.
        const std::uint64_t m =
            std::max({grid_max(s.parade_red), grid_max(s.parade_green), grid_max(s.parade_blue)});
        paint_grid(pixels, width, 0, s.parade_red, m, unpack(colours.histogram_red));
        paint_grid(pixels, width, N, s.parade_green, m, unpack(colours.histogram_green));
        paint_grid(pixels, width, 2 * N, s.parade_blue, m, unpack(colours.histogram_blue));
    } else if (which == gui::ScopeChoice::waveform) {
        paint_grid(pixels, width, 0, s.waveform, grid_max(s.waveform), unpack(colours.scope_trace));
    } else {
        paint_grid(pixels, width, 0, s.vectorscope, grid_max(s.vectorscope),
                   unpack(colours.scope_trace));
    }

    if (texture_ == nullptr || texture_width_ != width) {
        reset_texture();
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     width, N);
        if (texture_ == nullptr) {
            return;  // draws as an empty panel
        }
        texture_width_ = width;
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    }
    if (!SDL_UpdateTexture(texture_, nullptr, pixels.data(),
                           width * static_cast<int>(sizeof(std::uint32_t)))) {
        reset_texture();
    }
}

void ScopeView::draw(gui::ScopeChoice which, gui::Theme theme, float avail_w, float s) {
    if (dirty_ || which != built_for_ || theme != built_theme_) {
        rebuild(which, theme);
    }
    const Palette& colours = palette(theme);
    const bool vector = which == gui::ScopeChoice::vectorscope;
    const float height = std::round((vector ? vectorscope_max_side : scope_graph_height) * s);

    ImGui::InvisibleButton("##scope_graph", ImVec2(avail_w, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box_min = ImGui::GetItemRectMin();
    const ImVec2 box_max = ImGui::GetItemRectMax();
    const float rounding = 4.0f * s;
    draw->AddRectFilled(box_min, box_max, colours.histogram_background, rounding);
    draw->PushClipRect(box_min, box_max, true);

    const float pad = 4.0f * s;
    const float line = std::max(1.0f, std::round(s));
    const bool have = texture_ != nullptr && scopes_.has_value();

    if (!vector) {
        const ImVec2 plot_min(box_min.x + pad, box_min.y + pad);
        const ImVec2 plot_max(box_max.x - pad, box_max.y - pad);
        const int panels = which == gui::ScopeChoice::parade ? 3 : 1;
        const float gap = panels > 1 ? 4.0f * s : 0.0f;
        const float panel_w = (plot_max.x - plot_min.x - gap * (panels - 1)) / panels;
        for (int p = 0; p < panels; ++p) {
            const float x0 = plot_min.x + p * (panel_w + gap);
            const float x1 = x0 + panel_w;
            if (have) {
                draw->AddImage(texture_ref(texture_), ImVec2(x0, plot_min.y), ImVec2(x1, plot_max.y),
                               ImVec2(static_cast<float>(p) / panels, 0.0f),
                               ImVec2(static_cast<float>(p + 1) / panels, 1.0f));
            }
            for (const float level : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                const float y = std::round(plot_max.y - level * (plot_max.y - plot_min.y));
                draw->AddLine(ImVec2(x0, y), ImVec2(x1, y), colours.scope_guide, line);
            }
        }
    } else {
        const float side = std::min(box_max.x - box_min.x, box_max.y - box_min.y) - 2 * pad;
        const ImVec2 origin((box_min.x + box_max.x) * 0.5f - side * 0.5f,
                            (box_min.y + box_max.y) * 0.5f - side * 0.5f);
        if (have) {
            draw->AddImage(texture_ref(texture_), origin, ImVec2(origin.x + side, origin.y + side));
        }
        // Cell (cb, cr) is drawn centred at ((cb + 0.5) / 256, (255 - cr + 0.5) / 256) of the
        // square, so the neutral axis is cell 128's centre.
        const auto position = [&](gui::VectorscopeCell c) {
            return ImVec2(origin.x + (static_cast<float>(c.cb) + 0.5f) / N * side,
                          origin.y + (static_cast<float>(N - 1 - c.cr) + 0.5f) / N * side);
        };
        const ImVec2 centre = position({128, 128});
        const float radius = side * 0.5f;
        draw->AddCircle(centre, radius, colours.scope_guide, 64, line);
        draw->AddCircle(centre, radius * 0.5f, colours.scope_guide, 48, line);
        draw->AddLine(ImVec2(centre.x - radius, centre.y), ImVec2(centre.x + radius, centre.y),
                      colours.scope_guide, line);
        draw->AddLine(ImVec2(centre.x, centre.y - radius), ImVec2(centre.x, centre.y + radius),
                      colours.scope_guide, line);

        // Skin tones: the I-line, 123 degrees counter-clockwise from the +Cb axis.
        constexpr float pi = 3.14159265f;
        const float angle = 123.0f * pi / 180.0f;
        const ImVec2 skin_end(centre.x + std::cos(angle) * radius,
                              centre.y - std::sin(angle) * radius);
        draw->AddLine(centre, skin_end, colours.scope_skin, 1.5f * s);

        // Targets at 75% saturation (inside the ring, like colour bars): R, Mg, B, Cy, G, Yl.
        struct Target {
            const char* name;
            std::uint8_t r, g, b;
            ImU32 colour;
        };
        const std::array<Target, 6> targets{{{"R", 191, 0, 0, colours.histogram_red},
                                             {"Mg", 191, 0, 191, colours.scope_magenta},
                                             {"B", 0, 0, 191, colours.histogram_blue},
                                             {"Cy", 0, 191, 191, colours.scope_cyan},
                                             {"G", 0, 191, 0, colours.histogram_green},
                                             {"Yl", 191, 191, 0, colours.scope_yellow}}};
        const float half = 4.0f * s;
        const float label_size = ImGui::GetFontSize() * 0.72f;
        for (const Target& t : targets) {
            const ImVec2 p = position(gui::vectorscope_cell(t.r, t.g, t.b));
            draw->AddRect(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), t.colour,
                          0.0f, 0, 1.5f * s);
            // The label goes on the outer side of the target, away from the centre.
            const ImVec2 size = ImGui::CalcTextSize(t.name);
            const float k = label_size / ImGui::GetFontSize();
            const ImVec2 out(p.x - centre.x, p.y - centre.y);
            const float len = std::max(1.0f, std::sqrt(out.x * out.x + out.y * out.y));
            const ImVec2 at(p.x + out.x / len * (half + 3.0f * s) - (out.x < 0 ? size.x * k : 0.0f),
                            p.y + out.y / len * (half + 3.0f * s) - size.y * k * 0.5f);
            draw->AddText(nullptr, label_size, at, t.colour, t.name);
        }

        if (ImGui::IsItemHovered()) {
            const ImVec2 m = ImGui::GetMousePos();
            const ImVec2 d(skin_end.x - centre.x, skin_end.y - centre.y);
            const float len2 = d.x * d.x + d.y * d.y;
            const float t = std::clamp(((m.x - centre.x) * d.x + (m.y - centre.y) * d.y) / len2,
                                       0.0f, 1.0f);
            const float dx = m.x - (centre.x + d.x * t);
            const float dy = m.y - (centre.y + d.y * t);
            if (dx * dx + dy * dy <= (6.0f * s) * (6.0f * s)) {
                ImGui::SetTooltip("Skin tones usually sit near this line.");
            }
        }
    }
    draw->PopClipRect();
    draw->AddRect(box_min, box_max, colours.histogram_border, rounding, 0, 1.0f * s);
}

}  // namespace prism::gui_app
