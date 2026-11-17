#include "prism/gui/hdr_display.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "prism/color.hpp"
#include "prism/gain_map.hpp"
#include "prism/parallel.hpp"

namespace prism::gui {

DisplayMode display_mode(bool screen_shows_hdr, bool macos, bool hdr_on) noexcept {
    return screen_shows_hdr && macos && hdr_on ? DisplayMode::hdr_ui_target : DisplayMode::sdr;
}

bool hdr_canvas_on(DisplayMode mode, const EditSettings& s, StageId viewed, bool test_strip,
                   float display_headroom, bool sdr_view) noexcept {
    return mode == DisplayMode::hdr_ui_target && output_headroom(s).has_value() &&
           viewed == final_stage && !test_strip && !sdr_view &&
           display_headroom > 1.0f;  // false for NaN
}

bool sdr_only_view(const CanvasViews& views) noexcept {
    return views.real_pixels || views.split || views.easel;
}

WindowPixels pixels_covering(float x0, float y0, float x1, float y1, float scale) noexcept {
    const int left = static_cast<int>(std::floor(x0 * scale));
    const int top = static_cast<int>(std::floor(y0 * scale));
    const int right = static_cast<int>(std::ceil(x1 * scale));
    const int bottom = static_cast<int>(std::ceil(y1 * scale));
    return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

float canvas_weight(float display_headroom, float headroom_stops) noexcept {
    GainMapMetadata m;
    m.hdr_capacity_min = 0.0f;
    m.hdr_capacity_max = headroom_stops;
    return gain_map_weight(display_headroom, m);
}

float request_headroom(float display_headroom, float headroom_stops) noexcept {
    const float weight =
        std::round(canvas_weight(display_headroom, headroom_stops) * 256.0f) / 256.0f;
    return std::exp2(weight * headroom_stops);
}

Image viewer_picture(std::span<const std::uint8_t> sdr_srgb8, const Image& hdr,
                     float headroom_stops, float display_headroom, int threads) {
    const GainMap map = make_gain_map(sdr_srgb8, hdr, headroom_stops, {}, threads);
    return apply_gain_map(sdr_srgb8, map, canvas_weight(display_headroom, headroom_stops), threads);
}

std::uint16_t half_from_float(float v) noexcept {
    const std::uint32_t f = std::bit_cast<std::uint32_t>(v);
    const std::uint32_t sign = (f >> 16) & 0x8000u;
    const std::uint32_t abs = f & 0x7FFFFFFFu;
    if (abs > 0x7F800000u) {
        return static_cast<std::uint16_t>(sign | 0x7E00u);  // NaN: quiet
    }
    if (abs >= 0x477FF000u) {
        // 65520 and up rounds past the largest half (65504): infinity.
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    }
    if (abs < 0x38800000u) {
        // Below the smallest normal half (2^-14): a subnormal, in units of 2^-24, rounded to
        // nearest even. Below half the smallest subnormal it rounds to 0.
        if (abs < 0x33000000u) {
            return static_cast<std::uint16_t>(sign);
        }
        const std::uint32_t exponent = abs >> 23;
        const std::uint32_t mantissa = (abs & 0x7FFFFFu) | 0x800000u;
        // value = mantissa * 2^(exponent - 150); in units of 2^-24 that is mantissa >> (126 -
        // exponent), a shift of 14 to 24 here.
        const std::uint32_t shift = 126u - exponent;
        const std::uint32_t whole = mantissa >> shift;
        const std::uint32_t rest = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1u);
        const std::uint32_t rounded =
            whole + ((rest > halfway || (rest == halfway && (whole & 1u) != 0u)) ? 1u : 0u);
        return static_cast<std::uint16_t>(sign | rounded);
    }
    // Normal: rebias the exponent (127 -> 15) and keep 10 of the 23 mantissa bits, rounding to
    // nearest even; a carry out of the mantissa correctly bumps the exponent.
    const std::uint32_t rebased = abs - 0x38000000u;  // (127 - 15) << 23
    const std::uint32_t whole = rebased >> 13;
    const std::uint32_t rest = rebased & 0x1FFFu;
    const std::uint32_t rounded =
        whole + ((rest > 0x1000u || (rest == 0x1000u && (whole & 1u) != 0u)) ? 1u : 0u);
    return static_cast<std::uint16_t>(sign | rounded);
}

float float_from_half(std::uint16_t h) noexcept {
    const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16;
    const std::uint32_t exponent = (h >> 10) & 0x1Fu;
    const std::uint32_t mantissa = h & 0x3FFu;
    if (exponent == 0) {
        // Zero or subnormal: mantissa * 2^-24, exact in a float.
        const float v = std::ldexp(static_cast<float>(mantissa), -24);
        return sign != 0 ? -v : v;
    }
    if (exponent == 0x1Fu) {
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
    }
    return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}

std::vector<std::uint16_t> rgba_half_from(const Image& img, int threads) {
    const std::size_t pixels = static_cast<std::size_t>(std::max(img.width, 0)) *
                               static_cast<std::size_t>(std::max(img.height, 0));
    std::vector<std::uint16_t> out(pixels * 4);
    if (pixels == 0 || img.pixels.size() != pixels * 3) {
        return pixels == 0 ? out : std::vector<std::uint16_t>{};
    }
    const std::uint16_t one = half_from_float(1.0f);
    parallel_for_rows(img.height, threads_for_pixels(pixels, threads), [&](int begin, int end) {
        const std::size_t width = static_cast<std::size_t>(img.width);
        for (std::size_t i = static_cast<std::size_t>(begin) * width;
             i < static_cast<std::size_t>(end) * width; ++i) {
            out[i * 4 + 0] = half_from_float(img.pixels[i * 3 + 0]);
            out[i * 4 + 1] = half_from_float(img.pixels[i * 3 + 1]);
            out[i * 4 + 2] = half_from_float(img.pixels[i * 3 + 2]);
            out[i * 4 + 3] = one;
        }
    });
    return out;
}

OverlayPlacement overlay_placement(CanvasOverlay overlay, bool hdr_canvas) noexcept {
    if (!hdr_canvas) {
        return OverlayPlacement::on_top;
    }
    switch (overlay) {
        case CanvasOverlay::easel_shade:
        case CanvasOverlay::clip_overlay:
        case CanvasOverlay::stroke_overlay:
        case CanvasOverlay::split_line: return OverlayPlacement::into_picture;
        case CanvasOverlay::tooltip: return OverlayPlacement::opaque_on_top;
        case CanvasOverlay::frame_lines:
        case CanvasOverlay::sample_pin: return OverlayPlacement::on_top;
    }
    return OverlayPlacement::on_top;
}

void into_picture(const float picture[3], const float sdr[3], const LinearRgba& overlay,
                  float out[3]) noexcept {
    const float a = std::isnan(overlay.a) ? 0.0f : std::clamp(overlay.a, 0.0f, 1.0f);
    if (a == 0.0f) {
        out[0] = picture[0];
        out[1] = picture[1];
        out[2] = picture[2];
        return;
    }
    const float colour[3] = {overlay.r, overlay.g, overlay.b};
    for (int c = 0; c < 3; ++c) {
        out[c] = srgb_to_linear(linear_to_srgb(colour[c]) * a +
                                linear_to_srgb(std::clamp(sdr[c], 0.0f, 1.0f)) * (1.0f - a));
    }
}

bool overlay_drawn_into(const OverlayMask& mask, int width, int height) noexcept {
    return mask.shown && width > 0 && height > 0 && mask.width == width && mask.height == height &&
           mask.bytes == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

LinearRgba linear_rgba_from_abgr(std::uint32_t abgr) noexcept {
    return {srgb8_to_linear(static_cast<std::uint8_t>(abgr & 0xffu)),
            srgb8_to_linear(static_cast<std::uint8_t>((abgr >> 8) & 0xffu)),
            srgb8_to_linear(static_cast<std::uint8_t>((abgr >> 16) & 0xffu)),
            static_cast<float>((abgr >> 24) & 0xffu) / 255.0f};
}

std::vector<std::uint16_t> hdr_with_overlays(std::span<const std::uint16_t> hdr_rgba,
                                             std::span<const std::uint8_t> sdr_rgb8, int width,
                                             int height,
                                             std::span<const std::span<const std::uint32_t>> layers,
                                             int threads) {
    const std::size_t across = static_cast<std::size_t>(std::max(width, 0));
    const std::size_t pixels = across * static_cast<std::size_t>(std::max(height, 0));
    if (pixels == 0 || hdr_rgba.size() != pixels * 4 || sdr_rgb8.size() != pixels * 3) {
        return {};
    }
    std::vector<std::span<const std::uint32_t>> used;
    for (const std::span<const std::uint32_t> layer : layers) {
        if (layer.size() == pixels) {
            used.push_back(layer);
        }
    }
    std::vector<std::uint16_t> out(hdr_rgba.begin(), hdr_rgba.end());
    if (used.empty()) {
        return out;
    }
    parallel_for_rows(height, threads_for_pixels(pixels, threads), [&](int begin, int end) {
        for (std::size_t i = static_cast<std::size_t>(begin) * across;
             i < static_cast<std::size_t>(end) * across; ++i) {
            bool covered = false;
            float picture[3];
            float sdr[3];
            for (const std::span<const std::uint32_t> layer : used) {
                if ((layer[i] >> 24) == 0) {
                    continue;
                }
                if (!covered) {
                    for (int c = 0; c < 3; ++c) {
                        picture[c] = float_from_half(hdr_rgba[i * 4 + c]);
                        sdr[c] = srgb8_to_linear(sdr_rgb8[i * 3 + c]);
                    }
                    covered = true;
                }
                float blended[3];
                into_picture(picture, sdr, linear_rgba_from_abgr(layer[i]), blended);
                for (int c = 0; c < 3; ++c) {
                    picture[c] = blended[c];
                    sdr[c] = blended[c];
                }
            }
            if (covered) {
                for (int c = 0; c < 3; ++c) {
                    out[i * 4 + c] = half_from_float(picture[c]);
                }
            }
        }
    });
    return out;
}

void ui_over_canvas(const float canvas[3], const std::uint8_t ui_premultiplied[4],
                    float out[3]) noexcept {
    const std::uint8_t alpha = ui_premultiplied[3];
    const float a = static_cast<float>(alpha) / 255.0f;
    for (int c = 0; c < 3; ++c) {
        float ui = 0.0f;
        if (alpha != 0) {
            const float straight =
                std::min(1.0f, static_cast<float>(ui_premultiplied[c]) / static_cast<float>(alpha));
            ui = srgb_to_linear(straight) * a;
        }
        out[c] = alpha == 255 ? ui : ui + canvas[c] * (1.0f - a);
    }
}

}  // namespace prism::gui
