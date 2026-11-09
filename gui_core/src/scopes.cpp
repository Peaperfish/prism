#include "prism/gui/scopes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "prism/color.hpp"

namespace prism::gui {

namespace {

// Rec. 709 luma weights, and the divisors that scale B-Y and R-Y to +-0.5 (1 - 0.0722 and
// 1 - 0.2126, times 2).
constexpr float kr = 0.2126f;
constexpr float kg = 0.7152f;
constexpr float kb = 0.0722f;
constexpr float cb_divisor = 1.8556f;
constexpr float cr_divisor = 1.5748f;

int clamp_cell(float v) noexcept {
    return std::clamp(static_cast<int>(std::lround(v)) + 128, 0, Scopes::size - 1);
}

// Cb and Cr of a colour in cells from the neutral axis, before scaling.
VectorscopePoint unscaled_point(float r, float g, float b) noexcept {
    const float y = kr * r + kg * g + kb * b;
    return {(b - y) / cb_divisor, (r - y) / cr_divisor};
}

}  // namespace

int luma8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    // The weights in 16-bit fixed point; they add up to 65536, so grey v gives exactly v.
    return static_cast<int>((13933u * r + 46870u * g + 4733u * b + 32768u) >> 16);
}

float vectorscope_farthest_cells() noexcept {
    // The six corners of the cube that are neither black nor white.
    static constexpr float corners[6][3] = {{255, 0, 0},   {255, 255, 0}, {0, 255, 0},
                                            {0, 255, 255}, {0, 0, 255},   {255, 0, 255}};
    float farthest = 0.0f;
    for (const auto& c : corners) {
        const VectorscopePoint p = unscaled_point(c[0], c[1], c[2]);
        farthest = std::max(farthest, std::sqrt(p.cb * p.cb + p.cr * p.cr));
    }
    return farthest;
}

float vectorscope_scale() noexcept {
    static const float scale = static_cast<float>(vectorscope_ring_cells) / vectorscope_farthest_cells();
    return scale;
}

VectorscopePoint vectorscope_point(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    const VectorscopePoint p = unscaled_point(r, g, b);
    return {p.cb * vectorscope_scale(), p.cr * vectorscope_scale()};
}

VectorscopeCell vectorscope_cell(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    const VectorscopePoint p = vectorscope_point(r, g, b);
    return {clamp_cell(p.cb), clamp_cell(p.cr)};
}

ScopeRgb vectorscope_cell_colour(int cb, int cr) noexcept {
    // Back from the cell to how far R, G and B stand from the luma (see vectorscope_cell).
    const float db = static_cast<float>(cb - 128) / vectorscope_scale() * cb_divisor;
    const float dr = static_cast<float>(cr - 128) / vectorscope_scale() * cr_divisor;
    const float dg = -(kr * dr + kb * db) / kg;
    const float high = std::max({dr, dg, db});
    const float low = std::min({dr, dg, db});
    const float span = high - low;
    // Near the axis the hue can't be told, so it fades into the grey over the first stretch.
    constexpr float grey = 205.0f;
    constexpr float full_hue_span = 72.0f;
    const float strength = std::min(span / full_hue_span, 1.0f);
    const auto channel = [&](float d) {
        const float pure = span > 0.0f ? (d - low) / span * 255.0f : grey;
        return static_cast<std::uint8_t>(std::lround(grey + (pure - grey) * strength));
    };
    return {channel(dr), channel(dg), channel(db)};
}

std::span<const ScopeGuide> scope_guides() {
    static const std::array<ScopeGuide, 5> guides{{
        {linear_to_srgb8(1.0f), "white"},
        {linear_to_srgb8(0.5f), "50%"},
        {linear_to_srgb8(0.18f), "18%"},
        {linear_to_srgb8(0.05f), "5%"},
        {linear_to_srgb8(0.0f), "black"},
    }};
    return guides;
}

Scopes compute_scopes(const std::vector<std::uint8_t>& rgb8, int width, int height) {
    Scopes s;
    const std::size_t cells = static_cast<std::size_t>(Scopes::size) * Scopes::size;
    for (Scopes::Grid* g : {&s.parade_red, &s.parade_green, &s.parade_blue, &s.waveform,
                            &s.vectorscope}) {
        g->assign(cells, 0u);
    }
    if (width <= 0 || height <= 0 ||
        rgb8.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3) {
        return s;
    }

    const double pixels = static_cast<double>(width) * height;
    s.stride = std::max(1, static_cast<int>(std::ceil(
                               std::sqrt(pixels / static_cast<double>(Scopes::max_samples)))));
    const int first = s.stride / 2;  // centre the sample grid in its cells

    for (int y = first; y < height; y += s.stride) {
        const std::uint8_t* row = rgb8.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = first; x < width; x += s.stride) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 3;
            const std::size_t column = static_cast<std::size_t>(x) * Scopes::size / width;
            ++s.parade_red[p[0] * static_cast<std::size_t>(Scopes::size) + column];
            ++s.parade_green[p[1] * static_cast<std::size_t>(Scopes::size) + column];
            ++s.parade_blue[p[2] * static_cast<std::size_t>(Scopes::size) + column];
            ++s.waveform[static_cast<std::size_t>(luma8(p[0], p[1], p[2])) * Scopes::size +
                         column];
            const VectorscopeCell c = vectorscope_cell(p[0], p[1], p[2]);
            ++s.vectorscope[static_cast<std::size_t>(c.cr) * Scopes::size + c.cb];
            ++s.sample_count;
        }
    }
    return s;
}

}  // namespace prism::gui
