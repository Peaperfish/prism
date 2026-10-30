#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <sys/wait.h>
#include <unistd.h>  // getpid
#endif

#include "prism/camera_profile.hpp"
#include "prism/dng_profile.hpp"
#include "prism/pipeline.hpp"
#include "prism/version.hpp"
#include "tiff_builder.hpp"

// End-to-end tests of prism_profile (path supplied by CMake as PRISM_PROFILE_EXE) reading DNG and
// DCP files, built in memory with TiffBuilder from made-up matrices: no real camera's data.

namespace fs = std::filesystem;

using prism::DngProfile;
using prism::detail::TiffType;
using prism_test::TiffBuilder;

namespace {

// As in cli_test.cpp: the test's name and the process id keep `ctest -j` runs apart.
fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_profile_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

void write_file(const fs::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

struct RunResult {
    int exit_code;
    std::string out;  // stdout
    std::string err;  // stderr, kept apart to check where warnings go
};

RunResult run_profile(const std::string& args) {
    const fs::path out = temp_file("stdout.txt");
    const fs::path err = temp_file("stderr.txt");
    const std::string command = "\"" + std::string(PRISM_PROFILE_EXE) + "\" " + args + " > \"" +
                                out.string() + "\" 2> \"" + err.string() + "\"";
    const int status = std::system(command.c_str());
    RunResult r{0, read_file(out), read_file(err)};
    fs::remove(out);
    fs::remove(err);
#ifdef _WIN32
    r.exit_code = status;
#else
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return r;
}

std::string quoted(const fs::path& path) {
    return "\"" + path.string() + "\"";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Made-up matrices as integers over 10000, as in dng_profile_test.cpp.
using IntMat = std::array<std::array<std::int32_t, 3>, 3>;
constexpr IntMat color_a = {{{9050, -3400, 250}, {-3900, 11800, 2150}, {-450, 1300, 7400}}};
constexpr IntMat color_d65 = {{{8100, -2700, -500}, {-4400, 12200, 2400}, {-800, 1700, 6200}}};
constexpr IntMat forward_a = {{{6890, 1830, 920}, {2470, 9150, -1620}, {60, -2830, 10520}}};
constexpr IntMat forward_d65 = {{{7120, 1350, 1180}, {2810, 8520, -1330}, {150, -1950, 9550}}};
constexpr IntMat calibration = {{{10123, 0, 0}, {0, 10000, 0}, {0, 0, 9871}}};

std::vector<std::pair<std::int32_t, std::int32_t>> over_10000(const IntMat& m) {
    std::vector<std::pair<std::int32_t, std::int32_t>> out;
    for (const auto& row : m) {
        for (std::int32_t n : row) {
            out.emplace_back(n, 10000);
        }
    }
    return out;
}

constexpr std::uint16_t dcp_magic = 0x4352;

// Which matrices a test profile has. Every profile has both illuminants (A and D65), a camera
// model and a profile name; the strings can be replaced to test sanitising.
struct Spec {
    bool big_endian = false;
    std::uint16_t magic = dcp_magic;
    bool color_matrices = true;
    bool forward_matrices = true;
    bool second_calibration = true;
    bool tables = false;
    bool dng_extras = false;  // CameraCalibration1/2 and AnalogBalance (DNG only in real files)
    bool third_calibration = false;
    std::optional<std::uint32_t> embed_policy;
    std::string model = "TEST-CAM";
    std::string name = "Test Standard";
    std::string copyright = "Copyright test";
};

std::vector<std::byte> build(const Spec& s) {
    TiffBuilder b(s.big_endian, s.magic);
    if (s.magic == 42) {
        b.bytes(50706, TiffType::byte, {1, 7, 1, 0});
    }
    b.ascii(50708, s.model);
    if (s.color_matrices) {
        b.srationals(50721, over_10000(color_a));
        if (s.second_calibration) {
            b.srationals(50722, over_10000(color_d65));
        }
    }
    if (s.dng_extras) {
        b.srationals(50723, over_10000(calibration))
            .srationals(50724, over_10000(calibration))
            .rationals(50727, {{10000, 10000}, {10250, 10000}, {9875, 10000}});
    }
    b.shorts(50778, {17});
    if (s.second_calibration) {
        b.shorts(50779, {21});
    }
    b.ascii(50936, s.name);
    if (s.tables) {
        b.longs(50937, {4, 2, 1})
            .floats(50938, std::vector<float>(4 * 2 * 1 * 3, 0.5f))
            .floats(50939, std::vector<float>(4 * 2 * 1 * 3, 0.5f))
            .floats(50940, {0.0f, 0.0f, 0.5f, 0.6f, 1.0f, 1.0f});
    }
    if (s.embed_policy) {
        b.longs(50941, {*s.embed_policy});
    }
    b.ascii(50942, s.copyright);
    if (s.forward_matrices) {
        b.srationals(50964, over_10000(forward_a));
        if (s.second_calibration) {
            b.srationals(50965, over_10000(forward_d65));
        }
    }
    if (s.tables) {
        b.longs(50981, {6, 2, 3}).floats(50982, std::vector<float>(6 * 2 * 3 * 3, 0.5f));
    }
    if (s.third_calibration) {
        b.shorts(52529, {23}).srationals(52531, over_10000(color_a));
    }
    return b.build();
}

// Writes the profile to a temp file and returns its path.
fs::path write_profile(const Spec& s, const std::string& name = "profile.dcp") {
    const fs::path path = temp_file(name);
    write_file(path, build(s));
    return path;
}

// The matrix the library computes for the same file, as load_matrix would hold it.
prism::Matrix3 library_matrix(const Spec& s, double kelvin) {
    return prism::to_matrix3(
        prism::camera_to_srgb_from_dng(prism::parse_dng_profile(build(s)).calibrations, kelvin));
}

// Printed with 9 decimals and read back as float, each value is within an ulp or so of the
// float of the exact result.
void expect_matrix_eq(const prism::Matrix3& actual, const prism::Matrix3& expected) {
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_FLOAT_EQ(actual[i], expected[i]) << "entry " << i;
    }
}

const std::string raw_use_sentence =
    "The DNG specification says profiles with ProfileEmbedPolicy 0 or 1, or 2 when embedded in a "
    "DNG, may not be used to process non-DNG files; using this matrix for non-DNG (RAW) files may "
    "not be permitted, and that is your responsibility.";
const std::string no_embedding_sentence =
    "The DNG specification says this profile may not be embedded in files.";

}  // namespace

// --- Matrices ------------------------------------------------------------------------------------

TEST(PrismProfile, DngAndDcpInBothByteOrdersGiveTheLibraryMatrix) {
    for (const bool big : {false, true}) {
        for (const bool dng : {false, true}) {
            SCOPED_TRACE(std::string(big ? "MM" : "II") + (dng ? " DNG" : " DCP"));
            Spec s;
            s.big_endian = big;
            s.magic = dng ? 42 : dcp_magic;
            s.tables = true;
            s.dng_extras = dng;
            const fs::path input = write_profile(s, dng ? "in.dng" : "in.dcp");
            const fs::path output = temp_file("matrix.txt");

            const RunResult r = run_profile(quoted(input) + " -o " + quoted(output));
            ASSERT_EQ(r.exit_code, 0) << r.err;
            expect_matrix_eq(prism::load_matrix(output),
                             library_matrix(s, prism::dng_default_kelvin));

            const std::string text = read_file(output);
            EXPECT_TRUE(contains(text, "#   file: " + input.filename().string() + "\n")) << text;
            EXPECT_FALSE(contains(text, input.parent_path().string())) << "no directory: " << text;
            EXPECT_TRUE(contains(text, dng ? "#   format: DNG\n"
                                           : "#   format: DCP (camera "
                                             "profile)\n"))
                << text;
            EXPECT_TRUE(contains(text, "#   camera model: TEST-CAM\n")) << text;
            EXPECT_TRUE(contains(text, "#   profile name: Test Standard\n")) << text;
            // 6504 K is calibration 2's own illuminant, so that calibration is used alone.
            EXPECT_TRUE(contains(text,
                                 "# Method: ForwardMatrix2 alone, for a white balance of "
                                 "6504 K (the default, D65)"))
                << text;
            EXPECT_TRUE(contains(text, "This is a colorimetric matrix")) << text;
            EXPECT_TRUE(contains(text,
                                 "ProfileHueSatMap (4x2x1, 2 tables), ProfileLookTable "
                                 "(6x2x3), ProfileToneCurve (3 points)."))
                << text;
            EXPECT_EQ(contains(text,
                               "# Ignored: CameraCalibration1, CameraCalibration2, "
                               "AnalogBalance (in the file, but not applied).\n"),
                      dng)
                << text;
            EXPECT_FALSE(contains(text, "third calibration")) << text;

            // Without -o, the same text goes to stdout.
            const RunResult to_stdout = run_profile(quoted(input));
            EXPECT_EQ(to_stdout.exit_code, 0) << to_stdout.err;
            EXPECT_EQ(to_stdout.out, text);
            fs::remove(input);
            fs::remove(output);
        }
    }
}

TEST(PrismProfile, ColorMatrixOnlyAndSingleCalibrationProfilesConvert) {
    Spec color_only;
    color_only.forward_matrices = false;
    Spec single;
    single.second_calibration = false;
    single.color_matrices = false;
    for (const auto& [s, method] :
         {std::pair{color_only, "# Method: ColorMatrix2 alone"},
          std::pair{single,
                    "# Method: ForwardMatrix1 of the profile's single calibration "
                    "(illuminant 17: standard light A, 2856 K)"}}) {
        const fs::path input = write_profile(s);
        const fs::path output = temp_file("matrix.txt");
        const RunResult r = run_profile(quoted(input) + " -o " + quoted(output));
        ASSERT_EQ(r.exit_code, 0) << r.err;
        expect_matrix_eq(prism::load_matrix(output), library_matrix(s, prism::dng_default_kelvin));
        EXPECT_TRUE(contains(read_file(output), method)) << read_file(output);
        fs::remove(input);
        fs::remove(output);
    }
}

TEST(PrismProfile, TemperatureInterpolatesTwoForwardMatrices) {
    const Spec s;
    const fs::path input = write_profile(s);
    const fs::path output = temp_file("matrix.txt");

    RunResult r = run_profile(quoted(input) + " -o " + quoted(output) + " --temperature 4000");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    const prism::Matrix3 at_4000 = prism::load_matrix(output);
    expect_matrix_eq(at_4000, library_matrix(s, 4000));
    std::string text = read_file(output);
    EXPECT_TRUE(contains(text,
                         "# Method: ForwardMatrix1 and ForwardMatrix2 interpolated at 4000 K "
                         "(--temperature),\n"))
        << text;
    // (1/4000 - 1/6504) / (1/2856 - 1/6504)
    EXPECT_TRUE(contains(
        text, "#   weight 0.4901 on calibration 1 (illuminant 17: standard light A, 2856 K),\n"))
        << text;

    r = run_profile(quoted(input) + " -o " + quoted(output));
    ASSERT_EQ(r.exit_code, 0) << r.err;
    const prism::Matrix3 at_default = prism::load_matrix(output);
    double largest_change = 0;
    for (std::size_t i = 0; i < at_default.size(); ++i) {
        largest_change = std::max(largest_change, double(std::abs(at_4000[i] - at_default[i])));
    }
    EXPECT_GT(largest_change, 1e-3) << "--temperature 4000 should change the matrix";

    // Below both illuminants: calibration 1 alone, and the header says so.
    r = run_profile(quoted(input) + " -o " + quoted(output) + " --temperature 2000");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    expect_matrix_eq(prism::load_matrix(output), library_matrix(s, 2000));
    text = read_file(output);
    EXPECT_TRUE(contains(text,
                         "# Method: ForwardMatrix1 alone, for a white balance of 2000 K "
                         "(--temperature)"))
        << text;
    fs::remove(input);
    fs::remove(output);
}

TEST(PrismProfile, TemperatureIsRefusedUnlessForwardMatricesAreInterpolated) {
    Spec color_only;
    color_only.forward_matrices = false;
    Spec single;
    single.second_calibration = false;
    const fs::path color_input = write_profile(color_only, "color.dcp");
    const fs::path single_input = write_profile(single, "single.dcp");
    const fs::path output = temp_file("matrix.txt");

    RunResult r =
        run_profile(quoted(color_input) + " -o " + quoted(output) + " --temperature 4000");
    EXPECT_EQ(r.exit_code, 2) << r.err;
    EXPECT_TRUE(contains(r.err,
                         "has only ColorMatrices, and prism_profile white-balances those "
                         "for D65 only"))
        << r.err;
    EXPECT_FALSE(fs::exists(output)) << "nothing may be written";

    r = run_profile(quoted(single_input) + " -o " + quoted(output) + " --temperature 5000");
    EXPECT_EQ(r.exit_code, 2) << r.err;
    EXPECT_TRUE(contains(r.err, "has only one calibration, which is used at every temperature"))
        << r.err;
    EXPECT_FALSE(fs::exists(output));

    // The default value changes nothing, so it is accepted.
    for (const fs::path& input : {color_input, single_input}) {
        r = run_profile(quoted(input) + " -o " + quoted(output) + " --temperature 6504");
        EXPECT_EQ(r.exit_code, 0) << r.err;
        fs::remove(output);
    }

    // A LightCraft profile has no temperature at all.
    const fs::path json = temp_file("lc.json");
    std::ofstream(json) << R"({"version": 1, "model": "TEST-CAM", "files": 1, "samples": 1,)"
                        << R"( "matrix": [[1, 0, 0], [0, 1, 0], [0, 0, 1]]})";
    r = run_profile(quoted(json) + " --temperature 6504");
    EXPECT_EQ(r.exit_code, 2) << r.err;
    EXPECT_TRUE(contains(r.err, "--temperature only applies to DNG and DCP profiles")) << r.err;
    EXPECT_TRUE(r.out.empty()) << r.out;

    for (const fs::path& p : {color_input, single_input, json}) {
        fs::remove(p);
    }
}

TEST(PrismProfile, BadTemperatureIsUsageError) {
    const fs::path input = write_profile(Spec{});
    for (const char* bad : {"0", "-1", "-0", "nan", "inf", "-inf", "abc", "", "5000K", "1e999"}) {
        const RunResult r = run_profile(quoted(input) + " --temperature \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--temperature '" << bad << "': " << r.err;
        EXPECT_TRUE(contains(r.err, "--temperature needs a positive number of kelvin")) << r.err;
        EXPECT_TRUE(contains(r.err, "Usage:")) << r.err;
        EXPECT_TRUE(r.out.empty());
    }
    const RunResult r = run_profile(quoted(input) + " --temperature 5000 --temperature 5000");
    EXPECT_EQ(r.exit_code, 2) << r.err;
    EXPECT_TRUE(contains(r.err, "--temperature given more than once")) << r.err;
    fs::remove(input);
}

TEST(PrismProfile, UnconvertibleProfilesFailWithTheLibrarysReason) {
    // Kept as errors: no rule exists to interpolate these (see camera_to_srgb_from_dng).
    std::vector<std::byte> mixed_file = [&] {
        TiffBuilder b(false, dcp_magic);
        b.srationals(50721, over_10000(color_a))
            .srationals(50722, over_10000(color_d65))
            .shorts(50778, {17})
            .shorts(50779, {21})
            .srationals(50965, over_10000(forward_d65));
        return b.build();
    }();
    std::vector<std::byte> same_temperature = [&] {
        TiffBuilder b(false, dcp_magic);
        b.srationals(50721, over_10000(color_a))
            .srationals(50722, over_10000(color_d65))
            .shorts(50778, {1})   // daylight, 5500 K
            .shorts(50779, {4});  // flash, 5500 K
        return b.build();
    }();
    const fs::path input = temp_file("bad.dcp");
    write_file(input, mixed_file);
    RunResult r = run_profile(quoted(input));
    EXPECT_EQ(r.exit_code, 1) << r.err;
    EXPECT_TRUE(contains(r.err, "ForwardMatrix for only one of its two calibrations")) << r.err;

    write_file(input, same_temperature);
    r = run_profile(quoted(input));
    EXPECT_EQ(r.exit_code, 1) << r.err;
    EXPECT_TRUE(contains(r.err, "both 5500 K")) << r.err;
    EXPECT_TRUE(r.out.empty()) << r.out;
    fs::remove(input);
}

TEST(PrismProfile, ReportsAnIgnoredThirdCalibration) {
    Spec s;
    s.third_calibration = true;
    const fs::path input = write_profile(s);
    const RunResult r = run_profile(quoted(input));
    EXPECT_EQ(r.exit_code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "# Ignored: the file's third calibration")) << r.out;
    EXPECT_TRUE(contains(r.out, "# Skipped: nothing; the profile has no HueSatMap")) << r.out;
    fs::remove(input);
}

// --- Text from the file --------------------------------------------------------------------------

TEST(PrismProfile, TextFromTheFileCannotAddMatrixRows) {
    Spec s;
    s.name = "x\n9 9 9";
    s.model = "\r\n1 2 3\t#";
    s.copyright = "\x1b[31m\xce\xb1 (c)\x7f";
    const fs::path input = write_profile(s);
    const fs::path output = temp_file("matrix.txt");
    const RunResult r = run_profile(quoted(input) + " -o " + quoted(output) + " --note \"a\nb\"");
    ASSERT_EQ(r.exit_code, 0) << r.err;

    // load_matrix would throw on 12 or 15 numbers; it must read exactly the library's 9.
    expect_matrix_eq(prism::load_matrix(output), library_matrix(s, prism::dng_default_kelvin));
    const std::string text = read_file(output);
    EXPECT_TRUE(contains(text, "#   profile name: x 9 9 9\n")) << text;
    EXPECT_TRUE(contains(text, "#   camera model:   1 2 3 #\n")) << text;
    // ESC and DEL become spaces; each byte of the UTF-8 alpha becomes '?'.
    EXPECT_TRUE(contains(text, "#   profile copyright:  [31m?? (c) \n")) << text;
    EXPECT_TRUE(contains(text, "#   a b\n")) << text;

    // Every line is a comment, blank, or one of the 3 matrix rows; all of it is printable ASCII.
    std::istringstream lines(text);
    int rows = 0;
    for (std::string line; std::getline(lines, line);) {
        rows += (!line.empty() && line[0] != '#') ? 1 : 0;
        for (const char c : line) {
            const auto u = static_cast<unsigned char>(c);
            EXPECT_TRUE(u >= 0x20 && u < 0x7f) << "byte " << int(u) << " in: " << line;
        }
    }
    EXPECT_EQ(rows, 3) << text;
    // The summary on stdout is sanitised too (a terminal would act on the escape sequence).
    EXPECT_FALSE(contains(r.out, "\x1b")) << r.out;
    EXPECT_FALSE(contains(r.out, "\r")) << r.out;
    fs::remove(input);
    fs::remove(output);
}

TEST(PrismProfile, LongTextIsCutAt120Characters) {
    Spec s;
    s.name = std::string(200, 'n');
    s.model = std::string(120, 'm');  // exactly the limit: kept whole
    const fs::path input = write_profile(s);
    const RunResult r = run_profile(quoted(input));
    ASSERT_EQ(r.exit_code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "#   profile name: " + std::string(117, 'n') + "...\n")) << r.out;
    EXPECT_TRUE(contains(r.out, "#   camera model: " + std::string(120, 'm') + "\n")) << r.out;
    fs::remove(input);
}

// --- ProfileEmbedPolicy --------------------------------------------------------------------------

TEST(PrismProfile, ReportsTheEmbedPolicyAsTheSpecificationWordsIt) {
    // DNG 1.7.1.0, p. 52: 0 and 1 may not process non-DNG files; 2 is a stored profile that may
    // not be embedded in files, but once embedded in a DNG it has the restrictions of 1. Without
    // the tag, its default (0) applies.
    struct Case {
        std::optional<std::uint32_t> policy;
        bool dng;
        std::string value;   // the header's "ProfileEmbedPolicy: " line starts with this
        std::string notice;  // empty: none
        std::string level;   // "warning: " or "note: "
    };
    const std::vector<Case> cases = {
        {0, false, "0 (allow copying", raw_use_sentence, "warning: "},
        {0, true, "0 (allow copying", raw_use_sentence, "warning: "},
        {1, false, "1 (embed if used", raw_use_sentence, "warning: "},
        {1, true, "1 (embed if used", raw_use_sentence, "warning: "},
        {2, false, "2 (embed never: a stored profile", no_embedding_sentence, "note: "},
        {2, true, "2 (embed never; embedded in this DNG, so it has the restrictions of 1",
         raw_use_sentence, "warning: "},
        {3, false, "3 (no restrictions)", "", ""},
        {3, true, "3 (no restrictions)", "", ""},
        {4, false, "4 (not a value the DNG specification defines)",
         "ProfileEmbedPolicy 4 is not defined by the DNG specification, so the profile's terms are "
         "unknown.",
         "warning: "},
        {4294967295, true, "4294967295 (not a value the DNG specification defines)",
         "ProfileEmbedPolicy 4294967295 is not defined by the DNG specification, so the profile's "
         "terms are unknown.",
         "warning: "},
        // The tag's default is 0.
        {std::nullopt, false, "not in the file, so the specification's default applies: 0 (allow",
         raw_use_sentence, "warning: "},
        {std::nullopt, true, "not in the file, so the specification's default applies: 0 (allow",
         raw_use_sentence, "warning: "},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE("ProfileEmbedPolicy " + (c.policy ? std::to_string(*c.policy) : "absent") +
                     (c.dng ? " in a DNG" : " in a DCP"));
        Spec s;
        s.embed_policy = c.policy;
        s.magic = c.dng ? 42 : dcp_magic;
        const fs::path input = write_profile(s, c.dng ? "in.dng" : "in.dcp");
        const RunResult r = run_profile(quoted(input));
        ASSERT_EQ(r.exit_code, 0) << r.err;
        EXPECT_TRUE(contains(r.out, "# ProfileEmbedPolicy: " + c.value)) << r.out;
        if (c.notice.empty()) {
            EXPECT_TRUE(r.err.empty()) << r.err;
        } else {
            // The same sentence on stderr and in the header.
            EXPECT_TRUE(contains(
                r.err, c.level + input.filename().string() + " has ProfileEmbedPolicy " + c.value))
                << r.err;
            EXPECT_TRUE(contains(r.err, c.notice)) << r.err;
            EXPECT_TRUE(contains(r.out, "\n# " + c.notice + "\n")) << r.out;
        }
        // Only 0 and 1, or 2 in a DNG, say anything about non-DNG (RAW) files.
        const bool raw = c.notice == raw_use_sentence;
        EXPECT_EQ(contains(r.err, "RAW"), raw) << r.err;
        EXPECT_EQ(contains(r.out, "RAW"), raw) << r.out;
        EXPECT_FALSE(contains(r.out, "warning") || contains(r.out, "note:")) << "stderr only";
        fs::remove(input);
    }
}

// --- Input formats -------------------------------------------------------------------------------

TEST(PrismProfile, DetectsTheFormatByContentNotName) {
    const std::string json = R"({"version": 1, "model": "TEST-CAM", "files": 1, "samples": 1,)"
                             R"( "matrix": [[1, 0, 0], [0, 1, 0], [0, 0, 1]]})";
    const fs::path json_named_dcp = temp_file("lc.dcp");
    std::ofstream(json_named_dcp) << "\n  \t" << json;
    RunResult r = run_profile(quoted(json_named_dcp));
    EXPECT_EQ(r.exit_code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "from the LightCraft camera profile")) << r.out;

    const fs::path dcp_named_json = write_profile(Spec{}, "profile.json");
    r = run_profile(quoted(dcp_named_json));
    EXPECT_EQ(r.exit_code, 0) << r.err;
    EXPECT_TRUE(contains(r.out, "#   format: DCP (camera profile)\n")) << r.out;

    const fs::path other = temp_file("other.dcp");
    const std::string both_formats =
        "it accepts a LightCraft camera profile (JSON) or a DNG or DCP file (TIFF)";
    for (const std::string& content :
         {std::string(""), std::string("\x89PNG\r\n\x1a\n", 8), std::string("II*"),
          std::string("II+\0\x08\0\0\0", 8),  // BigTIFF: magic 43
          std::string("MM\0\x2b", 4), std::string("RC II"), std::string("   \n"),
          std::string("[1, 2, 3]"), std::string("not a profile")}) {
        std::ofstream(other, std::ios::binary) << content;
        r = run_profile(quoted(other));
        EXPECT_EQ(r.exit_code, 1) << r.err;
        EXPECT_TRUE(contains(r.err, both_formats)) << "content '" << content << "': " << r.err;
        EXPECT_TRUE(r.out.empty()) << r.out;
    }

    r = run_profile(quoted(temp_file("missing.dcp")));
    EXPECT_EQ(r.exit_code, 1) << r.err;
    EXPECT_TRUE(contains(r.err, "cannot open profile")) << r.err;

    // A TIFF header with nothing usable behind it is a TIFF, so the TIFF reader explains.
    std::ofstream(other, std::ios::binary) << std::string("IIRC\x08\0\0\0", 8);
    r = run_profile(quoted(other));
    EXPECT_EQ(r.exit_code, 1) << r.err;
    EXPECT_TRUE(contains(r.err, "TIFF: ")) << r.err;

    for (const fs::path& p : {json_named_dcp, dcp_named_json, other}) {
        fs::remove(p);
    }
}

TEST(PrismProfile, LightCraftOutputIsUnchanged) {
    // Exactly what prism_profile wrote before it read DNG and DCP files (commit 9b25b50) for the
    // input lc.json.
    std::string expected =
        R"(# White-balanced camera RGB -> linear sRGB (D65), for prism --raw-camera-matrix.
# Generated by prism_profile <version> from the LightCraft camera profile
#   lc.json
#   model TEST-CAM, profile version 1, fitted on 12 photos (3456 colour pairs)
#   from a test
#
# LightCraft's matrix maps white-balanced camera RGB -> linear Rec. 2020:
#     1.200000000  -0.300000000   0.100000000
#    -0.100000000   1.200000000  -0.100000000
#     0.000000000  -0.800000000   1.800000000
# Converted as normalize_rows(XYZ->sRGB * Rec2020->XYZ * matrix); both spaces are D65,
# so no chromatic adaptation. Row sums before normalising: 1.0000 1.0000 1.0000
#
# The profile was fitted to the camera's own JPEGs, so this is a camera-JPEG look
# (more saturated, like the in-camera style), not a colorimetric calibration.
# Skipped: the profile's hue/saturation/value table (72x5x5), which a 3x3 matrix cannot represent.

  2.051353316  -1.145036777   0.093683460
 -0.262750559   1.403524557  -0.140773998
 -0.011723026  -1.010233178   2.021956204
)";
    expected.replace(expected.find("<version>"), 9, prism::version());
    const fs::path json = temp_file("lc.json");
    const fs::path output = temp_file("matrix.txt");
    std::ofstream(json) << R"({"version": 1, "model": "TEST-CAM", "files": 12, "samples": 3456, )"
                        << R"("matrix": [[1.2, -0.3, 0.1], [-0.1, 1.2, -0.1], [0.0, -0.8, 1.8]], )"
                        << R"("hue_sat": {"hue_divisions": 72, "sat_divisions": 5, )"
                        << R"("val_divisions": 5, "data": []}})";
    // Only the file name is shown, so naming the temp file "lc.json" gives exactly the old text.
    const auto with_short_path = [&](std::string text) {
        const std::string line = "\n#   " + json.filename().string() + "\n";
        const std::size_t at = text.find(line);
        if (at != std::string::npos) {
            text.replace(at, line.size(), "\n#   lc.json\n");
        }
        return text;
    };

    RunResult r = run_profile(quoted(json) + " --note \"from a test\"");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    EXPECT_EQ(with_short_path(r.out), expected);
    EXPECT_TRUE(r.err.empty()) << r.err;

    r = run_profile(quoted(json) + " -o " + quoted(output) + " --note \"from a test\"");
    ASSERT_EQ(r.exit_code, 0) << r.err;
    EXPECT_EQ(with_short_path(read_file(output)), expected);
    EXPECT_EQ(r.out, "wrote " + output.string() +
                         " (TEST-CAM, camera RGB -> linear sRGB):\n"
                         "    2.051353316  -1.145036777   0.093683460\n"
                         "   -0.262750559   1.403524557  -0.140773998\n"
                         "   -0.011723026  -1.010233178   2.021956204\n"
                         "skipped the hue/saturation/value table (not representable as a 3x3 "
                         "matrix)\n");
    fs::remove(json);
    fs::remove(output);
}

TEST(PrismProfile, LightCraftTextCannotAddMatrixRows) {
    // The same cleaning as for DNG and DCP files: the model (from the file) and the notes.
    const fs::path json = temp_file("evil.json");
    const fs::path output = temp_file("matrix.txt");
    const std::string matrix = "[[1.2, -0.3, 0.1], [-0.1, 1.2, -0.1], [0.0, -0.8, 1.8]]";
    std::ofstream(json) << R"({"version": 1, "model": "A\n9 9 9\r\u001b[2J\u00e9", "files": 1, )"
                        << R"("samples": 1, "matrix": )" << matrix << "}";
    const RunResult r =
        run_profile(quoted(json) + " -o " + quoted(output) + " --note \"b\n8 8 8\"");
    ASSERT_EQ(r.exit_code, 0) << r.err;

    // load_matrix would throw on 12 or 15 numbers; it must read exactly the expected 9.
    const prism::Matrix3 expected = prism::to_matrix3(prism::camera_to_srgb_from_rec2020(
        prism::parse_lightcraft_profile(R"({"version": 1, "model": "X", "files": 1, )"
                                        R"("samples": 1, "matrix": )" +
                                        matrix + "}")
            .matrix));
    expect_matrix_eq(prism::load_matrix(output), expected);
    const std::string text = read_file(output);
    EXPECT_TRUE(contains(text, "#   model A 9 9 9  [2J??, profile version 1")) << text;
    EXPECT_TRUE(contains(text, "#   b 8 8 8\n")) << text;
    EXPECT_TRUE(contains(text, "#   " + json.filename().string() + "\n")) << text;
    EXPECT_FALSE(contains(text, json.parent_path().string())) << "no directory: " << text;
    EXPECT_TRUE(contains(r.out, "wrote " + output.string() + " (A 9 9 9  [2J??, camera RGB"))
        << r.out;
    EXPECT_FALSE(contains(r.out, "\x1b")) << r.out;
    fs::remove(json);
    fs::remove(output);
}

// --- Real files (opt-in) -------------------------------------------------------------------------
//
// With PRISM_TEST_DCP or PRISM_TEST_DNG set (see dng_profile_test.cpp), runs prism_profile on that
// file and checks that the output loads and white stays white. Neither the matrix nor the tool's
// stdout is printed, even on failure; stderr holds only errors and the policy warning.

namespace {

void check_real_file(const char* variable) {
    const char* path = std::getenv(variable);
    if (path == nullptr || *path == '\0') {
        GTEST_SKIP() << "set " << variable << " to a file to run this test";
    }
    const fs::path output = temp_file("real.txt");
    const RunResult r = run_profile(quoted(path) + " -o " + quoted(output));
    ASSERT_EQ(r.exit_code, 0) << r.err;
    prism::Matrix3 m{};
    try {
        m = prism::load_matrix(output);
    } catch (const std::exception& e) {
        fs::remove(output);
        FAIL() << "the output does not load: " << e.what();
    }
    fs::remove(output);
    for (int row = 0; row < 3; ++row) {
        const float sum = m[row * 3] + m[row * 3 + 1] + m[row * 3 + 2];
        EXPECT_TRUE(std::abs(sum - 1.0f) < 1e-5f) << "row " << row << " does not sum to 1";
    }
}

}  // namespace

TEST(PrismProfile, RealFileDcpGivesALoadableMatrix) {
    check_real_file("PRISM_TEST_DCP");
}

TEST(PrismProfile, RealFileDngGivesALoadableMatrix) {
    check_real_file("PRISM_TEST_DNG");
}
