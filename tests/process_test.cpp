#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"

namespace {

// 4x2 pixels of varied colours, dark to bright, so that every stage changes every pixel.
prism::Image make_image() {
    prism::Image img;
    img.width = 4;
    img.height = 2;
    img.pixels = {0.02f, 0.05f, 0.01f, 0.18f, 0.18f, 0.18f, 0.4f, 0.1f, 0.05f, 0.9f, 0.7f, 0.2f,
                  0.05f, 0.3f,  0.6f,  1.5f,  1.2f,  0.8f,  0.3f, 0.6f, 0.1f,  3.0f, 0.5f, 2.0f};
    return img;
}

// The pixels' bit patterns: compares NaNs and -0 exactly, where comparing floats would not.
std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

const prism::Matrix3 grade = {1.05f,  0.03f,  -0.01f,  //
                              0.01f,  1.00f,  0.00f,   //
                              -0.02f, -0.02f, 0.94f};

prism::ProcessSettings all_stages() {
    prism::ProcessSettings s;
    s.exposure_stops = 0.5f;
    s.matrix = grade;
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = prism::parse_curve("filmic");
    return s;
}

// process() on make_image(), returning the result, the image and the names of the stages that ran.
struct Outcome {
    prism::ProcessResult result;
    prism::Image img;
    std::vector<std::string> stages;
};

Outcome run_process(const prism::ProcessSettings& s, std::stop_token stop = {}) {
    Outcome r{prism::ProcessResult::completed, make_image(), {}};
    r.result = prism::process(r.img, s, 1, stop,
                              [&](const prism::StageTime& t) { r.stages.emplace_back(t.name); });
    return r;
}

using Names = std::vector<std::string>;

}  // namespace

// --- each stage alone, by hand vs through process()
// -----------------------------------------------

TEST(Process, ExposureAlone) {
    prism::ProcessSettings s;
    s.exposure_stops = -1.5f;
    prism::Image expected = make_image();
    prism::exposure(expected, -1.5f, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(r.result, prism::ProcessResult::completed);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, Names{"exposure"});
}

TEST(Process, MatrixAlone) {
    prism::ProcessSettings s;
    s.matrix = grade;
    prism::Image expected = make_image();
    prism::apply_matrix(expected, grade, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, Names{"matrix"});
}

TEST(Process, SaturationAlone) {
    prism::ProcessSettings s;
    s.saturation = 0.5f;
    prism::Image expected = make_image();
    prism::saturation(expected, 0.5f, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, Names{"saturation"});
}

TEST(Process, ContrastAlone) {
    prism::ProcessSettings s;
    s.contrast = 1.3f;
    prism::Image expected = make_image();
    prism::contrast(expected, 1.3f, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, Names{"contrast"});
}

TEST(Process, CurveAlone) {
    prism::ProcessSettings s;
    s.curve = prism::parse_curve("reinhard:2");
    prism::Image expected = make_image();
    prism::tone_curve(expected, *s.curve, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, Names{"curve"});
}

// --- all together, and the order
// ------------------------------------------------------------------

TEST(Process, AllStagesMatchCallingThemByHandInReadmeOrder) {
    const prism::ProcessSettings s = all_stages();
    prism::Image expected = make_image();
    prism::exposure(expected, *s.exposure_stops, 1);
    prism::apply_matrix(expected, *s.matrix, 1);
    prism::saturation(expected, *s.saturation, 1);
    prism::contrast(expected, *s.contrast, 1);
    prism::tone_curve(expected, *s.curve, 1);

    const Outcome r = run_process(s);
    EXPECT_EQ(r.result, prism::ProcessResult::completed);
    EXPECT_EQ(bits(r.img), bits(expected));
    EXPECT_EQ(r.stages, (Names{"exposure", "matrix", "saturation", "contrast", "curve"}));
}

TEST(Process, OrderMatters) {
    // Exposure before the curve is not the same as after it, so this checks that process() really
    // applies them in pipeline order rather than, say, the order the fields are declared.
    prism::ProcessSettings s;
    s.exposure_stops = 1.0f;
    s.curve = prism::parse_curve("filmic");
    prism::Image curve_first = make_image();
    prism::tone_curve(curve_first, *s.curve, 1);
    prism::exposure(curve_first, 1.0f, 1);

    const Outcome r = run_process(s);
    EXPECT_NE(bits(r.img), bits(curve_first));
    EXPECT_EQ(r.stages, (Names{"exposure", "curve"}));
}

TEST(Process, ThreadCountDoesNotChangeTheResult) {
    // Big enough (> parallel_min_pixels) that several threads really are used.
    prism::Image one;
    one.width = 1100;
    one.height = 1000;
    one.pixels.resize(static_cast<std::size_t>(one.width) * one.height * 3);
    for (std::size_t i = 0; i < one.pixels.size(); ++i) {
        one.pixels[i] = static_cast<float>(i % 1000) / 500.0f;
    }
    prism::Image many = one;
    EXPECT_EQ(prism::process(one, all_stages(), 1), prism::ProcessResult::completed);
    EXPECT_EQ(prism::process(many, all_stages(), 4), prism::ProcessResult::completed);
    EXPECT_EQ(bits(one), bits(many));
}

// --- neutral settings -------------------------------------------------------------------------

TEST(Process, NeutralSettingsLeaveEveryBitUnchanged) {
    // Values that the stages would not pass through bit for bit: the identity matrix turns -0 into
    // +0 and infinity into NaN (0 * inf), so this only passes because the stages are skipped.
    prism::Image img;
    img.width = 3;
    img.height = 1;
    img.pixels = {
        -0.0f,  std::numeric_limits<float>::infinity(),   std::numeric_limits<float>::quiet_NaN(),
        -0.25f, std::numeric_limits<float>::denorm_min(), 0.18f,
        1e30f,  -std::numeric_limits<float>::infinity(),  0.5f};
    const std::vector<std::uint32_t> before = bits(img);

    prism::ProcessSettings neutral;
    neutral.exposure_stops = 0.0f;
    neutral.matrix = prism::identity_matrix;
    neutral.saturation = 1.0f;
    neutral.contrast = 1.0f;
    int calls = 0;
    const auto count = [&](const prism::StageTime&) { ++calls; };

    EXPECT_EQ(prism::process(img, neutral, 1, {}, count), prism::ProcessResult::completed);
    EXPECT_EQ(bits(img), before);
    EXPECT_EQ(prism::process(img, prism::ProcessSettings{}, 1, {}, count),
              prism::ProcessResult::completed);
    EXPECT_EQ(bits(img), before);
    EXPECT_EQ(calls, 0);
}

// --- cancelling -------------------------------------------------------------------------------

TEST(Process, StopRequestedBeforehandRunsNothing) {
    std::stop_source source;
    source.request_stop();

    const Outcome r = run_process(all_stages(), source.get_token());
    EXPECT_EQ(r.result, prism::ProcessResult::cancelled);
    EXPECT_EQ(bits(r.img), bits(make_image()));
    EXPECT_TRUE(r.stages.empty());
}

TEST(Process, StopRequestedMidwayKeepsTheStagesThatRan) {
    // The callback runs between stages, so asking for a stop from it after the matrix stage is a
    // deterministic way to cancel half way through (a GUI would call request_stop from another
    // thread instead).
    std::stop_source source;
    prism::Image img = make_image();
    Names stages;
    const prism::ProcessResult result =
        prism::process(img, all_stages(), 1, source.get_token(), [&](const prism::StageTime& t) {
            stages.emplace_back(t.name);
            if (t.name == "matrix") {
                source.request_stop();
            }
        });

    prism::Image expected = make_image();
    prism::exposure(expected, *all_stages().exposure_stops, 1);
    prism::apply_matrix(expected, grade, 1);
    EXPECT_EQ(result, prism::ProcessResult::cancelled);
    EXPECT_EQ(stages, (Names{"exposure", "matrix"}));
    EXPECT_EQ(bits(img), bits(expected));
}

TEST(Process, StopAfterTheLastStageStillCompletes) {
    std::stop_source source;
    prism::Image img = make_image();
    const prism::ProcessResult result =
        prism::process(img, all_stages(), 1, source.get_token(), [&](const prism::StageTime& t) {
            if (t.name == "curve") {
                source.request_stop();
            }
        });
    EXPECT_EQ(result, prism::ProcessResult::completed);
}

// --- errors -----------------------------------------------------------------------------------

TEST(Process, InconsistentImageThrows) {
    prism::Image img = make_image();
    img.pixels.pop_back();
    EXPECT_THROW((void)prism::process(img, all_stages()), std::invalid_argument);
}
