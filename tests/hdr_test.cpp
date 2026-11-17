#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "prism/hdr.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"

// The expected values below are literals from the design probe for "Spike: HDR curve on real
// photos" (#101's comment, and the tables in the 0.8.0 design report), not this code's formula.
//
// Tolerance: the probe printed 3 or 4 decimals, and it took the curve's slope at the knee by
// differencing the float curve (step 1e-4 * x), which is off by about 0.1% (filmic: 0.906806
// against the exact 0.907828; Prism uses each curve's exact slope). So a value matches when it is
// within half a unit of the 4th decimal plus 0.05% of itself. For example filmic N = 2 at 0.5 is
// 0.40659 exactly, printed 0.4063 by the probe. The design table's 3-decimal rows sit closest to
// the edge: filmic N = 1 at 1.0 is 0.80884 against 0.808 (tolerance 0.0009), half a unit of the
// 3rd decimal plus the probe's slope error. Any other design (the knee at half white, or no
// lift) is off by 0.1 or more.

namespace {

std::optional<prism::ToneCurve> curve(const char* spec) {
    if (std::string(spec) == "none") {
        return std::nullopt;
    }
    return prism::parse_curve(spec);
}

float hdr(const char* spec, float x, float white, float stops) {
    return prism::make_hdr_curve(curve(spec), white, stops).map(x);
}

double tolerance(double literal) { return 5e-4 + 5e-4 * std::fabs(literal); }

#define EXPECT_PROBE(actual, literal) EXPECT_NEAR((actual), (literal), tolerance(literal))

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    out.reserve(img.pixels.size());
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

}  // namespace

// --- the curve ----------------------------------------------------------------------------------

TEST(HdrCurve, FilmicAtTwoStopsMatchesTheProbe) {
    // #101: filmic N = 2 gives 0.128 / 1.149 / 4.000 at 0.18 / 1 / 2, and 0.4063 at 0.5.
    EXPECT_PROBE(hdr("filmic", 0.18f, 2.0f, 2.0f), 0.128);
    EXPECT_PROBE(hdr("filmic", 1.0f, 2.0f, 2.0f), 1.149);
    EXPECT_PROBE(hdr("filmic", 2.0f, 2.0f, 2.0f), 4.000);
    EXPECT_PROBE(hdr("filmic", 0.5f, 2.0f, 2.0f), 0.4063);
    EXPECT_PROBE(prism::parse_curve("filmic").map(0.5f), 0.3043);  // SDR, for comparison
}

TEST(HdrCurve, NoCurveAtTwoStopsMatchesTheProbe) {
    // #101: none N = 2 gives 0.180 / 1.421 / 4.000.
    EXPECT_PROBE(hdr("none", 0.18f, 2.0f, 2.0f), 0.180);
    EXPECT_PROBE(hdr("none", 1.0f, 2.0f, 2.0f), 1.421);
    EXPECT_PROBE(hdr("none", 2.0f, 2.0f, 2.0f), 4.000);
}

TEST(HdrCurve, EveryCurveAndHeadroomMatchesTheDesignTable) {
    // Design report, "Design B: knee at middle grey (scene 0.18), S lands on H": scene 1.0 and
    // 0.18 for N = 1, 2, 4; S = 2.
    struct Row {
        const char* spec;
        double grey;
        double at_one[3];
    };
    const Row rows[] = {
        {"none", 0.180, {1.000, 1.421, 2.871}},
        {"gamma:2.2", 0.459, {1.211, 1.722, 3.477}},
        {"reinhard:4", 0.154, {0.864, 1.228, 2.480}},
        {"filmic", 0.128, {0.808, 1.149, 2.321}},
    };
    const float stops[] = {1.0f, 2.0f, 4.0f};
    for (const Row& r : rows) {
        for (int i = 0; i < 3; ++i) {
            SCOPED_TRACE(std::string(r.spec) + " N=" + std::to_string(stops[i]));
            EXPECT_PROBE(hdr(r.spec, 0.18f, 2.0f, stops[i]), r.grey);
            EXPECT_PROBE(hdr(r.spec, 1.0f, 2.0f, stops[i]), r.at_one[i]);
            // 2, 4, 8 and 16 all print as H in the table: S and everything above it.
            for (const float x : {2.0f, 4.0f, 8.0f, 16.0f}) {
                EXPECT_EQ(hdr(r.spec, x, 2.0f, stops[i]), std::exp2(stops[i])) << x;
            }
        }
    }
}

TEST(HdrCurve, AtAndBelowMiddleGreyItIsTheSdrCurveExactly) {
    for (const char* spec : {"none", "gamma:2.2", "reinhard:4", "filmic"}) {
        const prism::HdrCurve h = prism::make_hdr_curve(curve(spec), 2.0f, 2.0f);
        ASSERT_TRUE(h.lift) << spec;
        for (const float x : {-0.5f, 0.0f, 1e-6f, 0.01f, 0.09f, 0.17f, 0.18f}) {
            const float sdr = curve(spec) ? curve(spec)->map(x) : x;
            EXPECT_EQ(std::bit_cast<std::uint32_t>(h.map(x)), std::bit_cast<std::uint32_t>(sdr))
                << spec << " at " << x;
        }
    }
}

TEST(HdrCurve, JoinsTheSdrCurveSmoothlyAtTheKneeAndRisesToHeadroom) {
    for (const char* spec : {"none", "gamma:2.2", "reinhard:4", "filmic"}) {
        const prism::HdrCurve h = prism::make_hdr_curve(curve(spec), 2.0f, 2.0f);
        const auto c = [&](float x) { return curve(spec) ? curve(spec)->map(x) : x; };
        // Continuous: just above the knee it is within a hair of the SDR value there.
        const float above = 0.18f * 1.0001f;
        EXPECT_NEAR(h.map(above), c(above), 1e-5f * c(above)) << spec;
        // Same slope (stops per stop) on both sides of the knee: a step of 0.1% gives the same
        // rise (about 0.0014 * slope) to within 3e-6, so a slope off by 0.5% fails.
        const double sdr_rise = std::log2(c(0.18f * 1.001f) / c(0.18f));
        const double hdr_rise = std::log2(h.map(0.18f * 1.001f) / h.map(0.18f));
        EXPECT_NEAR(hdr_rise, sdr_rise, 3e-6) << spec;
        // Just below S it is just below H.
        EXPECT_GT(h.map(1.99f), 3.9f) << spec;
        EXPECT_LT(h.map(1.99f), 4.0f) << spec;
    }
}

TEST(HdrCurve, NeverPassesHeadroomAndNeverFallsAsLightGrows) {
    // Includes a bright white with a small headroom: there the parabola peaks before S (filmic,
    // S = 64, N = 0.1), and the cut at H must keep the curve from falling back.
    struct Case {
        const char* spec;
        float white;
        float stops;
    };
    for (const Case& k : {Case{"filmic", 2.0f, 2.0f}, Case{"filmic", 64.0f, 0.1f},
                          Case{"none", 64.0f, 0.1f}, Case{"gamma:2.2", 8.0f, 4.0f},
                          Case{"reinhard:4", 0.5f, 1.0f}}) {
        const prism::HdrCurve h = prism::make_hdr_curve(curve(k.spec), k.white, k.stops);
        float previous = h.map(0.0f);
        for (float x = 0.0f; x < 200.0f; x = x * 1.01f + 1e-4f) {
            const float v = h.map(x);
            ASSERT_LE(v, h.peak) << k.spec << " " << x;
            ASSERT_GE(v, previous) << k.spec << " " << x;
            previous = v;
        }
        EXPECT_EQ(h.map(std::numeric_limits<float>::infinity()), h.peak);
    }
}

TEST(HdrCurve, ThePeakBeforeWhiteCaseIsReallyTested) {
    // Guards the case above: with S = 64 and N = 0.1 the curve reaches H before S.
    const prism::HdrCurve h = prism::make_hdr_curve(curve("filmic"), 64.0f, 0.1f);
    EXPECT_LT(h.bend, 0.0f);
    EXPECT_EQ(h.map(40.0f), h.peak);
}

TEST(HdrCurve, GuardWhenWhiteIsAtOrBelowMiddleGrey) {
    // Probe: filmic N = 2 at 0.5 with S = 0.18 and 0.17 is the SDR 0.3043 (no lift), with S = 0.5
    // it is 4.0000, and with S = 1.0 it is 0.6327.
    EXPECT_PROBE(hdr("filmic", 0.5f, 0.18f, 2.0f), 0.3043);
    EXPECT_PROBE(hdr("filmic", 0.5f, 0.17f, 2.0f), 0.3043);
    EXPECT_PROBE(hdr("filmic", 0.5f, 0.5f, 2.0f), 4.0000);
    EXPECT_PROBE(hdr("filmic", 0.5f, 1.0f, 2.0f), 0.6327);
    EXPECT_FALSE(prism::make_hdr_curve(curve("filmic"), 0.18f, 2.0f).lift);
    EXPECT_FALSE(prism::make_hdr_curve(curve("filmic"), 0.0f, 2.0f).lift);
    EXPECT_FALSE(prism::make_hdr_curve(curve("filmic"), -1.0f, 2.0f).lift);
    // Without the lift the curve is still cut at H: gamma lifts values above 1.
    EXPECT_EQ(hdr("gamma:0.25", 3.0f, 0.1f, 1.0f), 2.0f);
}

TEST(HdrCurve, HeadroomOutsideItsRangeIsRefused) {
    for (const float stops : {0.0f, -0.1f, 4.01f, std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        EXPECT_THROW((void)prism::make_hdr_curve(curve("filmic"), 2.0f, stops),
                     std::invalid_argument)
            << stops;
    }
    EXPECT_THROW((void)prism::make_hdr_curve(curve("filmic"),
                                             std::numeric_limits<float>::infinity(), 2.0f),
                 std::invalid_argument);
    EXPECT_NO_THROW((void)prism::make_hdr_curve(curve("filmic"), 2.0f, 4.0f));
    EXPECT_NO_THROW((void)prism::make_hdr_curve(curve("filmic"), 2.0f, 0.1f));
}

TEST(HdrCurve, RangeAndDefault) {
    // #96 decision 3 as changed by its last comment: 0 to +4.0 stops, default +2.0.
    EXPECT_EQ(prism::headroom_min_stops, 0.0f);
    EXPECT_EQ(prism::headroom_max_stops, 4.0f);
    EXPECT_EQ(prism::headroom_default_stops, 2.0f);
}

TEST(HdrCurve, LogSlopesAtMiddleGrey) {
    // Measured independently (double-precision central differences of each formula).
    EXPECT_NEAR(prism::tone_curve_log_slope(std::nullopt, 0.18), 1.0, 1e-9);
    EXPECT_NEAR(prism::tone_curve_log_slope(curve("gamma:2.2"), 0.18), 0.454545, 1e-6);
    EXPECT_NEAR(prism::tone_curve_log_slope(curve("reinhard:4"), 0.18), 0.858582, 1e-6);
    EXPECT_NEAR(prism::tone_curve_log_slope(curve("filmic"), 0.18), 0.907828, 1e-6);
}

// --- the file's white ---------------------------------------------------------------------------

TEST(HdrFileWhite, NaturalIsTwo) {
    prism::ProcessSettings s;
    s.exposure_stops = 1.0f;
    s.saturation = 1.0f;
    s.contrast = 1.0f;
    s.curve = prism::parse_curve("filmic");
    EXPECT_EQ(prism::hdr_file_white(s), 2.0f);
    EXPECT_EQ(prism::hdr_file_white(prism::ProcessSettings{}), 1.0f);
}

TEST(HdrFileWhite, FollowsContrastAndTheBrightestChannel) {
    prism::ProcessSettings s;
    s.exposure_stops = 1.0f;
    s.contrast = 1.15f;
    // 2 * 2^(0.3 * u / sqrt(1 + u^2)), u = log2(2 / 0.18) / 2: 2.39494.
    EXPECT_NEAR(prism::hdr_file_white(s), 2.39494, 2e-4);

    prism::ProcessSettings m;
    m.exposure_stops = 1.0f;
    m.matrix = prism::Matrix3{1.5f, 0, 0, 0, 1, 0, 0, 0, 0.8f};
    EXPECT_EQ(prism::hdr_file_white(m), 3.0f);  // red: 2 * 1.5
    m.matrix.reset();
    m.white_balance = prism::Matrix3{1, 0, 0, 0, 1, 0, 0, 0, 1.25f};
    EXPECT_EQ(prism::hdr_file_white(m), 2.5f);  // blue: 2 * 1.25
}

// --- process_hdr --------------------------------------------------------------------------------

namespace {

prism::Image ramp(int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        // 0 to 3.0 in steps, so every part of the curve is used, plus a few negatives.
        img.pixels[i] = static_cast<float>(static_cast<int>(i % 1009) - 9) / 330.0f;
    }
    return img;
}

prism::ProcessSettings look() {
    prism::ProcessSettings s;
    s.exposure_stops = 0.5f;
    s.white_balance = prism::Matrix3{1.1f, 0, 0, 0, 1, 0, 0, 0, 0.9f};
    s.matrix = prism::Matrix3{1.05f, 0.03f, -0.01f, 0.01f, 1.0f, 0.0f, -0.02f, -0.02f, 0.94f};
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = prism::parse_curve("filmic");
    return s;
}

}  // namespace

TEST(ProcessHdr, HeadroomZeroIsExactlyProcess) {
    prism::Image expected = ramp(17, 5);
    ASSERT_EQ(prism::process(expected, look(), 1), prism::ProcessResult::completed);
    prism::Image img = ramp(17, 5);
    prism::Image out = ramp(3, 3);  // anything left in it is cleared
    std::vector<std::string> names;
    ASSERT_EQ(prism::process_hdr(img, out, look(), 1, {},
                                 [&](const prism::StageTime& t) { names.emplace_back(t.name); }),
              prism::ProcessResult::completed);
    EXPECT_EQ(bits(img), bits(expected));
    EXPECT_EQ(out.width, 0);
    EXPECT_TRUE(out.pixels.empty());
    EXPECT_EQ(names, (std::vector<std::string>{"exposure", "white_balance", "matrix",
                                               "saturation", "contrast", "curve"}));
}

TEST(ProcessHdr, TheSdrBranchIsUnchangedAndTheHdrBranchFollowsContrast) {
    prism::ProcessSettings s = look();
    s.headroom_stops = 2.0f;
    // 64 x 16 so the ramp reaches 3.0, past the file's white.

    prism::Image sdr_expected = ramp(64, 16);
    ASSERT_EQ(prism::process(sdr_expected, s, 1), prism::ProcessResult::completed);

    // By hand: every stage up to contrast, then the HDR curve with the file's white.
    prism::Image hdr_expected = ramp(64, 16);
    prism::ProcessSettings to_contrast = s;
    to_contrast.curve.reset();
    to_contrast.headroom_stops = 0.0f;
    ASSERT_EQ(prism::process(hdr_expected, to_contrast, 1), prism::ProcessResult::completed);
    prism::hdr_curve(hdr_expected,
                     prism::make_hdr_curve(s.curve, prism::hdr_file_white(s), 2.0f), 1);

    prism::Image img = ramp(64, 16);
    prism::Image out;
    std::vector<std::string> names;
    ASSERT_EQ(prism::process_hdr(img, out, s, 1, {},
                                 [&](const prism::StageTime& t) { names.emplace_back(t.name); }),
              prism::ProcessResult::completed);
    EXPECT_EQ(bits(img), bits(sdr_expected));
    EXPECT_EQ(out.width, 64);
    EXPECT_EQ(out.height, 16);
    EXPECT_EQ(bits(out), bits(hdr_expected));
    EXPECT_EQ(names, (std::vector<std::string>{"exposure", "white_balance", "matrix",
                                               "saturation", "contrast", "curve", "hdr_curve"}));
    // Light above white is kept, up to and including H.
    float brightest = 0.0f;
    for (const float v : out.pixels) {
        brightest = std::max(brightest, v);
    }
    EXPECT_EQ(brightest, 4.0f);
}

TEST(ProcessHdr, ABadHeadroomThrowsBeforeAnyPixelChanges) {
    for (const float stops : {-1.0f, 4.5f, std::numeric_limits<float>::quiet_NaN()}) {
        prism::ProcessSettings s = look();
        s.headroom_stops = stops;
        prism::Image img = ramp(4, 3);
        const prism::Image before = img;
        prism::Image out;
        EXPECT_THROW((void)prism::process_hdr(img, out, s, 1), std::invalid_argument) << stops;
        EXPECT_EQ(bits(img), bits(before));
    }
}

TEST(ProcessHdr, AStopBeforeTheBranchLeavesNoHdrImage) {
    prism::ProcessSettings s = look();
    s.headroom_stops = 2.0f;
    std::stop_source stop;
    stop.request_stop();
    prism::Image img = ramp(4, 3);
    prism::Image out = ramp(2, 2);
    EXPECT_EQ(prism::process_hdr(img, out, s, 1, stop.get_token()),
              prism::ProcessResult::cancelled);
    EXPECT_TRUE(out.pixels.empty());
}

TEST(ProcessHdr, OneAndEightThreadsGiveTheSameBits) {
    // At least 1,572,864 px (1.5 x parallel_min_pixels), so 8 threads really run, and a height
    // that 8 does not divide (1213 = 8 * 151 + 5), so the chunks are unequal.
    const int width = 1301;
    const int height = 1213;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    ASSERT_GE(pixels, 1572864u);
    ASSERT_NE(height % 8, 0);
    ASSERT_EQ(prism::threads_for_pixels(pixels, 8), 8);

    prism::ProcessSettings s = look();
    s.headroom_stops = 2.5f;
    prism::Image one = ramp(width, height);
    prism::Image one_hdr;
    ASSERT_EQ(prism::process_hdr(one, one_hdr, s, 1), prism::ProcessResult::completed);
    prism::Image eight = ramp(width, height);
    prism::Image eight_hdr;
    ASSERT_EQ(prism::process_hdr(eight, eight_hdr, s, 8), prism::ProcessResult::completed);
    EXPECT_EQ(bits(one), bits(eight));
    EXPECT_EQ(bits(one_hdr), bits(eight_hdr));
    // The HDR branch really lifted something: not a copy of the SDR picture.
    EXPECT_NE(bits(one_hdr), bits(one));
}

TEST(ProcessHdr, NaturalEndToEndMatchesTheProbe) {
    // Exposure +1 makes the file's white 2.0, as in the probe, so input 0.5 reaches the curve as
    // scene 1.0, 0.09 as 0.18 and 1.0 as 2.0. Literals from #101 and the design table (N = 2).
    struct Case {
        const char* spec;
        double grey;
        double one;
    };
    for (const Case& k : {Case{"filmic", 0.128, 1.149}, Case{"none", 0.180, 1.421},
                          Case{"gamma:2.2", 0.459, 1.722}, Case{"reinhard:4", 0.154, 1.228}}) {
        SCOPED_TRACE(k.spec);
        prism::ProcessSettings s;
        s.exposure_stops = 1.0f;
        s.curve = curve(k.spec);
        s.headroom_stops = 2.0f;
        prism::Image img{3, 1, {0.09f, 0.09f, 0.09f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f}};
        prism::Image out;
        ASSERT_EQ(prism::process_hdr(img, out, s, 1), prism::ProcessResult::completed);
        for (int c = 0; c < 3; ++c) {
            EXPECT_PROBE(out.pixels[c], k.grey);
            EXPECT_PROBE(out.pixels[3 + c], k.one);
            EXPECT_EQ(out.pixels[6 + c], 4.0f);
        }
    }
}

TEST(ProcessHdr, WhiteBelowMiddleGreyGivesTheSdrCurveEndToEnd) {
    // Exposure -4: the file's white is 1/16, below 0.18, so the guard applies and the HDR branch
    // is the SDR curve (filmic never reaches H = 4).
    prism::ProcessSettings s;
    s.exposure_stops = -4.0f;
    s.curve = prism::parse_curve("filmic");
    s.headroom_stops = 2.0f;
    prism::Image img = ramp(64, 16);
    prism::Image out;
    ASSERT_EQ(prism::process_hdr(img, out, s, 1), prism::ProcessResult::completed);
    EXPECT_EQ(bits(out), bits(img));
}

TEST(ProcessHdr, TheHdrCurveReceivesTheImageAfterContrast) {
    prism::ProcessSettings s = look();
    s.headroom_stops = 2.0f;
    prism::Image after_contrast = ramp(8, 4);
    prism::ProcessSettings to_contrast = s;
    to_contrast.curve.reset();
    to_contrast.headroom_stops = 0.0f;
    ASSERT_EQ(prism::process(after_contrast, to_contrast, 1), prism::ProcessResult::completed);

    prism::Image img = ramp(8, 4);
    prism::Image out;
    std::vector<std::uint32_t> seen;
    ASSERT_EQ(prism::process_hdr(img, out, s, 1, {}, {},
                                 [&](std::string_view name, const prism::Image& input) {
                                     if (name == "hdr_curve") {
                                         seen = bits(input);
                                     }
                                 }),
              prism::ProcessResult::completed);
    EXPECT_EQ(seen, bits(after_contrast));
}

TEST(ProcessHdr, AStopAfterTheBranchLeavesNoHdrImage) {
    // The stop arrives while the SDR curve is about to run: "curve" still runs, "hdr_curve" does
    // not, and `hdr` must not be left holding the unfinished copy.
    prism::ProcessSettings s = look();
    s.headroom_stops = 2.0f;
    std::stop_source stop;
    prism::Image img = ramp(4, 3);
    prism::Image out;
    std::vector<std::string> names;
    EXPECT_EQ(prism::process_hdr(
                  img, out, s, 1, stop.get_token(),
                  [&](const prism::StageTime& t) { names.emplace_back(t.name); },
                  [&](std::string_view name, const prism::Image&) {
                      if (name == "curve") {
                          stop.request_stop();
                      }
                  }),
              prism::ProcessResult::cancelled);
    EXPECT_EQ(names.back(), "curve");
    EXPECT_TRUE(out.pixels.empty());
    EXPECT_EQ(out.width, 0);
}

TEST(ProcessHdr, ABadHeadroomLeavesTheHdrImageAlone) {
    prism::ProcessSettings s = look();
    s.headroom_stops = 9.0f;
    prism::Image img = ramp(4, 3);
    prism::Image out = ramp(2, 2);
    const prism::Image before = out;
    EXPECT_THROW((void)prism::process_hdr(img, out, s, 1), std::invalid_argument);
    EXPECT_EQ(bits(out), bits(before));
}
