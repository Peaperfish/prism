#include "prism/gui/scopes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

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

}  // namespace

int luma8(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    // The weights in 16-bit fixed point; they add up to 65536, so grey v gives exactly v.
    return static_cast<int>((13933u * r + 46870u * g + 4733u * b + 32768u) >> 16);
}

VectorscopeCell vectorscope_cell(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    const float y = kr * r + kg * g + kb * b;
    return {clamp_cell((static_cast<float>(b) - y) / cb_divisor),
            clamp_cell((static_cast<float>(r) - y) / cr_divisor)};
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
