#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"

namespace fs = std::filesystem;
using prism::gui::CurveKind;
using prism::gui::EditSettings;
using prism::gui::SimpleControls;

namespace {

prism::Image make_image() {
    prism::Image img;
    img.width = 3;
    img.height = 2;
    img.pixels = {0.02f, 0.05f, 0.01f, 0.18f, 0.18f, 0.18f, 0.4f, 0.1f, 0.05f,
                  0.9f,  0.7f,  0.2f,  0.05f, 0.3f,  0.6f,  1.5f, 1.2f, 0.8f};
    return img;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

prism::Image processed(const EditSettings& s) {
    prism::Image img = make_image();
    EXPECT_EQ(prism::process(img, prism::gui::to_process_settings(s), 1),
              prism::ProcessResult::completed);
    return img;
}

fs::path write_temp(const std::string& name, const std::string& contents) {
    const fs::path path = fs::path(testing::TempDir()) / ("prism_gui_settings_" + name);
    std::ofstream(path) << contents;
    return path;
}

bool mentions(const std::vector<std::string>& problems, const std::string& text) {
    for (const std::string& p : problems) {
        if (p.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// --- defaults, equality, reset ----------------------------------------------------------------

TEST(EditSettings, DefaultsLeaveTheImageUnchanged) {
    const EditSettings s;
    EXPECT_TRUE(prism::gui::validate(s).empty());
    EXPECT_EQ(bits(processed(s)), bits(make_image()));
    EXPECT_EQ(prism::gui::simple_from(s), (SimpleControls{0, 0, 0}));
}

TEST(EditSettings, EqualityComparesEveryField) {
    EditSettings a;
    EditSettings b;
    EXPECT_EQ(a, b);
    b.curve_on = false;
    EXPECT_NE(a, b);
    b = a;
    b.raw_camera_matrix = prism::gui::MatrixFile{"cam.txt", prism::identity_matrix};
    EXPECT_NE(a, b);
    a.raw_camera_matrix = prism::gui::MatrixFile{"other.txt", prism::identity_matrix};
    EXPECT_NE(a, b) << "same values from another file still differ: the UI shows the file name";
}

TEST(EditSettings, ResetRestoresTheDefaults) {
    EditSettings s;
    s.exposure_stops = 1.5f;
    s.saturation_on = false;
    s.curve = CurveKind::filmic;
    s.matrix = prism::gui::MatrixFile{"grade.txt", prism::identity_matrix};
    s.raw_half_size = true;
    prism::gui::reset_to_defaults(s);
    EXPECT_EQ(s, EditSettings{});
}

// --- validation -------------------------------------------------------------------------------

TEST(EditSettings, ValidationExplainsEachProblem) {
    EditSettings s;
    s.saturation = 12.0f;
    s.contrast = 0.05f;
    s.gamma = 0.0f;
    s.reinhard_white = -1.0f;
    s.exposure_stops = std::numeric_limits<float>::infinity();
    const std::vector<std::string> problems = prism::gui::validate(s);
    EXPECT_EQ(problems.size(), 5u);
    EXPECT_TRUE(mentions(problems, "Saturation must be from 0 to 10 (it is 12)"));
    EXPECT_TRUE(mentions(problems, "Contrast must be from 0.1 to 10 (it is 0.05)"));
    EXPECT_TRUE(mentions(problems, "Gamma must be a number above 0 (it is 0)"));
    EXPECT_TRUE(mentions(problems, "Reinhard white point must be a number above 0"));
    EXPECT_TRUE(mentions(problems, "Exposure must be a finite number"));
}

TEST(EditSettings, StagesThatAreOffAreStillValidated) {
    EditSettings s;
    s.saturation = std::numeric_limits<float>::quiet_NaN();
    s.saturation_on = false;
    EXPECT_TRUE(mentions(prism::gui::validate(s), "Saturation"));
}

TEST(EditSettings, InvalidSettingsDoNotConvert) {
    EditSettings s;
    s.contrast = 20.0f;
    try {
        (void)prism::gui::to_process_settings(s);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("Contrast must be from 0.1 to 10"), std::string::npos)
            << e.what();
    }
}

// --- to_process_settings ----------------------------------------------------------------------

TEST(EditSettings, StagesThatAreOffAreSkipped) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.saturation = 2.0f;
    s.contrast = 1.3f;
    s.curve = CurveKind::filmic;
    s.matrix = prism::gui::MatrixFile{"m.txt", {0, 0, 1, 0, 1, 0, 1, 0, 0}};
    s.exposure_on = s.matrix_on = s.saturation_on = s.contrast_on = s.curve_on = false;

    const prism::ProcessSettings p = prism::gui::to_process_settings(s);
    EXPECT_FALSE(p.exposure_stops || p.matrix || p.saturation || p.contrast || p.curve);
    EXPECT_EQ(bits(processed(s)), bits(make_image()));
}

TEST(EditSettings, EveryStageMatchesTheCliOptions) {
    EditSettings s;
    s.exposure_stops = 0.5f;
    s.matrix =
        prism::gui::MatrixFile{"m.txt", {1.05f, 0.03f, -0.01f, 0.01f, 1, 0, -0.02f, -0.02f, 0.94f}};
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = CurveKind::filmic;

    // What prism --exposure 0.5 --matrix m.txt --saturation 1.25 --contrast 1.15 --curve filmic
    // does (src/main.cpp builds the same ProcessSettings from its options).
    prism::ProcessSettings cli;
    cli.exposure_stops = 0.5f;
    cli.matrix = s.matrix->values;
    cli.saturation = 1.25f;
    cli.contrast = 1.15f;
    cli.curve = prism::parse_curve("filmic");
    prism::Image expected = make_image();
    ASSERT_EQ(prism::process(expected, cli, 1), prism::ProcessResult::completed);
    EXPECT_EQ(bits(processed(s)), bits(expected));
}

TEST(EditSettings, CurveSpecsReadBackAsTheSameFloat) {
    EditSettings s;
    EXPECT_EQ(prism::gui::curve_spec(s), std::nullopt);
    s.curve = CurveKind::filmic;
    EXPECT_EQ(prism::gui::curve_spec(s), "filmic");
    s.curve = CurveKind::reinhard;
    EXPECT_EQ(prism::gui::curve_spec(s), "reinhard:4");
    s.curve = CurveKind::gamma;
    EXPECT_EQ(prism::gui::curve_spec(s), "gamma:2.2");

    // Awkward values too: the curve must use exactly the float in the settings, as make_gamma
    // and make_reinhard in lib/pipeline.cpp compute it.
    for (const float v : {0.1f + 0.2f, 1.0f / 3.0f, 2.2000003f, 7.123456789f, 1e-3f, 37.5f}) {
        s.curve = CurveKind::gamma;
        s.gamma = v;
        const prism::ToneCurve gamma = *prism::gui::to_process_settings(s).curve;
        EXPECT_EQ(std::get<prism::GammaCurve>(gamma.shape).inv_gamma, 1.0f / v) << v;

        s.curve = CurveKind::reinhard;
        s.reinhard_white = v;
        const prism::ToneCurve reinhard = *prism::gui::to_process_settings(s).curve;
        EXPECT_EQ(std::get<prism::ReinhardCurve>(reinhard.shape).inv_white_sq, 1.0f / (v * v)) << v;
    }
}

TEST(MatrixFile, LoadsAFileAndReportsBadOnes) {
    const fs::path good = write_temp("good.txt", "1 0 0\n0 2 0 # comment\n0 0 3\n");
    const prism::gui::MatrixFile m = prism::gui::MatrixFile::load(good);
    EXPECT_EQ(m.path, good);
    EXPECT_EQ(m.values, (prism::Matrix3{1, 0, 0, 0, 2, 0, 0, 0, 3}));

    const fs::path bad = write_temp("bad.txt", "1 0 0\n0 x 0\n");
    try {
        (void)prism::gui::MatrixFile::load(bad);
        FAIL() << "expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("line 2"), std::string::npos) << e.what();
    }
    fs::remove(good);
    fs::remove(bad);
}

// --- Simple mode mapping ----------------------------------------------------------------------

TEST(SimpleControls, NeutralSlidersGiveExactlyNeutralSettings) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.contrast = 2.0f;
    s.saturation = 0.3f;
    prism::gui::apply_simple(s, {0, 0, 0});
    // Exactly 0, 1 and 1 (not merely close), so process() skips these stages.
    EXPECT_EQ(s, EditSettings{});
}

TEST(SimpleControls, ExtremesMapToTheDocumentedValues) {
    EditSettings s;
    prism::gui::apply_simple(s, {100, 100, 100});
    EXPECT_FLOAT_EQ(s.exposure_stops, 2.0f);
    EXPECT_FLOAT_EQ(s.contrast, 1.5f);
    EXPECT_FLOAT_EQ(s.saturation, 2.0f);

    prism::gui::apply_simple(s, {-100, -100, -100});
    EXPECT_FLOAT_EQ(s.exposure_stops, -2.0f);
    EXPECT_FLOAT_EQ(s.contrast, 1.0f / 1.5f);
    EXPECT_FLOAT_EQ(s.saturation, 0.0f);  // black and white
    EXPECT_TRUE(prism::gui::validate(s).empty());
}

TEST(SimpleControls, HalfwayPointsAreOneStopAndMirrorImages) {
    EditSettings s;
    prism::gui::apply_simple(s, {50, 50, 50});
    EXPECT_FLOAT_EQ(s.exposure_stops, 1.0f);
    EXPECT_FLOAT_EQ(s.saturation, 1.5f);
    const float up = s.contrast;
    prism::gui::apply_simple(s, {-50, -50, -50});
    EXPECT_FLOAT_EQ(s.exposure_stops, -1.0f);
    EXPECT_FLOAT_EQ(s.saturation, 0.5f);
    EXPECT_FLOAT_EQ(std::log2(up), -std::log2(s.contrast));  // same number of stops each way
}

TEST(SimpleControls, PositionsBeyondTheEndsAreClamped) {
    EditSettings s;
    prism::gui::apply_simple(s, {250, -300, std::numeric_limits<float>::quiet_NaN()});
    EXPECT_FLOAT_EQ(s.exposure_stops, 2.0f);
    EXPECT_FLOAT_EQ(s.contrast, 1.0f / 1.5f);
    EXPECT_EQ(s.saturation, 1.0f);
}

TEST(SimpleControls, ReadingBackGivesTheSliderPositions) {
    for (int b = -100; b <= 100; b += 10) {
        for (int c = -100; c <= 100; c += 25) {
            EditSettings s;
            const SimpleControls in{static_cast<float>(b), static_cast<float>(c),
                                    static_cast<float>(-b)};
            prism::gui::apply_simple(s, in);
            const SimpleControls out = prism::gui::simple_from(s);
            EXPECT_NEAR(out.brightness, in.brightness, 1e-3) << b << " " << c;
            EXPECT_NEAR(out.contrast, in.contrast, 1e-3) << b << " " << c;
            EXPECT_NEAR(out.colour, in.colour, 1e-3) << b << " " << c;
            const prism::gui::SimpleOutOfRange range = prism::gui::outside_simple_range(s);
            EXPECT_FALSE(range.brightness || range.contrast || range.colour);
        }
    }
}

TEST(SimpleControls, ValuesFromAdvancedModeBeyondTheSlidersAreFlagged) {
    EditSettings s;
    s.exposure_stops = -3.0f;  // below -2
    s.contrast = 0.5f;         // below 1 / 1.5
    s.saturation = 3.0f;       // above 2
    EXPECT_EQ(prism::gui::simple_from(s), (SimpleControls{-100, -100, 100}));
    const prism::gui::SimpleOutOfRange range = prism::gui::outside_simple_range(s);
    EXPECT_TRUE(range.brightness);
    EXPECT_TRUE(range.contrast);
    EXPECT_TRUE(range.colour);
}

TEST(SimpleControls, StagesThatAreOffShowNeutralAndMovingASliderTurnsThemOn) {
    EditSettings s;
    s.exposure_stops = 1.0f;
    s.exposure_on = false;
    s.saturation = 3.0f;
    s.saturation_on = false;
    EXPECT_EQ(prism::gui::simple_from(s), (SimpleControls{0, 0, 0}));
    EXPECT_FALSE(prism::gui::outside_simple_range(s).colour);

    prism::gui::apply_simple(s, {10, 0, 0});
    EXPECT_TRUE(s.exposure_on && s.saturation_on && s.contrast_on);
}

TEST(SimpleControls, AdvancedOnlyFieldsAreLeftAlone) {
    EditSettings s;
    s.curve = CurveKind::gamma;
    s.gamma = 1.8f;
    s.curve_on = false;
    s.matrix = prism::gui::MatrixFile{"grade.txt", prism::identity_matrix};
    s.raw_half_size = true;
    EditSettings expected = s;
    prism::gui::apply_simple(s, {40, -20, 60});
    expected.exposure_stops = s.exposure_stops;
    expected.contrast = s.contrast;
    expected.saturation = s.saturation;
    EXPECT_EQ(s, expected);
}
