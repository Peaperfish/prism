// The HDR canvas's decisions (#106): when it is on, the viewer weight, the viewer picture, half
// floats for the texture, where overlays go, and the compositing maths.

#include "prism/gui/hdr_display.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "prism/color.hpp"
#include "prism/gain_map.hpp"
#include "prism/gui/brush.hpp"
#include "prism/gui/clipping.hpp"
#include "prism/gui/preview_renderer.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

using prism::gui::CanvasOverlay;
using prism::gui::DisplayMode;
using prism::gui::EditSettings;
using prism::gui::OverlayPlacement;
using prism::gui::StageId;

namespace {

EditSettings hdr_on(float headroom = 2.0f) {
    EditSettings s;
    s.hdr_on = true;
    s.headroom_stops = headroom;
    return s;
}

// A width x height picture pair: an SDR picture (as 8-bit sRGB) and an HDR picture that is the
// SDR picture lifted towards 2^N in its bright part, varied in every pixel.
struct Pair {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> sdr;
    prism::Image hdr;
};

Pair pair(int width, int height, float n) {
    Pair p;
    p.width = width;
    p.height = height;
    p.sdr.resize(static_cast<std::size_t>(width) * height * 3);
    p.hdr.width = width;
    p.hdr.height = height;
    p.hdr.pixels.resize(p.sdr.size());
    const float h = std::exp2(n);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
            for (int c = 0; c < 3; ++c) {
                const std::uint8_t code = static_cast<std::uint8_t>((x * 7 + y * 3 + c * 50) % 256);
                p.sdr[i + c] = code;
                const float lin = prism::srgb8_to_linear(code);
                p.hdr.pixels[i + c] =
                    lin > 0.5f ? lin * (1.0f + (h - 1.0f) * (lin - 0.5f) * 2.0f) : lin;
            }
        }
    }
    return p;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out(img.pixels.size());
    std::memcpy(out.data(), img.pixels.data(), out.size() * sizeof(float));
    return out;
}

}  // namespace

TEST(HdrDisplay, UiTargetModeOnlyOnMacOsWithAnHdrScreen) {
    EXPECT_EQ(prism::gui::display_mode(true, true, true), DisplayMode::hdr_ui_target);
    // HDR off keeps today's renderer, whatever the screen.
    EXPECT_EQ(prism::gui::display_mode(true, true, false), DisplayMode::sdr);
    EXPECT_EQ(prism::gui::display_mode(false, true, true), DisplayMode::sdr);
    EXPECT_EQ(prism::gui::display_mode(true, false, true), DisplayMode::sdr);
    EXPECT_EQ(prism::gui::display_mode(false, false, false), DisplayMode::sdr);
}

TEST(HdrDisplay, TheCanvasIsHdrOnlyForTheFinalPictureWithHdrOnAndRoomAboveWhite) {
    const auto on = [](DisplayMode m, const EditSettings& s, StageId v, bool strip, float d) {
        return prism::gui::hdr_canvas_on(m, s, v, strip, d);
    };
    const DisplayMode hdr = DisplayMode::hdr_ui_target;
    EXPECT_TRUE(on(hdr, hdr_on(), StageId::encode, false, 4.0f));
    EXPECT_TRUE(on(hdr, hdr_on(0.1f), StageId::encode, false, 1.01f));
    EXPECT_FALSE(on(DisplayMode::sdr, hdr_on(), StageId::encode, false, 4.0f));
    EXPECT_FALSE(on(hdr, EditSettings{}, StageId::encode, false, 4.0f));  // HDR off
    EXPECT_FALSE(on(hdr, hdr_on(0.0f), StageId::encode, false, 4.0f));    // on at 0
    EXPECT_FALSE(on(hdr, hdr_on(), StageId::curve, false, 4.0f));
    EXPECT_FALSE(on(hdr, hdr_on(), StageId::decode, false, 4.0f));
    EXPECT_FALSE(on(hdr, hdr_on(), StageId::encode, true, 4.0f));   // a test strip
    EXPECT_FALSE(on(hdr, hdr_on(), StageId::encode, false, 1.0f));  // no room right now
    EXPECT_FALSE(
        on(hdr, hdr_on(), StageId::encode, false, std::numeric_limits<float>::quiet_NaN()));
    // The views this version shows in SDR (the easel, a split, an overlay, real pixels).
    EXPECT_FALSE(prism::gui::hdr_canvas_on(hdr, hdr_on(), StageId::encode, false, 4.0f, true));
    EXPECT_TRUE(prism::gui::hdr_canvas_on(hdr, hdr_on(), StageId::encode, false, 4.0f, false));
}

TEST(HdrDisplay, TheCanvasWeightIsTheViewersFromTheFilesMetadata) {
    using prism::gui::canvas_weight;
    EXPECT_FLOAT_EQ(canvas_weight(1.0f, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(canvas_weight(2.0f, 2.0f), 0.5f);
    EXPECT_FLOAT_EQ(canvas_weight(4.0f, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(canvas_weight(16.0f, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(canvas_weight(8.0f, 4.0f), 0.75f);  // what ImageIO shows of N = 4 (#105)
    EXPECT_FLOAT_EQ(canvas_weight(1.2f, 1.0f), std::log2(1.2f));
    EXPECT_FLOAT_EQ(canvas_weight(0.0f, 2.0f), 0.0f);
    EXPECT_FLOAT_EQ(canvas_weight(std::numeric_limits<float>::quiet_NaN(), 2.0f), 0.0f);
}

TEST(HdrDisplay, TheViewerPictureIsTheBaseAtWeightZeroAndTheGainMapsPictureAbove) {
    const Pair p = pair(37, 23, 2.0f);
    // An SDR screen right now: the decoded base exactly.
    const prism::Image at_one = prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, 1.0f, 1);
    ASSERT_EQ(at_one.pixels.size(), p.sdr.size());
    for (std::size_t i = 0; i < p.sdr.size(); ++i) {
        ASSERT_EQ(at_one.pixels[i], prism::srgb8_to_linear(p.sdr[i])) << i;
    }
    // Any other screen: the gain map of the base and the HDR picture at the viewer's weight.
    const prism::GainMap map = prism::make_gain_map(p.sdr, p.hdr, 2.0f, {}, 1);
    for (const float d : {1.5f, 2.0f, 4.0f, 16.0f}) {
        const prism::Image got = prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, d, 1);
        EXPECT_EQ(bits(got),
                  bits(prism::apply_gain_map(p.sdr, map, prism::gui::canvas_weight(d, 2.0f), 1)))
            << d;
    }
    // At full weight it reaches above white, and no channel passes 2^N (the map's limit).
    const prism::Image full = prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, 4.0f, 1);
    float peak = 0.0f;
    for (const float v : full.pixels) {
        peak = std::max(peak, v);
    }
    EXPECT_GT(peak, 2.0f);
    EXPECT_LE(peak, 4.0f * 1.0001f);
}

TEST(HdrDisplay, ARenderAsksForTheWeightRoundedSoSmallHeadroomChangesRenderNothing) {
    using prism::gui::request_headroom;
    EXPECT_EQ(request_headroom(1.0f, 2.0f), 1.0f);
    EXPECT_EQ(request_headroom(4.0f, 2.0f), 4.0f);
    EXPECT_EQ(request_headroom(16.0f, 2.0f), 4.0f);  // clamped: the same request as 4
    EXPECT_EQ(request_headroom(5.3f, 2.0f), request_headroom(9.0f, 2.0f));
    EXPECT_FLOAT_EQ(request_headroom(2.0f, 2.0f), 2.0f);  // weight 0.5 exactly
    // A wobble of the screen's headroom smaller than a 1/256 step of weight asks for the same.
    EXPECT_EQ(request_headroom(2.0f, 2.0f), request_headroom(2.0005f, 2.0f));
    EXPECT_NE(request_headroom(2.0f, 2.0f), request_headroom(2.1f, 2.0f));
    // The weight of what is asked for is the screen's own, within half a step.
    for (const float d : {1.1f, 1.7f, 2.9f, 3.3f}) {
        EXPECT_NEAR(prism::gui::canvas_weight(request_headroom(d, 2.0f), 2.0f),
                    prism::gui::canvas_weight(d, 2.0f), 0.5f / 256.0f + 1e-6f)
            << d;
    }
}

TEST(HdrDisplay, HalfFloatsRoundToNearestEven) {
    using prism::gui::half_from_float;
    EXPECT_EQ(half_from_float(0.0f), 0x0000u);
    EXPECT_EQ(half_from_float(-0.0f), 0x8000u);
    EXPECT_EQ(half_from_float(1.0f), 0x3C00u);
    EXPECT_EQ(half_from_float(-2.0f), 0xC000u);
    EXPECT_EQ(half_from_float(0.5f), 0x3800u);
    EXPECT_EQ(half_from_float(16.0f), 0x4C00u);
    EXPECT_EQ(half_from_float(65504.0f), 0x7BFFu);
    EXPECT_EQ(half_from_float(65519.0f), 0x7BFFu);
    EXPECT_EQ(half_from_float(65520.0f), 0x7C00u);  // the tie rounds up to infinity
    EXPECT_EQ(half_from_float(std::numeric_limits<float>::infinity()), 0x7C00u);
    EXPECT_EQ(half_from_float(std::numeric_limits<float>::quiet_NaN()), 0x7E00u);
    EXPECT_EQ(half_from_float(std::ldexp(1.0f, -14)), 0x0400u);  // the smallest normal
    EXPECT_EQ(half_from_float(std::ldexp(1.0f, -24)), 0x0001u);  // the smallest subnormal
    EXPECT_EQ(half_from_float(std::ldexp(1.0f, -25)), 0x0000u);  // half of it: ties to even, 0
    EXPECT_EQ(half_from_float(std::ldexp(3.0f, -26)), 0x0001u);  // 0.75 of it: up
    EXPECT_EQ(half_from_float(std::ldexp(3.0f, -25)), 0x0002u);  // 1.5 of it: ties to even, 2
    // 1 + 2^-11 is halfway between 1 and the next half: to even (1); 1 + 3 * 2^-11 goes up.
    EXPECT_EQ(half_from_float(1.0f + std::ldexp(1.0f, -11)), 0x3C00u);
    EXPECT_EQ(half_from_float(1.0f + std::ldexp(3.0f, -11)), 0x3C02u);
    EXPECT_EQ(half_from_float(1.0f + std::ldexp(1.0f, -10)), 0x3C01u);
    // Every half that is a number comes back exactly.
    for (std::uint32_t h = 0; h < 0x10000u; ++h) {
        if (((h >> 10) & 0x1Fu) == 0x1Fu && (h & 0x3FFu) != 0) {
            continue;  // NaN
        }
        ASSERT_EQ(half_from_float(prism::gui::float_from_half(static_cast<std::uint16_t>(h))), h)
            << std::hex << h;
    }
}

TEST(HdrDisplay, TheTextureIsRgbaHalfWithAlphaOne) {
    prism::Image img;
    img.width = 2;
    img.height = 1;
    img.pixels = {1.0f, 0.5f, 4.0f, 0.0f, 16.0f, 2.0f};
    EXPECT_EQ(prism::gui::rgba_half_from(img, 1),
              (std::vector<std::uint16_t>{0x3C00, 0x3800, 0x4400, 0x3C00, 0x0000, 0x4C00, 0x4000,
                                          0x3C00}));
}

TEST(HdrDisplay, OneAndEightThreadsGiveTheSameViewerPictureAndTexture) {
    // 1301 x 1213 = 1,578,113 pixels; 1213 = 8 * 151 + 5 rows, so a chunking split leaves rows
    // over.
    const Pair p = pair(1301, 1213, 2.0f);
    const prism::Image one = prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, 2.5f, 1);
    const prism::Image eight = prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, 2.5f, 8);
    EXPECT_TRUE(bits(one) == bits(eight));
    EXPECT_TRUE(prism::gui::rgba_half_from(one, 1) == prism::gui::rgba_half_from(one, 8));
    // And the 8-thread texture is the per-pixel conversion everywhere, the last row included.
    const std::vector<std::uint16_t> tex = prism::gui::rgba_half_from(one, 8);
    for (const std::size_t px :
         {std::size_t{0}, one.pixels.size() / 3 - 1, one.pixels.size() / 3 - 1301 / 2}) {
        ASSERT_EQ(tex[px * 4], prism::gui::half_from_float(one.pixels[px * 3])) << px;
        ASSERT_EQ(tex[px * 4 + 3], 0x3C00u) << px;
    }
}

TEST(HdrDisplay, TranslucentOverlaysGoIntoAnHdrPictureAndTooltipsBecomeOpaque) {
    using prism::gui::overlay_placement;
    for (const CanvasOverlay o :
         {CanvasOverlay::easel_shade, CanvasOverlay::clip_overlay, CanvasOverlay::stroke_overlay,
          CanvasOverlay::split_line, CanvasOverlay::frame_lines, CanvasOverlay::sample_pin,
          CanvasOverlay::tooltip}) {
        EXPECT_EQ(overlay_placement(o, false), OverlayPlacement::on_top);
    }
    EXPECT_EQ(overlay_placement(CanvasOverlay::easel_shade, true), OverlayPlacement::into_picture);
    EXPECT_EQ(overlay_placement(CanvasOverlay::clip_overlay, true), OverlayPlacement::into_picture);
    EXPECT_EQ(overlay_placement(CanvasOverlay::stroke_overlay, true),
              OverlayPlacement::into_picture);
    EXPECT_EQ(overlay_placement(CanvasOverlay::split_line, true), OverlayPlacement::into_picture);
    EXPECT_EQ(overlay_placement(CanvasOverlay::tooltip, true), OverlayPlacement::opaque_on_top);
    EXPECT_EQ(overlay_placement(CanvasOverlay::frame_lines, true), OverlayPlacement::on_top);
    EXPECT_EQ(overlay_placement(CanvasOverlay::sample_pin, true), OverlayPlacement::on_top);
}

TEST(HdrDisplay, AnOverlayInThePictureDimsTheSdrPictureNotTheHighlight) {
    const float picture[3] = {4.0f, 4.0f, 2.0f};  // +2 stops on screen
    const float sdr[3] = {1.0f, 0.8f, 0.5f};
    float out[3];
    using prism::linear_to_srgb;
    using prism::srgb_to_linear;
    // 50% black: the SDR picture at half its sRGB code, as an SDR screen shows it (white becomes
    // 0.214, sRGB 128). Blending over the HDR pixel in linear light would leave +1 stop, 2.0.
    prism::gui::into_picture(picture, sdr, {0.0f, 0.0f, 0.0f, 0.5f}, out);
    EXPECT_FLOAT_EQ(out[0], srgb_to_linear(0.5f));
    EXPECT_NEAR(out[0], 0.214f, 0.001f);
    EXPECT_FLOAT_EQ(out[1], srgb_to_linear(0.5f * linear_to_srgb(0.8f)));
    EXPECT_FLOAT_EQ(out[2], srgb_to_linear(0.5f * linear_to_srgb(0.5f)));
    // A coloured overlay at 25%.
    prism::gui::into_picture(picture, sdr, {1.0f, 0.0f, 0.0f, 0.25f}, out);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    EXPECT_FLOAT_EQ(out[1], srgb_to_linear(0.75f * linear_to_srgb(0.8f)));
    // Clear: the HDR pixel as it is; opaque: the colour; NaN and out-of-range alphas clamp.
    for (const float clear : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()}) {
        prism::gui::into_picture(picture, sdr, {1.0f, 1.0f, 1.0f, clear}, out);
        EXPECT_EQ(out[0], 4.0f);
        EXPECT_EQ(out[2], 2.0f);
    }
    prism::gui::into_picture(picture, sdr, {0.2f, 0.3f, 0.4f, 7.0f}, out);
    EXPECT_NEAR(out[0], 0.2f, 1e-6f);
    EXPECT_NEAR(out[2], 0.4f, 1e-6f);
}

TEST(HdrDisplay, TheUiTargetIsLaidOverTheCanvasPremultiplied) {
    const float canvas[3] = {4.0f, 0.5f, 0.0f};
    float out[3];
    const std::uint8_t opaque[4] = {255, 128, 0, 255};
    prism::gui::ui_over_canvas(canvas, opaque, out);
    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[1], prism::srgb_to_linear(128.0f / 255.0f));
    EXPECT_EQ(out[2], 0.0f);
    const std::uint8_t clear[4] = {0, 0, 0, 0};
    prism::gui::ui_over_canvas(canvas, clear, out);
    EXPECT_EQ(out[0], 4.0f);
    EXPECT_EQ(out[1], 0.5f);
    // Half black over +2 stops leaves about +1 stop: why overlays go into the picture.
    const std::uint8_t half_black[4] = {0, 0, 0, 128};
    prism::gui::ui_over_canvas(canvas, half_black, out);
    EXPECT_FLOAT_EQ(out[0], 4.0f * (1.0f - 128.0f / 255.0f));
    // Half white (premultiplied 128 of 128): white's light at half, plus half the canvas.
    const std::uint8_t half_white[4] = {128, 128, 128, 128};
    prism::gui::ui_over_canvas(canvas, half_white, out);
    EXPECT_FLOAT_EQ(out[1], 128.0f / 255.0f + 0.5f * (1.0f - 128.0f / 255.0f));
}

// --- The preview's HDR picture -------------------------------------------------------------------

namespace {

// Light from black to well above white, varied in every pixel, so +1 stop and filmic put part of
// it above white.
prism::Image bright_base(int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
            const float t = 2.0f * static_cast<float>(x) / static_cast<float>(width - 1);
            img.pixels[i] = t;
            img.pixels[i + 1] = t * (0.4f + 0.6f * static_cast<float>(y) / (height - 1));
            img.pixels[i + 2] = 0.3f * t * t;
        }
    }
    return img;
}

EditSettings natural_hdr(float headroom) {
    EditSettings s = hdr_on(headroom);
    s.exposure_stops = 1.0f;
    s.curve = prism::gui::CurveKind::filmic;
    return s;
}

prism::gui::PreviewFrame render(const prism::Image& base, const EditSettings& s, int threads,
                                std::optional<float> display, StageId stop_after = StageId::encode,
                                bool strip = false) {
    prism::gui::PreviewOptions o;
    o.hdr_display_headroom = display;
    if (strip) {
        o.test_strip = prism::gui::TestStrip{};
    }
    std::optional<prism::gui::PreviewFrame> f =
        prism::gui::render_preview(base, s, threads, {}, {}, stop_after, o);
    EXPECT_TRUE(f.has_value());
    return f ? *f : prism::gui::PreviewFrame{};
}

}  // namespace

TEST(HdrPreview, TheFinalPictureCarriesTheViewerPictureAndItsSdrBytesAreUnchanged) {
    const prism::Image base = bright_base(97, 61);
    const EditSettings s = natural_hdr(2.0f);
    const prism::gui::PreviewFrame sdr = render(base, s, 1, std::nullopt);
    EXPECT_TRUE(sdr.hdr_rgba.empty());
    for (const float d : {1.5f, 4.0f}) {
        const prism::gui::PreviewFrame hdr = render(base, s, 1, d);
        EXPECT_EQ(hdr.rgb8, sdr.rgb8) << d;  // the SDR picture is bit for bit the same
        EXPECT_EQ(hdr.width, sdr.width);
        // What it should be, made independently: process_hdr, then the viewer picture.
        prism::Image img = base;
        prism::Image hdr_img;
        ASSERT_EQ(prism::process_hdr(img, hdr_img, prism::gui::to_process_settings(s), 1),
                  prism::ProcessResult::completed);
        const std::vector<std::uint8_t> bytes = prism::encode_srgb8(img, 1);
        EXPECT_EQ(bytes, sdr.rgb8);
        EXPECT_TRUE(hdr.hdr_rgba == prism::gui::rgba_half_from(
                                        prism::gui::viewer_picture(bytes, hdr_img, 2.0f, d, 1), 1))
            << d;
        EXPECT_FLOAT_EQ(hdr.hdr_weight, prism::gui::canvas_weight(d, 2.0f));
        // At full weight something is above white.
        if (d == 4.0f) {
            bool above = false;
            for (std::size_t i = 0; i < hdr.hdr_rgba.size(); i += 4) {
                above = above || prism::gui::float_from_half(hdr.hdr_rgba[i]) > 1.5f;
            }
            EXPECT_TRUE(above);
        }
    }
}

TEST(HdrPreview, NoViewerPictureForAnEarlierStageATestStripOrSettingsWithoutHdr) {
    const prism::Image base = bright_base(40, 30);
    EXPECT_TRUE(render(base, natural_hdr(2.0f), 1, 4.0f, StageId::curve).hdr_rgba.empty());
    EXPECT_TRUE(render(base, natural_hdr(2.0f), 1, 4.0f, StageId::encode, true).hdr_rgba.empty());
    EditSettings off = natural_hdr(2.0f);
    off.hdr_on = false;
    EXPECT_TRUE(render(base, off, 1, 4.0f).hdr_rgba.empty());
    EXPECT_TRUE(render(base, natural_hdr(0.0f), 1, 4.0f).hdr_rgba.empty());
}

TEST(HdrPreview, OneAndEightThreadsGiveTheSameFrame) {
    // 1301 x 1213 = 1,578,113 pixels, with rows left over after an 8-way split.
    const prism::Image base = bright_base(1301, 1213);
    const EditSettings s = natural_hdr(2.0f);
    const prism::gui::PreviewFrame one = render(base, s, 1, 2.5f);
    const prism::gui::PreviewFrame eight = render(base, s, 8, 2.5f);
    ASSERT_FALSE(one.hdr_rgba.empty());
    EXPECT_TRUE(one.rgb8 == eight.rgb8);
    EXPECT_TRUE(one.hdr_rgba == eight.hdr_rgba);
}

TEST(HdrPreview, WithEveryExtraOnlyTheHdrFieldsAndTheStageListDiffer) {
    // The HDR render goes through process_hdr with the mask and compare callbacks too.
    const prism::Image base = bright_base(97, 61);
    const EditSettings s = natural_hdr(2.0f);
    prism::gui::PreviewOptions o;
    o.scopes = true;
    o.clip_report = true;
    o.clip_mask = true;
    o.compare_stage = StageId::exposure;
    const std::optional<prism::gui::PreviewFrame> sdr =
        prism::gui::render_preview(base, s, 1, {}, {}, StageId::encode, o);
    o.hdr_display_headroom = 4.0f;
    const std::optional<prism::gui::PreviewFrame> hdr =
        prism::gui::render_preview(base, s, 1, {}, {}, StageId::encode, o);
    ASSERT_TRUE(sdr && hdr);
    ASSERT_FALSE(hdr->hdr_rgba.empty());
    EXPECT_EQ(hdr->rgb8, sdr->rgb8);
    EXPECT_EQ(hdr->clip_mask, sdr->clip_mask);
    EXPECT_FALSE(hdr->clip_mask.empty());
    ASSERT_TRUE(hdr->compare && sdr->compare);
    EXPECT_EQ(hdr->compare->rgb8, sdr->compare->rgb8);
    ASSERT_TRUE(hdr->clips && sdr->clips);
    EXPECT_TRUE(*hdr->clips == *sdr->clips);
    ASSERT_TRUE(hdr->scopes && sdr->scopes);
    EXPECT_TRUE(*hdr->scopes == *sdr->scopes);
    EXPECT_TRUE(hdr->histogram == sdr->histogram);
    // The stage list gains "hdr_curve" and nothing else.
    std::vector<std::string_view> a, b;
    for (const prism::StageTime& t : sdr->stages)
        a.push_back(t.name);
    for (const prism::StageTime& t : hdr->stages) {
        if (t.name != "hdr_curve")
            b.push_back(t.name);
    }
    EXPECT_EQ(a, b);
    EXPECT_EQ(hdr->stages.size(), sdr->stages.size() + 1);
}

// --- Overlays drawn into the HDR picture (#106, part 2) -----------------------------------------

namespace {

// The J overlay's pixels as PreviewTexture::upload_overlay made them before they moved into
// gui_core: the SDR texture must stay exactly this.
std::vector<std::uint32_t> old_clip_overlay(const std::vector<std::uint8_t>& mask, int width,
                                            std::uint32_t over, std::uint32_t under,
                                            std::uint32_t gamut) {
    std::vector<std::uint32_t> out(mask.size());
    const std::size_t across = static_cast<std::size_t>(width);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const std::uint8_t m = mask[i];
        const bool stripe = (((i % across) + (i / across)) / 4) % 2 == 0;
        out[i] = (m & prism::gui::clip_mask_high)    ? over
                 : (m & prism::gui::clip_mask_gamut) ? (stripe ? gamut : 0u)
                 : (m & prism::gui::clip_mask_low)   ? under
                                                     : 0u;
    }
    return out;
}

std::vector<std::uint16_t> halves(const prism::Image& img) {
    return prism::gui::rgba_half_from(img, 1);
}

constexpr std::uint32_t over_abgr = 0x99'20'40'f0u;  // a = 0x99, b = 0x20, g = 0x40, r = 0xf0
constexpr std::uint32_t under_abgr = 0x80'f0'60'20u;
constexpr std::uint32_t gamut_abgr = 0xb0'e0'20'c0u;

}  // namespace

TEST(HdrOverlays, TheJOverlaysPixelsAreTheSdrTexturesAsBefore) {
    const int width = 37;
    const int height = 11;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        mask[i] = static_cast<std::uint8_t>((i * 5 + i / 7) % 8);  // every combination of bits
    }
    const std::vector<std::uint32_t> got =
        prism::gui::clip_overlay_pixels(mask, width, height, over_abgr, under_abgr, gamut_abgr);
    EXPECT_TRUE(got == old_clip_overlay(mask, width, over_abgr, under_abgr, gamut_abgr));
    // The stripes: the gamut colour at (0, 0), clear at (4, 0) and (0, 4), again at (8, 0).
    EXPECT_EQ(prism::gui::clip_overlay_abgr(prism::gui::clip_mask_gamut, 0, 0, 1, 2, 3), 3u);
    EXPECT_EQ(prism::gui::clip_overlay_abgr(prism::gui::clip_mask_gamut, 4, 0, 1, 2, 3), 0u);
    EXPECT_EQ(prism::gui::clip_overlay_abgr(prism::gui::clip_mask_gamut, 0, 4, 1, 2, 3), 0u);
    EXPECT_EQ(prism::gui::clip_overlay_abgr(prism::gui::clip_mask_gamut, 8, 0, 1, 2, 3), 3u);
    EXPECT_EQ(prism::gui::clip_overlay_abgr(7, 4, 0, 1, 2, 3), 1u);  // high first
    EXPECT_EQ(prism::gui::clip_overlay_abgr(6, 4, 0, 1, 2, 3), 0u);  // gamut, off its stripe
    EXPECT_EQ(prism::gui::clip_overlay_abgr(2, 4, 0, 1, 2, 3), 2u);
    EXPECT_TRUE(prism::gui::clip_overlay_pixels(mask, width, height + 1, 1, 2, 3).empty());
    // The strokes' layer is stroke_mask_abgr per byte.
    const std::vector<std::uint8_t> strokes = {128, 255, 1, 200, 60};
    const std::vector<std::uint32_t> layer =
        prism::gui::stroke_overlay_pixels(strokes, over_abgr, under_abgr);
    ASSERT_EQ(layer.size(), strokes.size());
    for (std::size_t i = 0; i < strokes.size(); ++i) {
        EXPECT_EQ(layer[i], prism::gui::stroke_mask_abgr(strokes[i], over_abgr, under_abgr)) << i;
    }
    EXPECT_EQ(layer[0], 0u);
}

TEST(HdrOverlays, AnAbgrPixelIsItsCodesInLinearLightWithStraightAlpha) {
    const prism::gui::LinearRgba c = prism::gui::linear_rgba_from_abgr(over_abgr);
    EXPECT_EQ(c.r, prism::srgb8_to_linear(0xf0));
    EXPECT_EQ(c.g, prism::srgb8_to_linear(0x40));
    EXPECT_EQ(c.b, prism::srgb8_to_linear(0x20));
    EXPECT_EQ(c.a, 153.0f / 255.0f);
}

TEST(HdrOverlays, ClearPixelsKeepTheHdrPictureAndCoveredOnesAreTheSdrBlend) {
    // 3 x 2: the HDR picture is +2 stops at (1, 0); the overlay covers (1, 0) and (2, 1) only.
    prism::Image hdr;
    hdr.width = 3;
    hdr.height = 2;
    hdr.pixels = {0.1f, 0.2f, 0.3f, 4.0f, 3.5f, 2.0f, 0.5f, 0.5f,  0.5f,
                  1.5f, 0.7f, 0.2f, 0.0f, 0.0f, 0.0f, 2.5f, 1.25f, 0.6f};
    const std::vector<std::uint8_t> sdr = {30,  60,  90,  255, 250, 190, 180, 180, 180,
                                           255, 200, 120, 0,   0,   0,   255, 230, 160};
    const std::vector<std::uint16_t> tex = halves(hdr);
    const std::vector<std::uint32_t> layer = {0, 0x80000000u, 0, 0, 0, over_abgr};
    const std::span<const std::uint32_t> layers[] = {layer};
    const std::vector<std::uint16_t> got = prism::gui::hdr_with_overlays(tex, sdr, 3, 2, layers, 1);
    ASSERT_EQ(got.size(), tex.size());
    for (const std::size_t px : {0u, 2u, 3u, 4u}) {
        for (int c = 0; c < 4; ++c) {
            EXPECT_EQ(got[px * 4 + c], tex[px * 4 + c]) << px << " " << c;  // bit for bit
        }
    }
    // 50% black over the +2 stop pixel: the SDR picture at half its code, never the highlight.
    for (int c = 0; c < 3; ++c) {
        const float sdr_lin = prism::srgb8_to_linear(sdr[3 + c]);
        float want[3];
        const float picture[3] = {4.0f, 3.5f, 2.0f};
        const float base[3] = {prism::srgb8_to_linear(sdr[3]), prism::srgb8_to_linear(sdr[4]),
                               prism::srgb8_to_linear(sdr[5])};
        prism::gui::into_picture(picture, base, {0.0f, 0.0f, 0.0f, 128.0f / 255.0f}, want);
        EXPECT_EQ(got[4 + c], prism::gui::half_from_float(want[c])) << c;
        EXPECT_NEAR(prism::gui::float_from_half(got[4 + c]),
                    prism::srgb_to_linear(prism::linear_to_srgb(sdr_lin) * 127.0f / 255.0f), 2e-3f)
            << c;
        EXPECT_LT(prism::gui::float_from_half(got[4 + c]), 0.25f) << c;
    }
    EXPECT_EQ(got[4 + 3], tex[4 + 3]);  // alpha stays the picture's
    // The J colour over the bottom-right pixel: its sRGB codes blended over the SDR codes.
    const float a = 153.0f / 255.0f;
    const std::uint8_t codes[3] = {0xf0, 0x40, 0x20};
    for (int c = 0; c < 3; ++c) {
        const float want =
            prism::srgb_to_linear(codes[c] / 255.0f * a + sdr[15 + c] / 255.0f * (1.0f - a));
        EXPECT_NEAR(prism::gui::float_from_half(got[5 * 4 + c]), want, want * 1e-3f + 1e-4f) << c;
    }
    // No layers, or a layer of the wrong size: the picture as it is. Mismatched pictures: none.
    EXPECT_TRUE(prism::gui::hdr_with_overlays(tex, sdr, 3, 2, {}, 1) == tex);
    const std::vector<std::uint32_t> short_layer(5, 0xff000000u);
    const std::span<const std::uint32_t> wrong[] = {short_layer};
    EXPECT_TRUE(prism::gui::hdr_with_overlays(tex, sdr, 3, 2, wrong, 1) == tex);
    EXPECT_TRUE(prism::gui::hdr_with_overlays(tex, sdr, 3, 3, layers, 1).empty());
    EXPECT_TRUE(prism::gui::hdr_with_overlays(tex, std::span<const std::uint8_t>(sdr).first(15), 3,
                                              2, layers, 1)
                    .empty());
}

TEST(HdrOverlays, LayersBlendBottomFirstAsTheSdrCanvasDrawsThem) {
    prism::Image hdr;
    hdr.width = 1;
    hdr.height = 1;
    hdr.pixels = {3.0f, 3.0f, 3.0f};
    const std::vector<std::uint8_t> sdr = {250, 250, 250};
    const std::vector<std::uint16_t> tex = halves(hdr);
    const std::vector<std::uint32_t> j = {over_abgr};
    const std::vector<std::uint32_t> strokes = {under_abgr};
    const std::span<const std::uint32_t> j_then_strokes[] = {j, strokes};
    const std::span<const std::uint32_t> strokes_then_j[] = {strokes, j};
    const std::vector<std::uint16_t> got =
        prism::gui::hdr_with_overlays(tex, sdr, 1, 1, j_then_strokes, 1);
    const std::uint8_t first[3] = {0xf0, 0x40, 0x20};
    const std::uint8_t second[3] = {0x20, 0x60, 0xf0};
    const float a1 = 153.0f / 255.0f;
    const float a2 = 128.0f / 255.0f;
    for (int c = 0; c < 3; ++c) {
        float code = 250.0f / 255.0f;
        code = first[c] / 255.0f * a1 + code * (1.0f - a1);
        code = second[c] / 255.0f * a2 + code * (1.0f - a2);
        const float want = prism::srgb_to_linear(code);
        EXPECT_NEAR(prism::gui::float_from_half(got[c]), want, want * 1e-3f + 1e-4f) << c;
    }
    EXPECT_FALSE(prism::gui::hdr_with_overlays(tex, sdr, 1, 1, strokes_then_j, 1) == got);
}

TEST(HdrOverlays, OneAndEightThreadsGiveTheSamePicture) {
    // 1301 x 1213 = 1,578,113 pixels; 1213 = 8 * 151 + 5 rows, so a chunking split leaves rows
    // over. The overlays cover pixels in every row, the last ones included.
    const Pair p = pair(1301, 1213, 2.0f);
    const std::vector<std::uint16_t> tex =
        halves(prism::gui::viewer_picture(p.sdr, p.hdr, 2.0f, 4.0f, 8));
    std::vector<std::uint8_t> clips(p.sdr.size() / 3);
    std::vector<std::uint8_t> strokes(clips.size());
    for (std::size_t i = 0; i < clips.size(); ++i) {
        clips[i] = static_cast<std::uint8_t>(i % 11 == 0 ? 1 : i % 13 == 0 ? 4 : 0);
        strokes[i] = static_cast<std::uint8_t>(i % 3 == 0 ? 128 : (i * 37) % 256);
    }
    for (std::size_t i = clips.size() - 1301; i < clips.size(); ++i) {
        strokes[i] = 255;  // the last row fully dodged
    }
    const std::vector<std::uint32_t> j =
        prism::gui::clip_overlay_pixels(clips, 1301, 1213, over_abgr, under_abgr, gamut_abgr);
    const std::vector<std::uint32_t> m =
        prism::gui::stroke_overlay_pixels(strokes, over_abgr, under_abgr);
    const std::span<const std::uint32_t> layers[] = {j, m};
    const std::vector<std::uint16_t> one =
        prism::gui::hdr_with_overlays(tex, p.sdr, 1301, 1213, layers, 1);
    const std::vector<std::uint16_t> eight =
        prism::gui::hdr_with_overlays(tex, p.sdr, 1301, 1213, layers, 8);
    ASSERT_EQ(one.size(), tex.size());
    EXPECT_TRUE(one == eight);
    // The last row is covered (by the strokes), so the 8-thread picture differs from the plain
    // one there: every row was drawn into.
    const std::size_t last = clips.size() - 1;
    ASSERT_NE(m[last] >> 24, 0u);
    EXPECT_NE(eight[last * 4], tex[last * 4]);
    EXPECT_NE(one[last * 4], tex[last * 4]);
}

TEST(HdrOverlays, OnlyRealPixelsWithoutAnHdrPictureOrASplitKeepTheCanvasSdr) {
    using prism::gui::CanvasViews;
    using prism::gui::sdr_only_view;
    EXPECT_FALSE(sdr_only_view(CanvasViews{}));
    EXPECT_FALSE(sdr_only_view(CanvasViews{.clip_overlay = true, .stroke_overlay = true}));
    EXPECT_TRUE(sdr_only_view(CanvasViews{.real_pixels = true}));
    EXPECT_TRUE(sdr_only_view(CanvasViews{.split = true}));
    EXPECT_FALSE(sdr_only_view(CanvasViews{
        .clip_overlay = true, .stroke_overlay = true, .real_pixels = false, .split = false}));
}

TEST(HdrOverlays, AMaskIsDrawnInOnlyWhileItShowsAndOnlyIfItIsThePicturesSize) {
    using prism::gui::overlay_drawn_into;
    using prism::gui::OverlayMask;
    EXPECT_TRUE(overlay_drawn_into(OverlayMask{true, 12, 4, 3}, 4, 3));
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{false, 12, 4, 3}, 4, 3));  // J or M off
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 0, 4, 3}, 4, 3));    // no mask in the frame
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 12, 3, 4}, 4, 3));   // another frame's
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 12, 4, 3}, 3, 4));
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 11, 4, 3}, 4, 3));  // bytes short
    // A mask whose size disagrees with its own bytes is no mask of this picture either.
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 12, 6, 3}, 4, 3));
    EXPECT_FALSE(overlay_drawn_into(OverlayMask{true, 0, 0, 0}, 0, 0));   // no picture
}

TEST(HdrViews, ThePictureUnderTheUiIsClippedToEveryPixelTheCanvasTouches) {
    using prism::gui::pixels_covering;
    using prism::gui::WindowPixels;
    EXPECT_EQ(pixels_covering(10.0f, 20.0f, 110.0f, 70.0f, 2.0f), (WindowPixels{20, 40, 200, 100}));
    // Half a unit on a 2x screen is one pixel; a quarter reaches into the next one.
    EXPECT_EQ(pixels_covering(10.25f, 20.5f, 110.25f, 70.75f, 2.0f),
              (WindowPixels{20, 41, 201, 101}));
    EXPECT_EQ(pixels_covering(0.4f, 0.4f, 0.6f, 0.6f, 1.0f), (WindowPixels{0, 0, 1, 1}));
    EXPECT_EQ(pixels_covering(-3.5f, 0.0f, 2.0f, 1.0f, 1.0f), (WindowPixels{-4, 0, 6, 1}));
    EXPECT_EQ(pixels_covering(5.0f, 5.0f, 5.0f, 5.0f, 1.0f).width, 0);
    EXPECT_EQ(pixels_covering(5.0f, 5.0f, 4.0f, 9.0f, 1.0f).width, 0);
}

TEST(HdrViews, TheEaselsVeilIsTheCanvasColourAtSixtyPercent) {
    EXPECT_EQ(prism::gui::easel_veil_abgr(0xff'30'40'50u), 0x99'30'40'50u);  // 153 = 60% of 255
    EXPECT_EQ(prism::gui::easel_veil_abgr(0x00'30'40'50u), 0x99'30'40'50u);
    // Drawn into the HDR picture, the veil over a +2 stop pixel is the SDR picture veiled, as the
    // SDR easel shows it: its codes blended with the canvas grey's, below white.
    prism::Image hdr;
    hdr.width = 1;
    hdr.height = 1;
    hdr.pixels = {4.0f, 4.0f, 3.0f};
    const std::vector<std::uint8_t> sdr = {255, 255, 240};
    const std::vector<std::uint32_t> veil = {prism::gui::easel_veil_abgr(0xff'59'59'59u)};
    const std::span<const std::uint32_t> layers[] = {veil};
    const std::vector<std::uint16_t> got =
        prism::gui::hdr_with_overlays(halves(hdr), sdr, 1, 1, layers, 1);
    const float a = 153.0f / 255.0f;
    for (int c = 0; c < 3; ++c) {
        const float want = prism::srgb_to_linear(0x59 / 255.0f * a + sdr[c] / 255.0f * (1.0f - a));
        EXPECT_NEAR(prism::gui::float_from_half(got[c]), want, want * 1e-3f) << c;
        EXPECT_LT(prism::gui::float_from_half(got[c]), 0.5f) << c;
    }
}

TEST(HdrViews, TheTurnedPhotoIsTwoTrianglesWithAddImageQuadsTextureCorners) {
    // A photo turned a little: top left, top right, bottom right, bottom left, in ImGui units.
    const float corners[8] = {10.0f, 12.0f, 110.0f, 2.0f, 118.0f, 82.0f, 18.0f, 92.0f};
    const std::array<prism::gui::QuadVertex, 6> q = prism::gui::quad_triangles(corners, 2.0f);
    using V = prism::gui::QuadVertex;
    EXPECT_EQ(q[0], (V{20.0f, 24.0f, 0.0f, 0.0f}));
    EXPECT_EQ(q[1], (V{220.0f, 4.0f, 1.0f, 0.0f}));
    EXPECT_EQ(q[2], (V{236.0f, 164.0f, 1.0f, 1.0f}));
    EXPECT_EQ(q[3], q[0]);
    EXPECT_EQ(q[4], q[2]);
    EXPECT_EQ(q[5], (V{36.0f, 184.0f, 0.0f, 1.0f}));
}

TEST(HdrViews, RealPixelsAreKeyedByTheSettingsAndTheHdrRequest) {
    prism::gui::DetailLookNumber look;
    EXPECT_EQ(look.current(), 0u);
    const std::uint64_t first = look.update(7, std::nullopt);
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(look.update(7, std::nullopt), first);  // nothing changed: the same frame still fits
    const std::uint64_t hdr_on = look.update(7, 2.0f);
    EXPECT_NE(hdr_on, first);  // HDR turned on: asked for again
    EXPECT_EQ(look.update(7, 2.0f), hdr_on);
    const std::uint64_t brighter = look.update(7, 2.5f);
    EXPECT_NE(brighter, hdr_on);  // another screen headroom
    const std::uint64_t edited = look.update(8, 2.5f);
    EXPECT_NE(edited, brighter);  // the settings changed
    EXPECT_EQ(look.current(), edited);
    EXPECT_NE(look.update(8, std::nullopt), edited);  // HDR off again
    // Settings generation 0 is a generation like any other.
    prism::gui::DetailLookNumber fresh;
    EXPECT_EQ(fresh.update(0, std::nullopt), 1u);
}

TEST(HdrViews, TheEaselsFrameCoversThePixelsWhoseCentresAreInIt) {
    using prism::gui::pixels_centred_in;
    using prism::gui::WindowPixels;
    EXPECT_EQ(pixels_centred_in(10.0f, 20.0f, 110.0f, 70.0f, 2.0f),
              (WindowPixels{20, 40, 200, 100}));
    // 20.4 and 220.6 device pixels: the centres in it are 20.5 .. 220.5, pixels 20 to 220.
    EXPECT_EQ(pixels_centred_in(10.2f, 20.3f, 110.3f, 70.2f, 2.0f),
              (WindowPixels{20, 41, 201, 99}));
    EXPECT_EQ(pixels_centred_in(5.0f, 5.0f, 4.0f, 9.0f, 1.0f).width, 0);
    // An edge a little past a boundary leaves the next pixel to the veil, on each side.
    EXPECT_EQ(pixels_centred_in(0.3f, 0.3f, 10.2f, 10.2f, 1.0f), (WindowPixels{0, 0, 10, 10}));
}
