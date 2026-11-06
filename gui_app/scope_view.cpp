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
// The count's log decides the opacity (a lone sample is still visible), and `tint(level, col)` the
// colour; the plot shows through.
template <typename Tint>
void paint_grid(std::vector<std::uint32_t>& pixels, int row_pixels, int x0,
                const gui::Scopes::Grid& grid, std::uint64_t max_count, Tint tint) {
    const double norm = max_count > 0 ? 1.0 / std::log1p(static_cast<double>(max_count)) : 0.0;
    for (int level = 0; level < N; ++level) {
        const int row = N - 1 - level;
        for (int col = 0; col < N; ++col) {
            const std::uint32_t count = grid[static_cast<std::size_t>(level) * N + col];
            if (count == 0) {
                pixels[static_cast<std::size_t>(row) * row_pixels + x0 + col] = 0;
                continue;
            }
            const double t = std::log1p(static_cast<double>(count)) * norm;
            const auto alpha = static_cast<std::uint32_t>(std::lround(48.0 + 207.0 * t));
            const Rgb c = tint(level, col);
            pixels[static_cast<std::size_t>(row) * row_pixels + x0 + col] =
                IM_COL32(c.r, c.g, c.b, alpha);
        }
    }
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
        // One scale for the three, so a channel that holds more shows more.
        const std::uint64_t m =
            std::max({grid_max(s.parade_red), grid_max(s.parade_green), grid_max(s.parade_blue)});
        const auto flat = [](ImU32 colour) {
            return [c = unpack(colour)](int, int) { return c; };
        };
        paint_grid(pixels, width, 0, s.parade_red, m, flat(colours.histogram_red));
        paint_grid(pixels, width, N, s.parade_green, m, flat(colours.histogram_green));
        paint_grid(pixels, width, 2 * N, s.parade_blue, m, flat(colours.histogram_blue));
    } else if (which == gui::ScopeChoice::waveform) {
        paint_grid(pixels, width, 0, s.waveform, grid_max(s.waveform),
                   [c = unpack(colours.scope_trace)](int, int) { return c; });
    } else {
        // Each dot in its own hue. A cell's row is its Cr and its column its Cb.
        paint_grid(pixels, width, 0, s.vectorscope, grid_max(s.vectorscope), [](int cr, int cb) {
            const gui::ScopeRgb c = gui::vectorscope_cell_colour(cb, cr);
            return Rgb{c.r, c.g, c.b};
        });
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

SDL_Texture* ScopeView::texture(gui::ScopeChoice which, gui::Theme theme) {
    if (dirty_ || which != built_for_ || theme != built_theme_) {
        rebuild(which, theme);
    }
    return scopes_ ? texture_ : nullptr;
}

void HistogramTexture::reset() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

SDL_Texture* HistogramTexture::update(const gui::StageHistogram& histogram,
                                      const gui::HistogramPaintSize& size,
                                      const gui::HistogramPaintColours& colours) {
    if (size.width <= 0 || size.height <= 0) {
        return nullptr;
    }
    const bool repainted = cache_.update(histogram, size, colours);
    if (!repainted && texture_ != nullptr) {
        return texture_;
    }
    if (texture_ == nullptr || width_ != size.width || height_ != size.height) {
        reset();
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     size.width, size.height);
        if (texture_ == nullptr) {
            return nullptr;
        }
        width_ = size.width;
        height_ = size.height;
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);  // the rounded corners' soft edge
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    }
    if (!SDL_UpdateTexture(texture_, nullptr, cache_.pixels().data(), size.width * 4)) {
        reset();
    }
    return texture_;
}

}  // namespace prism::gui_app
