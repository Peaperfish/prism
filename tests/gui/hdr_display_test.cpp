// The HDR canvas's decisions (#106): when it is on, the viewer weight, the viewer picture, half
// floats for the texture, where overlays go, and the compositing maths.

#include "prism/gui/hdr_display.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "prism/color.hpp"
#include "prism/gain_map.hpp"
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
