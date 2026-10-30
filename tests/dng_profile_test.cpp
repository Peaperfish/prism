#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "env_var.hpp"
#include "prism/camera_profile.hpp"
#include "prism/dng_profile.hpp"
#include "tiff_builder.hpp"

namespace fs = std::filesystem;

using prism::DngProfile;
using prism::detail::TiffType;
using prism_test::TiffBuilder;

namespace {

// Made-up matrices, as integers over 10000: each entry is stored as the SRATIONAL n/10000, so the
// parsed double must equal n / 10000.0 exactly. (No real camera's data is used in these tests.)
using IntMat = std::array<std::array<std::int32_t, 3>, 3>;
constexpr IntMat color_a = {{{9050, -3400, 250}, {-3900, 11800, 2150}, {-450, 1300, 7400}}};
constexpr IntMat color_d65 = {{{8100, -2700, -500}, {-4400, 12200, 2400}, {-800, 1700, 6200}}};
constexpr IntMat forward_a = {{{6890, 1830, 920}, {2470, 9150, -1620}, {60, -2830, 10520}}};
constexpr IntMat forward_d65 = {{{7120, 1350, 1180}, {2810, 8520, -1330}, {150, -1950, 9550}}};
constexpr IntMat calibration_1 = {{{10123, 0, 0}, {0, 10000, 0}, {0, 0, 9871}}};
constexpr IntMat calibration_2 = {{{9987, 0, 0}, {0, 10000, 0}, {0, 0, 10042}}};

std::vector<std::pair<std::int32_t, std::int32_t>> over_10000(const std::vector<std::int32_t>& v) {
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    for (std::int32_t n : v) {
        out.emplace_back(n, 10000);
    }
    return out;
}

std::vector<std::pair<std::int32_t, std::int32_t>> over_10000(const IntMat& m) {
    std::vector<std::int32_t> v;
    for (const auto& row : m) {
        v.insert(v.end(), row.begin(), row.end());  // row scan order
    }
    return over_10000(v);
}

prism::Mat3 expected(const IntMat& m) {
    prism::Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = m[i][j] / 10000.0;
        }
    }
    return out;
}

std::vector<float> floats(std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<float>(i % 7) * 0.25f;
    }
    return v;
}

// A complete two-illuminant profile (A and D65). As a DNG it also has the IFD 0 tags that a DCP
// can't carry: DNGVersion, CameraCalibration, AnalogBalance, AsShotNeutral and the camera's
// calibration signature. The tables are tiny, but their sizes are what matter.
TiffBuilder profile_file(bool big_endian, DngProfile::Source source) {
    const bool dng = source == DngProfile::Source::dng;
    TiffBuilder b(big_endian, dng ? 42 : 0x4352);
    if (dng) {
        b.bytes(50706, TiffType::byte, {1, 7, 1, 0});
    }
    b.ascii(50708, "TEST-CAM")
        .srationals(50721, over_10000(color_a))
        .srationals(50722, over_10000(color_d65));
    if (dng) {
        b.srationals(50723, over_10000(calibration_1))
            .srationals(50724, over_10000(calibration_2))
            .rationals(50727, {{10000, 10000}, {10250, 10000}, {9875, 10000}})
            .rationals(50728, {{4500, 10000}, {10000, 10000}, {6250, 10000}});
    }
    b.shorts(50778, {17}).shorts(50779, {21});
    if (dng) {
        b.ascii(50931, "com.example");
    }
    b.ascii(50932, "com.example")
        .ascii(50936, "Test Standard")
        .longs(50937, {4, 2, 1})
        .floats(50938, floats(4 * 2 * 1 * 3))
        .floats(50939, floats(4 * 2 * 1 * 3))
        .floats(50940, {0.0f, 0.0f, 0.5f, 0.6f, 1.0f, 1.0f})
        .longs(50941, {1})
        .ascii(50942, "Copyright test")
        .srationals(50964, over_10000(forward_a))
        .srationals(50965, over_10000(forward_d65))
        .longs(50981, {6, 2, 3})
        .floats(50982, floats(6 * 2 * 3 * 3));
    return b;
}

// The smallest valid profile: one ColorMatrix and its illuminant.
TiffBuilder minimal_file() {
    TiffBuilder b(false, 0x4352);
    b.srationals(50721, over_10000(color_d65)).shorts(50778, {21});
    return b;
}

void expect_matrix(const std::optional<prism::Mat3>& actual, const IntMat& m) {
    ASSERT_TRUE(actual.has_value());
    const prism::Mat3 e = expected(m);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_EQ((*actual)[i][j], e[i][j]) << "[" << i << "][" << j << "]";
        }
    }
}

// Parses, expecting std::runtime_error whose message contains `expected`.
void expect_error(const std::vector<std::byte>& file, const std::string& expected) {
    try {
        (void)prism::parse_dng_profile(file);
        ADD_FAILURE() << "parsed without error; expected \"" << expected << "\"";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos) << e.what();
    }
}

}  // namespace

// --- Round trips ---------------------------------------------------------------------------------

TEST(DngProfile, RoundTripsAsDcpAndDngInBothByteOrders) {
    for (const bool big : {false, true}) {
        for (const DngProfile::Source source : {DngProfile::Source::dcp, DngProfile::Source::dng}) {
            const bool dng = source == DngProfile::Source::dng;
            SCOPED_TRACE(std::string(big ? "MM" : "II") + (dng ? " DNG" : " DCP"));
            const std::vector<std::byte> file = profile_file(big, source).build();
            const DngProfile p = prism::parse_dng_profile(file);

            EXPECT_EQ(p.source, source);
            EXPECT_EQ(p.calibrations[0].illuminant, 17);
            EXPECT_EQ(p.calibrations[1].illuminant, 21);
            expect_matrix(p.calibrations[0].color_matrix, color_a);
            expect_matrix(p.calibrations[1].color_matrix, color_d65);
            expect_matrix(p.calibrations[0].forward_matrix, forward_a);
            expect_matrix(p.calibrations[1].forward_matrix, forward_d65);
            EXPECT_FALSE(p.ignored_third_calibration);

            if (dng) {
                expect_matrix(p.camera_calibration[0], calibration_1);
                expect_matrix(p.camera_calibration[1], calibration_2);
                ASSERT_TRUE(p.analog_balance.has_value());
                EXPECT_EQ(*p.analog_balance, (prism::Vec3{1.0, 1.025, 0.9875}));
                ASSERT_TRUE(p.as_shot_neutral.has_value());
                EXPECT_EQ(*p.as_shot_neutral, (prism::Vec3{0.45, 1.0, 0.625}));
                EXPECT_EQ(p.camera_calibration_signature, "com.example");
            } else {
                EXPECT_FALSE(p.camera_calibration[0].has_value());
                EXPECT_FALSE(p.camera_calibration[1].has_value());
                EXPECT_FALSE(p.analog_balance.has_value());
                EXPECT_FALSE(p.as_shot_neutral.has_value());
                EXPECT_FALSE(p.camera_calibration_signature.has_value());
            }
            EXPECT_EQ(p.profile_calibration_signature, "com.example");
            EXPECT_EQ(p.unique_camera_model, "TEST-CAM");
            EXPECT_EQ(p.profile_name, "Test Standard");
            EXPECT_EQ(p.profile_copyright, "Copyright test");
            EXPECT_EQ(p.embed_policy, 1u);

            // Only the sizes of the skipped tables.
            ASSERT_TRUE(p.hue_sat_map.has_value());
            EXPECT_EQ(p.hue_sat_map->hue_divisions, 4u);
            EXPECT_EQ(p.hue_sat_map->sat_divisions, 2u);
            EXPECT_EQ(p.hue_sat_map->val_divisions, 1u);
            EXPECT_EQ(p.hue_sat_map_tables, 2);
            ASSERT_TRUE(p.look_table.has_value());
            EXPECT_EQ(p.look_table->hue_divisions, 6u);
            EXPECT_EQ(p.look_table->sat_divisions, 2u);
            EXPECT_EQ(p.look_table->val_divisions, 3u);
            EXPECT_EQ(p.tone_curve_points, 3u);

            // The calibrations go straight into the existing DNG maths: D65 is closest to D65,
            // and its ForwardMatrix wins over its ColorMatrix.
            const prism::Mat3 srgb = prism::camera_to_srgb_from_dng(p.calibrations);
            const prism::Mat3 want =
                prism::camera_to_srgb_from_forward_matrix(expected(forward_d65));
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    EXPECT_EQ(srgb[i][j], want[i][j]);
                }
            }
        }
    }
}

TEST(DngProfile, OptionalTagsMayAllBeMissing) {
    const std::vector<std::byte> file = minimal_file().build();
    const DngProfile p = prism::parse_dng_profile(file);
    EXPECT_EQ(p.source, DngProfile::Source::dcp);
    EXPECT_EQ(p.calibrations[0].illuminant, 21);
    expect_matrix(p.calibrations[0].color_matrix, color_d65);
    EXPECT_FALSE(p.calibrations[0].forward_matrix.has_value());
    EXPECT_EQ(p.calibrations[1].illuminant, 0);
    EXPECT_FALSE(p.calibrations[1].color_matrix.has_value());
    EXPECT_FALSE(p.calibrations[1].forward_matrix.has_value());
    EXPECT_FALSE(p.analog_balance || p.as_shot_neutral || p.unique_camera_model || p.profile_name ||
                 p.profile_copyright || p.embed_policy || p.hue_sat_map || p.look_table ||
                 p.tone_curve_points || p.camera_calibration_signature ||
                 p.profile_calibration_signature);
    EXPECT_EQ(p.hue_sat_map_tables, 0);

    // A ForwardMatrix alone is also enough, in either calibration.
    TiffBuilder forward_only(true, 42);
    forward_only.srationals(50965, over_10000(forward_d65)).longs(50779, {21});
    const DngProfile f = prism::parse_dng_profile(forward_only.build());
    expect_matrix(f.calibrations[1].forward_matrix, forward_d65);
    EXPECT_FALSE(f.calibrations[0].color_matrix || f.calibrations[0].forward_matrix);
}

TEST(DngProfile, AsShotNeutralMayBeShorts) {
    TiffBuilder b = minimal_file();
    b.shorts(50728, {512, 1024, 768});
    const DngProfile p = prism::parse_dng_profile(b.build());
    EXPECT_EQ(p.as_shot_neutral, (prism::Vec3{512, 1024, 768}));
}

TEST(DngProfile, TagsWithAnUnexpectedTypeAreIgnored) {
    // ColorMatrix1 as FLOAT and ProfileEmbedPolicy as SRATIONAL aren't allowed by the spec, so
    // they're skipped like an unknown type: as if absent. The illuminant as a LONG is fine (TIFF
    // readers accept BYTE, SHORT or LONG for an unsigned integer).
    TiffBuilder b(false, 0x4352);
    b.floats(50721, floats(9))
        .longs(50778, {17})
        .srationals(50722, over_10000(color_d65))
        .bytes(50779, TiffType::byte, {21})
        .srationals(50941, {{1, 1}})
        .shorts(50936, {65, 66})
        .bytes(50942, TiffType::byte, {'(', 'c', ')', 0});
    const DngProfile p = prism::parse_dng_profile(b.build());
    EXPECT_FALSE(p.calibrations[0].color_matrix.has_value());
    EXPECT_EQ(p.calibrations[0].illuminant, 17);
    expect_matrix(p.calibrations[1].color_matrix, color_d65);
    EXPECT_EQ(p.calibrations[1].illuminant, 21);
    EXPECT_FALSE(p.embed_policy.has_value());
    EXPECT_FALSE(p.profile_name.has_value());  // SHORT is not text
    EXPECT_EQ(p.profile_copyright, "(c)");     // BYTE is ("ASCII or BYTE" in the spec)

    // If the only matrix has the wrong type, there is no matrix at all.
    TiffBuilder only_float(false, 0x4352);
    only_float.floats(50721, floats(9)).shorts(50778, {21});
    expect_error(only_float.build(), "no ColorMatrix or ForwardMatrix");
}

TEST(DngProfile, ReportsAndIgnoresAThirdCalibration) {
    TiffBuilder b = profile_file(false, DngProfile::Source::dcp);
    b.shorts(52529, {23}).srationals(52531, over_10000(color_a));
    const DngProfile p = prism::parse_dng_profile(b.build());
    EXPECT_TRUE(p.ignored_third_calibration);
    EXPECT_EQ(p.calibrations[0].illuminant, 17);
    EXPECT_EQ(p.calibrations[1].illuminant, 21);
    expect_matrix(p.calibrations[1].color_matrix, color_d65);
}

TEST(DngProfile, KeepsStringsExactlyAsRead) {
    // Sanitising happens when a string is written out (prism_profile, step 4), not here.
    const std::string name = "Evil\nname\t\x1b[31m\r\n9 9 9";
    TiffBuilder b = minimal_file();
    b.ascii(50936, name).ascii(50942, "\x01\x7f\xff tail").ascii(50708, "");
    const DngProfile p = prism::parse_dng_profile(b.build());
    EXPECT_EQ(p.profile_name, name);
    EXPECT_EQ(p.profile_copyright, "\x01\x7f\xff tail");
    EXPECT_EQ(p.unique_camera_model, "");
}

// --- Rejections ----------------------------------------------------------------------------------

TEST(DngProfile, RejectsCamerasWithoutThreeColourPlanes) {
    const std::vector<std::int32_t> twelve(12, 2500);
    TiffBuilder cm(false, 0x4352);
    cm.srationals(50721, over_10000(twelve)).shorts(50778, {21});
    expect_error(cm.build(),
                 "ColorMatrix1 has 12 values, expected 9 (for 3 colour planes); that is a "
                 "4-colour camera, and only 3-colour (RGB) cameras are supported");

    TiffBuilder fm = minimal_file();
    fm.srationals(50964, over_10000(twelve));
    expect_error(fm.build(), "ForwardMatrix1 has 12 values");

    TiffBuilder cc = minimal_file();
    cc.srationals(50723, over_10000(std::vector<std::int32_t>(16, 2500)));
    expect_error(cc.build(),
                 "CameraCalibration1 has 16 values, expected 9 (for 3 colour planes); "
                 "that is a 4-colour camera");

    TiffBuilder neutral = minimal_file();
    neutral.rationals(50728, {{1, 2}, {1, 1}, {1, 1}, {2, 3}});
    expect_error(neutral.build(), "AsShotNeutral has 4 values, expected 3");
}

TEST(DngProfile, RejectsWrongCounts) {
    TiffBuilder cm(false, 0x4352);
    cm.srationals(50721, over_10000(std::vector<std::int32_t>(8, 1000))).shorts(50778, {21});
    expect_error(cm.build(), "ColorMatrix1 has 8 values, expected 9");

    TiffBuilder ab = minimal_file();
    ab.rationals(50727, {{1, 1}, {1, 1}});
    expect_error(ab.build(), "AnalogBalance has 2 values, expected 3");

    TiffBuilder ill(false, 0x4352);
    ill.srationals(50721, over_10000(color_d65)).shorts(50778, {21, 17});
    expect_error(ill.build(), "CalibrationIlluminant1 has 2 values, expected 1");

    TiffBuilder dims = minimal_file();
    dims.longs(50937, {90, 30});
    expect_error(dims.build(), "ProfileHueSatMapDims has 2 values, expected 3");

    TiffBuilder policy = minimal_file();
    policy.longs(50941, {0, 1});
    expect_error(policy.build(), "ProfileEmbedPolicy has 2 values, expected 1");

    TiffBuilder curve = minimal_file();
    curve.floats(50940, {0.0f, 0.0f, 1.0f});
    expect_error(curve.build(), "ProfileToneCurve has 3 values");
}

TEST(DngProfile, RejectsZeroDenominators) {
    auto values = over_10000(color_a);
    values[4].second = 0;
    TiffBuilder cm(true, 42);
    cm.srationals(50722, values).shorts(50779, {17});
    expect_error(cm.build(),
                 "ColorMatrix2: TIFF: tag 50722 value 4 is an SRATIONAL with denominator 0");

    TiffBuilder ab = minimal_file();
    ab.rationals(50727, {{1, 1}, {1, 0}, {1, 1}});
    expect_error(ab.build(), "AnalogBalance: TIFF: tag 50727 value 1 is a RATIONAL");
}
// Non-finite values can't be stored in the RATIONAL and SRATIONAL types that the matrices and
// vectors use (a nonzero denominator always gives a finite quotient), so parse_dng_profile's
// finite check has no test that can reach it; it guards against future FLOAT tags.

TEST(DngProfile, RejectsAMatrixWithoutItsIlluminant) {
    TiffBuilder cm(false, 0x4352);
    cm.srationals(50721, over_10000(color_d65)).shorts(50779, {21});  // illuminant 2, not 1
    expect_error(cm.build(), "ColorMatrix1 has no CalibrationIlluminant1");

    TiffBuilder fm(false, 42);
    fm.srationals(50721, over_10000(color_d65))
        .shorts(50778, {21})
        .srationals(50965, over_10000(forward_d65));
    expect_error(fm.build(), "ForwardMatrix2 has no CalibrationIlluminant2");
}

TEST(DngProfile, RejectsProfilesWithoutMatrices) {
    TiffBuilder b(false, 42);
    b.bytes(50706, TiffType::byte, {1, 7, 1, 0}).ascii(50708, "MONO").shorts(50778, {21});
    expect_error(b.build(), "no ColorMatrix or ForwardMatrix");
}

TEST(DngProfile, RejectsFilesThatAreNotTiff) {
    const std::string json = R"({"version": 1, "model": "X"})";
    std::vector<std::byte> file;
    for (char c : json) {
        file.push_back(static_cast<std::byte>(c));
    }
    expect_error(file, "TIFF: not a TIFF file");
}

// --- Loading files -------------------------------------------------------------------------------

TEST(DngProfile, LoadReadsTheFileAndNamesItInErrors) {
    const fs::path dir = fs::path(testing::TempDir());
    const fs::path good = dir / "prism_dng_profile_test.dcp";
    const std::vector<std::byte> bytes = profile_file(true, DngProfile::Source::dcp).build();
    {
        std::ofstream out(good, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const DngProfile p = prism::load_dng_profile(good.string());
    EXPECT_EQ(p.profile_name, "Test Standard");
    expect_matrix(p.calibrations[1].forward_matrix, forward_d65);

    const fs::path bad = dir / "prism_dng_profile_test_bad.dcp";
    std::ofstream(bad) << "not a profile";
    try {
        (void)prism::load_dng_profile(bad.string());
        ADD_FAILURE() << "no error";
    } catch (const std::runtime_error& e) {
        // The path first, then the reader's own message.
        EXPECT_EQ(std::string(e.what()).rfind(bad.string() + ": TIFF: not a TIFF file", 0), 0u)
            << e.what();
    }

    EXPECT_THROW((void)prism::load_dng_profile((dir / "prism_no_such_profile.dcp").string()),
                 std::runtime_error);
    fs::remove(good);
    fs::remove(bad);
}

TEST(DngProfile, LoadRefusesFilesOverTheSizeLimit) {
    // A sparse file: it has the size but takes no disk space, and is refused before reading.
    const fs::path big = fs::path(testing::TempDir()) / "prism_dng_profile_test_big.dng";
    std::ofstream(big).put('I');
    fs::resize_file(big, prism::max_dng_profile_bytes + 1);
    try {
        (void)prism::load_dng_profile(big.string());
        ADD_FAILURE() << "no error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("536870913 bytes, more than the 536870912-byte limit"),
                  std::string::npos)
            << e.what();
    }
    fs::remove(big);
}

// --- Damaged files -------------------------------------------------------------------------------

TEST(DngProfile, SurvivesEveryTruncationAndEveryFlippedByte) {
    // Whatever the damage, parse_dng_profile must return a profile or throw std::runtime_error:
    // never another exception, a crash or an out-of-bounds read (which the ASan build reports).
    const auto check = [](std::span<const std::byte> file, const std::string& what) {
        try {
            (void)prism::parse_dng_profile(file);
        } catch (const std::runtime_error&) {
        } catch (const std::exception& e) {
            ADD_FAILURE() << what << ": unexpected exception: " << e.what();
        } catch (...) {
            ADD_FAILURE() << what << ": unexpected non-standard exception";
        }
    };
    for (const bool big : {false, true}) {
        for (const DngProfile::Source source : {DngProfile::Source::dcp, DngProfile::Source::dng}) {
            const std::vector<std::byte> file = profile_file(big, source).build();
            const std::string name = std::string(big ? "MM" : "II") +
                                     (source == DngProfile::Source::dng ? " DNG" : " DCP");
            for (std::size_t len = 0; len <= file.size(); ++len) {
                const std::vector<std::byte> prefix(
                    file.begin(), file.begin() + static_cast<std::ptrdiff_t>(len));
                check(prefix, name + " truncated to " + std::to_string(len));
            }
            for (std::size_t i = 0; i < file.size(); ++i) {
                for (const std::uint8_t mask :
                     std::initializer_list<std::uint8_t>{0xFF, 0x80, 0x01}) {
                    std::vector<std::byte> damaged = file;
                    damaged[i] ^= static_cast<std::byte>(mask);
                    check(damaged,
                          name + " byte " + std::to_string(i) + " ^ " + std::to_string(mask));
                }
            }
        }
    }
}

// --- Real files (opt-in) -------------------------------------------------------------------------
//
// Camera profiles and DNGs are copyrighted and large, so none are in the repo. To check that real
// files parse, point these at your own:
//
//   PRISM_TEST_DCP="/Library/Application Support/Adobe/CameraRaw/CameraProfiles/..." \
//   PRISM_TEST_DNG=~/Pictures/photo.dng ctest --test-dir build -R RealFile
//
// They only check that the file parses and that its matrices are finite and invertible: no values
// are printed or stored.

namespace {

void expect_usable_matrix(const std::optional<prism::Mat3>& m, const std::string& name) {
    if (!m) {
        return;
    }
    bool finite = true;
    for (const auto& row : *m) {
        for (double v : row) {
            finite = finite && std::isfinite(v);
        }
    }
    EXPECT_TRUE(finite) << name << " has a non-finite value";
    EXPECT_NO_THROW((void)prism::invert(*m)) << name << " is not invertible";
}

void check_real_file(const char* variable, DngProfile::Source source) {
    const std::string path = prism_test::env_var(variable);
    if (path.empty()) {
        GTEST_SKIP() << "set " << variable << " to a file to run this test";
    }
    DngProfile p;
    try {
        p = prism::load_dng_profile(path);
    } catch (const std::exception& e) {
        FAIL() << e.what();
    }
    EXPECT_EQ(p.source, source);
    bool any = false;
    for (std::size_t k = 0; k < 2; ++k) {
        const std::string n = std::to_string(k + 1);
        expect_usable_matrix(p.calibrations[k].color_matrix, "ColorMatrix" + n);
        expect_usable_matrix(p.calibrations[k].forward_matrix, "ForwardMatrix" + n);
        expect_usable_matrix(p.camera_calibration[k], "CameraCalibration" + n);
        any = any || p.calibrations[k].color_matrix || p.calibrations[k].forward_matrix;
    }
    EXPECT_TRUE(any);
}

}  // namespace

TEST(DngProfile, RealFileDcpParses) {
    check_real_file("PRISM_TEST_DCP", DngProfile::Source::dcp);
}

TEST(DngProfile, RealFileDngParses) {
    check_real_file("PRISM_TEST_DNG", DngProfile::Source::dng);
}
