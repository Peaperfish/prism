#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "env_var.hpp"
#include "heic_damage.hpp"
#include "prism/heic.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"
#include "prism/raw.hpp"
#include "prism/resize.hpp"
#include "prism/version.hpp"
#include "run_command.hpp"
#include "synthetic_exif.hpp"

// End-to-end tests: run the real prism executable (path supplied by CMake as PRISM_EXE)
// and check its exit code and output.

namespace fs = std::filesystem;

namespace {

// A temporary file path that no other test can be using at the same time. gtest_discover_tests
// makes each TEST its own ctest entry (its own process), and `ctest -j` runs them side by side in
// one shared temp directory, so a fixed name like "prism_cli_output.txt" would be overwritten and
// deleted by other tests mid-run. The test's name keeps tests apart; the process id keeps apart
// two runs of the same test, e.g. from two build directories at once.
fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_cli_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

struct RunResult {
    int exit_code;
    std::string output;  // stdout and stderr together
};

RunResult run_prism(const std::string& args) {
    const fs::path log = temp_file("output.txt");
    const std::string command =
        "\"" + std::string(PRISM_EXE) + "\" " + args + " > \"" + log.string() + "\" 2>&1";
    const int exit_code = prism_test::run_command(command);

    std::ifstream file(log);
    std::stringstream text;
    text << file.rdbuf();
    file.close();
    fs::remove(log);
    return {exit_code, text.str()};
}

std::string quoted(const fs::path& path) {
    return "\"" + path.string() + "\"";
}

// A small image where every channel is linear 0.18 (photographic middle grey, sRGB 118):
// nothing is clipped until the pipeline pushes it there.
fs::path write_grey_input() {
    prism::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(4 * 4 * 3, 0.18f);
    const fs::path path = temp_file("grey.png");
    prism::save_image(path, img);
    return path;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Cli, VersionPrintsTheProjectVersion) {
    const RunResult r = run_prism("--version");

    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_EQ(r.output, "prism " + std::string(prism::version()) + "\n");
}

TEST(Cli, NoOutputAndNoHistogramIsUsageError) {
    const fs::path input = write_grey_input();
    const RunResult r = run_prism(quoted(input) + " --exposure 1");
    fs::remove(input);

    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "no output given")) << r.output;
    EXPECT_TRUE(contains(r.output, "Usage:")) << r.output;
}

TEST(Cli, HistogramWithoutOutputReflectsProcessing) {
    const fs::path input = write_grey_input();
    const fs::path hist = temp_file("hist.png");
    const fs::path zero_matrix = temp_file("zero_matrix.txt");
    std::ofstream(zero_matrix) << "0 0 0\n0 0 0\n0 0 0\n";
    const std::string base = quoted(input) + " --histogram " + quoted(hist);

    // Unprocessed: mid-grey, nothing clipped.
    RunResult r = run_prism(base);
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(hist));
    EXPECT_TRUE(contains(r.output, "R 0.00% / 0.00%  G 0.00% / 0.00%  B 0.00% / 0.00%"))
        << r.output;
    EXPECT_FALSE(contains(r.output, "(4x4)")) << "no image should be written without -o";

    // +3 stops: 0.18 * 8 = 1.44, so every pixel clips to 255.
    r = run_prism(base + " --exposure +3");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "R 0.00% / 100.00%  G 0.00% / 100.00%  B 0.00% / 100.00%"))
        << r.output;

    // ...and a curve after it pulls 1.44 back below 1 (reinhard: 0.643), so nothing clips.
    r = run_prism(base + " --exposure +3 --curve reinhard");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "R 0.00% / 0.00%  G 0.00% / 0.00%  B 0.00% / 0.00%"))
        << r.output;

    // An all-zero matrix sends every pixel to black.
    r = run_prism(base + " --matrix " + quoted(zero_matrix));
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "R 100.00% / 0.00%  G 100.00% / 0.00%  B 100.00% / 0.00%"))
        << r.output;

    fs::remove(input);
    fs::remove(hist);
    fs::remove(zero_matrix);
}

TEST(Cli, OutputAndHistogramTogetherMatchRunningThemSeparately) {
    // With both options the image is encoded once and the bytes serve both; that must not change
    // either result.
    const fs::path input = write_grey_input();
    const fs::path both_out = temp_file("both.png");
    const fs::path both_hist = temp_file("both_hist.png");
    const fs::path only_out = temp_file("only.png");
    const fs::path only_hist = temp_file("only_hist.png");
    const std::string pipeline = " --exposure +2.5 --curve filmic";

    const RunResult both = run_prism(quoted(input) + pipeline + " -o " + quoted(both_out) +
                                     " --histogram " + quoted(both_hist));
    const RunResult out = run_prism(quoted(input) + pipeline + " -o " + quoted(only_out));
    const RunResult hist =
        run_prism(quoted(input) + pipeline + " --histogram " + quoted(only_hist));
    ASSERT_EQ(both.exit_code, 0) << both.output;

    const auto bytes_of = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    EXPECT_EQ(bytes_of(both_out), bytes_of(only_out));
    EXPECT_EQ(bytes_of(both_hist), bytes_of(only_hist));
    const auto clipping_line = [](const std::string& text) {
        const std::size_t at = text.find("clipped");
        return text.substr(at, text.find('\n', at) - at);
    };
    EXPECT_EQ(clipping_line(both.output), clipping_line(hist.output));

    for (const fs::path& p : {input, both_out, both_hist, only_out, only_hist}) {
        fs::remove(p);
    }
}

TEST(Cli, BadThreadCountIsUsageError) {
    const fs::path input = write_grey_input();
    const fs::path hist = temp_file("hist_threads.png");
    for (const char* bad : {"0", "-2", "abc", "4x", "", "99999"}) {
        const RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) +
                                      " --threads \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--threads '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "--threads needs a whole number")) << r.output;
    }
    fs::remove(input);
    fs::remove(hist);
}

TEST(Cli, TimePrintsEachStageThatRan) {
    const fs::path input = write_grey_input();
    const fs::path output = temp_file("timed.png");

    RunResult r = run_prism(quoted(input) + " -o " + quoted(output) +
                            " --exposure 1 --curve filmic --threads 4 --time");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    // A 4x4 image is far below the ~1 MP threshold, so it runs on one thread whatever was asked.
    EXPECT_TRUE(contains(r.output, "time (1 thread):")) << r.output;
    for (const char* stage : {"load", "exposure", "curve", "save", "total"}) {
        EXPECT_TRUE(contains(r.output, std::string("  ") + stage + " ")) << stage << r.output;
    }
    EXPECT_FALSE(contains(r.output, "matrix")) << "no --matrix was given: " << r.output;

    // A stage given its neutral value is skipped, so it is not listed either.
    r = run_prism(quoted(input) + " -o " + quoted(output) + " --exposure 0 --saturation 1 --time");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_FALSE(contains(r.output, "exposure")) << r.output;
    EXPECT_FALSE(contains(r.output, "saturation")) << r.output;

    // Without --time, no timings.
    r = run_prism(quoted(input) + " -o " + quoted(output) + " --threads 1");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_FALSE(contains(r.output, "time (")) << r.output;

    fs::remove(input);
    fs::remove(output);
}

TEST(Cli, RawFlagsOnNonRawInputAreUsageErrors) {
    const fs::path input = write_grey_input();
    const fs::path hist = temp_file("hist_raw_flags.png");
    const fs::path matrix = temp_file("identity.txt");
    std::ofstream(matrix) << "1 0 0\n0 1 0\n0 0 1\n";

    for (const std::string& flags :
         {std::string(" --raw-half"), " --raw-camera-matrix " + quoted(matrix)}) {
        const RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) + flags);
        EXPECT_EQ(r.exit_code, 2) << flags << ": " << r.output;
        EXPECT_TRUE(contains(r.output, "only applies to RAW input")) << r.output;
    }
    fs::remove(input);
    fs::remove(hist);
    fs::remove(matrix);
}

TEST(Cli, MissingRawFileIsReportedAsRawError) {
    const fs::path hist = temp_file("hist_missing_raw.png");
    const RunResult r =
        run_prism(quoted(temp_file("missing.ARW")) + " --histogram " + quoted(hist));
    EXPECT_EQ(r.exit_code, 1) << r.output;
    EXPECT_TRUE(contains(r.output, "cannot load RAW file")) << r.output;
}

// A JPEG whose EXIF says "turned 90 degrees": the render comes out upright, so 32 x 64.
TEST(Cli, AJpegWithOrientation6RendersUpright) {
    const fs::path input = temp_file("sideways.jpg");
    const fs::path output = temp_file("upright.png");
    prism_test::write_oriented_jpeg(input, 6);

    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output));
    EXPECT_EQ(r.exit_code, 0) << r.output;
    ASSERT_TRUE(fs::exists(output));
    const prism::Image out = prism::load_image(output);
    EXPECT_EQ(out.width, prism_test::quadrant_height);
    EXPECT_EQ(out.height, prism_test::quadrant_width);
    fs::remove(input);
    fs::remove(output);
}

// No skip: on macOS the HEIC photo is rendered upright, elsewhere the tool says it needs macOS.
TEST(Cli, AHeicWithOrientation6RendersUprightOrSaysItNeedsMacOS) {
    const fs::path input = fs::path(PRISM_TEST_DATA) / "heic" / "quadrants_o6.heic";
    const fs::path output = temp_file("heic_upright.png");
    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output));
    if (prism::heic_support_available()) {
        EXPECT_EQ(r.exit_code, 0) << r.output;
        ASSERT_TRUE(fs::exists(output));
        const prism::Image out = prism::load_image(output);
        EXPECT_EQ(out.width, prism_test::quadrant_height);
        EXPECT_EQ(out.height, prism_test::quadrant_width);
    } else {
        EXPECT_NE(r.exit_code, 0);
        EXPECT_TRUE(contains(r.output, "macOS")) << r.output;
        EXPECT_FALSE(fs::exists(output));
    }
    std::error_code ec;
    fs::remove(output, ec);
}

// A truncated or scrambled HEIC makes the tool fail, not write a black picture.
TEST(Cli, DamagedCopiesOfAHeicFailWithoutWritingAnOutput) {
    if (!prism::heic_support_available()) {
        GTEST_SKIP() << "HEIC photos open on macOS only";
    }
    const fs::path dir = temp_file("damaged");
    fs::create_directories(dir);
    const fs::path source = fs::path(PRISM_TEST_DATA) / "heic" / "quadrants_o1.heic";
    const fs::path output = temp_file("damaged.png");
    for (const fs::path& path : prism_test::write_damaged_heic_copies(source, dir)) {
        SCOPED_TRACE(path.filename().string());
        const RunResult r = run_prism(quoted(path) + " -o " + quoted(output));
        EXPECT_NE(r.exit_code, 0) << r.output;
        EXPECT_FALSE(fs::exists(output));
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::remove(output, ec);
}

// Runs only with PRISM_TEST_RAW set; see raw_test.cpp. Half size keeps it quick.
TEST(Cli, RawWithIdentityCameraMatrixRuns) {
    const fs::path raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    const fs::path matrix = temp_file("raw_identity.txt");
    const fs::path output = temp_file("raw.jpg");
    std::ofstream(matrix) << "1 0 0\n0 1 0\n0 0 1\n";

    const RunResult r = run_prism(quoted(raw) + " --raw-half --raw-camera-matrix " +
                                  quoted(matrix) + " --curve filmic -o " + quoted(output));
    // Without LibRaw the run must fail cleanly instead.
    if (contains(r.output, "built without LibRaw")) {
        EXPECT_EQ(r.exit_code, 1) << r.output;
    } else {
        EXPECT_EQ(r.exit_code, 0) << r.output;
        EXPECT_TRUE(fs::exists(output));
    }
    fs::remove(matrix);
    fs::remove(output);
}

TEST(Cli, BadSaturationAndContrastAreUsageErrors) {
    const fs::path input = write_grey_input();
    const fs::path hist = temp_file("hist_sat.png");
    for (const char* bad : {"abc", "", "-0.5", "1.2x", "nan", "inf", "11", "125"}) {
        RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) +
                                " --saturation \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--saturation '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "--saturation needs a number from 0 to 10")) << r.output;
        EXPECT_TRUE(contains(r.output, "Usage:")) << r.output;
    }
    for (const char* bad : {"abc", "0", "-1", "0.05", "1.15.", "20"}) {
        RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) + " --contrast \"" +
                                bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--contrast '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "--contrast needs a number from 0.1 to 10")) << r.output;
    }
    for (const char* twice : {" --saturation 1.2 --saturation 1.3", " --contrast 1 --contrast 2"}) {
        RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) + twice);
        EXPECT_EQ(r.exit_code, 2) << twice << ": " << r.output;
        EXPECT_TRUE(contains(r.output, "given more than once")) << r.output;
    }
    fs::remove(input);
    fs::remove(hist);
}

TEST(Cli, SaturationAndContrastKeepMiddleGrey) {
    // The input is linear 0.18 grey: saturation leaves greys alone and 0.18 is the contrast pivot,
    // so the output must be byte-identical to an unprocessed run. --time shows both stages ran.
    const fs::path input = write_grey_input();
    const fs::path plain = temp_file("plain.png");
    const fs::path graded = temp_file("graded.png");

    RunResult r = run_prism(quoted(input) + " -o " + quoted(plain));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    r = run_prism(quoted(input) + " -o " + quoted(graded) +
                  " --contrast 1.5 --saturation 2 --time");
    ASSERT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "  saturation ")) << r.output;
    EXPECT_TRUE(contains(r.output, "  contrast ")) << r.output;

    const auto bytes_of = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    EXPECT_EQ(bytes_of(plain), bytes_of(graded));
    for (const fs::path& p : {input, plain, graded}) {
        fs::remove(p);
    }
}

// --- --long-side (#52) --------------------------------------------------------------------------

namespace {

// A width x height image of varied, repeatable values, so that reducing and editing it change it.
fs::path write_sized_input(const std::string& name, int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height * 3);
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        img.pixels[i] = static_cast<float>((i * 7) % 23) / 23.0f;
    }
    const fs::path path = temp_file(name);
    prism::save_image(path, img);
    return path;
}

// The output with Windows' "\r\n" turned into "\n".
std::string plain(std::string text) {
    std::string out;
    for (const char c : text) {
        if (c != '\r') {
            out += c;
        }
    }
    return out;
}

}  // namespace

TEST(Cli, LongSideShrinksBeforeTheStagesRun) {
    const fs::path input = write_sized_input("big.png", 40, 30);
    const fs::path output = temp_file("small.png");
    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output) +
                                  " --long-side 20 --exposure 1 --curve filmic");
    ASSERT_EQ(r.exit_code, 0) << r.output;
    EXPECT_FALSE(contains(plain(r.output), "note:")) << r.output;

    prism::Image expected = prism::downscale(prism::load_image(input), 20);
    prism::ProcessSettings settings;
    settings.exposure_stops = 1.0f;
    settings.curve = prism::parse_curve("filmic");
    ASSERT_EQ(prism::process(expected, settings), prism::ProcessResult::completed);
    const prism::Image written = prism::load_image(output);
    EXPECT_EQ(written.width, 20);
    EXPECT_EQ(written.height, 15);
    EXPECT_EQ(prism::encode_srgb8(written), prism::encode_srgb8(expected));
    EXPECT_TRUE(contains(plain(r.output), "(20x15)\n")) << r.output;
    fs::remove(input);
    fs::remove(output);
}

TEST(Cli, LongSideCountsThePortraitsLongerSide) {
    const fs::path input = write_sized_input("tall.png", 30, 41);
    const fs::path output = temp_file("tall_small.png");
    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output) + " --long-side 21");
    ASSERT_EQ(r.exit_code, 0) << r.output;
    const prism::Image written = prism::load_image(output);
    EXPECT_EQ(written.height, 21);
    EXPECT_EQ(written.width, 15);  // 30 * 21 / 41 = 15.4
    fs::remove(input);
    fs::remove(output);
}

TEST(Cli, LongSideNotSmallerKeepsTheSizeAndSaysSo) {
    const fs::path input = write_sized_input("kept.png", 8, 6);
    const fs::path reference = temp_file("kept_reference.png");
    ASSERT_EQ(run_prism(quoted(input) + " -o " + quoted(reference) + " --exposure 1").exit_code, 0);
    for (const int side : {8, 100}) {
        const fs::path output = temp_file("kept_" + std::to_string(side) + ".png");
        const RunResult r = run_prism(quoted(input) + " -o " + quoted(output) + " --exposure 1" +
                                      " --long-side " + std::to_string(side));
        ASSERT_EQ(r.exit_code, 0) << r.output;
        const std::string expected_note = "note: " + input.string() + " is 8x6, not larger than " +
                                          "--long-side " + std::to_string(side) +
                                          "; kept its size\n";
        EXPECT_TRUE(contains(plain(r.output), expected_note)) << r.output;
        const prism::Image written = prism::load_image(output);
        EXPECT_EQ(written.width, 8);
        EXPECT_EQ(written.height, 6);
        EXPECT_EQ(prism::encode_srgb8(written), prism::encode_srgb8(prism::load_image(reference)));
        fs::remove(output);
    }
    fs::remove(input);
    fs::remove(reference);
}

TEST(Cli, TimeListsTheResizeStageBetweenLoadAndTheOthers) {
    const fs::path input = write_sized_input("timed_big.png", 40, 30);
    const fs::path output = temp_file("timed_small.png");
    const std::string args = quoted(input) + " -o " + quoted(output) + " --exposure 1 --time";

    const std::string with = plain(run_prism(args + " --long-side 20").output);
    const std::size_t load = with.find("  load ");
    const std::size_t resize = with.find("  resize ");
    const std::size_t exposure = with.find("  exposure ");
    ASSERT_NE(resize, std::string::npos) << with;
    EXPECT_LT(load, resize) << with;
    EXPECT_LT(resize, exposure) << with;

    EXPECT_FALSE(contains(plain(run_prism(args).output), "resize"));
    fs::remove(input);
    fs::remove(output);
}

TEST(Cli, LongSideWorksWithAHistogramAlone) {
    const fs::path input = write_sized_input("hist_big.png", 40, 30);
    const fs::path hist = temp_file("hist_small.png");
    const RunResult r = run_prism(quoted(input) + " --histogram " + quoted(hist) + " --long-side 10");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(hist));
    fs::remove(input);
    fs::remove(hist);
}

TEST(Cli, BadLongSideIsUsageError) {
    const fs::path input = write_grey_input();
    const fs::path output = temp_file("bad_long_side.png");
    for (const char* bad : {"0", "-3", "2.5", "abc", "", "+5", " 7", "7 ", "1e3", "0x10", "7px",
                            "99999999999", "2147483648"}) {
        const RunResult r =
            run_prism(quoted(input) + " -o " + quoted(output) + " --long-side \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--long-side '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "error: --long-side needs a whole number")) << r.output;
        EXPECT_TRUE(contains(r.output, "Usage:")) << r.output;
        EXPECT_FALSE(fs::exists(output));
    }
    // The largest value that fits is accepted (and keeps the size).
    EXPECT_EQ(run_prism(quoted(input) + " -o " + quoted(output) + " --long-side 2147483647")
                  .exit_code,
              0);

    RunResult r = run_prism(quoted(input) + " -o " + quoted(output) + " --long-side");
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "--long-side needs a value")) << r.output;
    r = run_prism(quoted(input) + " -o " + quoted(output) + " --long-side 2 --long-side 3");
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "--long-side given more than once")) << r.output;
    fs::remove(input);
    fs::remove(output);
}

TEST(Cli, HelpDescribesLongSide) {
    const RunResult r = run_prism("--help");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "--long-side <pixels>")) << r.output;
    EXPECT_TRUE(contains(r.output, "_prism_<n>.jpg")) << r.output;
}

// Runs only with PRISM_TEST_RAW set; see raw_test.cpp.
TEST(Cli, LongSideWorksOnRaw) {
    const fs::path raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    const fs::path output = temp_file("raw_small.png");
    const RunResult r =
        run_prism(quoted(raw) + " --raw-half --long-side 64 -o " + quoted(output));
    if (contains(r.output, "built without LibRaw")) {
        EXPECT_EQ(r.exit_code, 1) << r.output;
    } else {
        ASSERT_EQ(r.exit_code, 0) << r.output;
        const prism::Image written = prism::load_image(output);
        EXPECT_EQ(std::max(written.width, written.height), 64);
    }
    fs::remove(output);
}

// --- --kelvin and --tint
// ---------------------------------------------------------------------------

namespace {

// A grey JPEG: it has no as-shot light, so 6500 K / tint 0 means "no change".
fs::path write_grey_jpeg() {
    prism::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(4 * 4 * 3, 0.18f);
    const fs::path path = temp_file("grey.jpg");
    prism::save_image(path, img);
    return path;
}

std::string read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// Runs prism on `input` with `flags`, returns the written PNG's bytes (empty if it failed).
std::string run_to_bytes(const fs::path& input, const std::string& flags,
                         int* exit_code = nullptr) {
    const fs::path output = temp_file("wb_out.png");
    fs::remove(output);
    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output) + " " + flags);
    if (exit_code != nullptr) {
        *exit_code = r.exit_code;
    }
    std::string bytes = r.exit_code == 0 ? read_bytes(output) : std::string();
    fs::remove(output);
    return bytes;
}

// The linear value of the first pixel of a PNG held as bytes.
std::array<float, 3> first_pixel(const std::string& png_bytes) {
    const fs::path path = temp_file("wb_pixel.png");
    {
        std::ofstream out(path, std::ios::binary);
        out << png_bytes;
    }
    const prism::Image img = prism::load_image(path);
    fs::remove(path);
    return {img.pixels[0], img.pixels[1], img.pixels[2]};
}

}  // namespace

TEST(CliWhiteBalance, AsShotValuesOnAJpegChangeNothing) {
    const fs::path input = write_grey_jpeg();
    const std::string plain_run = run_to_bytes(input, "--threads 1");
    ASSERT_FALSE(plain_run.empty());
    EXPECT_EQ(run_to_bytes(input, "--kelvin 6500 --tint 0 --threads 1"), plain_run);
    // Either one alone keeps the other at 6500 / 0.
    EXPECT_EQ(run_to_bytes(input, "--kelvin 6500"), plain_run);
    EXPECT_EQ(run_to_bytes(input, "--tint 0"), plain_run);
    fs::remove(input);
}

TEST(CliWhiteBalance, ALowerKelvinIsCoolerAndAHigherOneWarmer) {
    const fs::path input = write_grey_jpeg();
    const auto cool = first_pixel(run_to_bytes(input, "--kelvin 4000"));
    EXPECT_LT(cool[0], cool[2]);
    const auto warm = first_pixel(run_to_bytes(input, "--kelvin 9000"));
    EXPECT_GT(warm[0], warm[2]);
    fs::remove(input);
}

TEST(CliWhiteBalance, TintAloneKeepsTheKelvinAsShot) {
    const fs::path input = write_grey_jpeg();
    const std::string alone = run_to_bytes(input, "--tint 50");
    EXPECT_EQ(alone, run_to_bytes(input, "--kelvin 6500 --tint 50"));
    const auto magenta = first_pixel(alone);
    EXPECT_LT(magenta[1], 0.5f * (magenta[0] + magenta[2]));
    const auto green = first_pixel(run_to_bytes(input, "--tint -50"));
    EXPECT_GT(green[1], 0.5f * (green[0] + green[2]));
    fs::remove(input);
}

TEST(CliWhiteBalance, ARecipesWhiteBalanceIsReplacedByTheFlags) {
    const fs::path input = write_grey_jpeg();
    const fs::path recipe = temp_file("wb.prism");
    {
        std::ofstream out(recipe, std::ios::binary);
        out << "prism_recipe = 2\n[white_balance]\nkelvin = 9000\ntint = 40\n";
    }
    const std::string plain_run = run_to_bytes(input, "");
    const std::string with_recipe = run_to_bytes(input, "--recipe " + quoted(recipe));
    EXPECT_NE(with_recipe, plain_run);
    // The flag replaces the whole stage: the recipe's tint of 40 is not kept, so this is as shot.
    EXPECT_EQ(run_to_bytes(input, "--recipe " + quoted(recipe) + " --kelvin 6500"), plain_run);
    EXPECT_NE(run_to_bytes(input, "--recipe " + quoted(recipe) + " --kelvin 4000"), with_recipe);
    fs::remove(input);
    fs::remove(recipe);
}

TEST(CliWhiteBalance, BadValuesAreUsageErrors) {
    const fs::path input = write_grey_jpeg();
    const fs::path hist = temp_file("hist.png");
    const std::string base = quoted(input) + " --histogram " + quoted(hist) + " ";
    for (const char* bad : {"1999", "15001", "abc", "", "nan", "6500K"}) {
        const RunResult r = run_prism(base + "--kelvin \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--kelvin '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "--kelvin needs a number from 2000 to 15000")) << r.output;
    }
    for (const char* bad : {"101", "-101", "abc", "", "inf"}) {
        const RunResult r = run_prism(base + "--tint \"" + bad + "\"");
        EXPECT_EQ(r.exit_code, 2) << "--tint '" << bad << "': " << r.output;
        EXPECT_TRUE(contains(r.output, "--tint needs a number from -100 to 100")) << r.output;
    }
    for (const char* flag : {"--kelvin", "--tint"}) {
        const RunResult r = run_prism(base + flag);
        EXPECT_EQ(r.exit_code, 2) << flag << ": " << r.output;
        EXPECT_TRUE(contains(r.output, std::string(flag) + " needs a value")) << r.output;
    }
    for (const char* twice : {"--kelvin 5000 --kelvin 6000", "--tint 1 --tint 2"}) {
        const RunResult r = run_prism(base + twice);
        EXPECT_EQ(r.exit_code, 2) << twice << ": " << r.output;
        EXPECT_TRUE(contains(r.output, "given more than once")) << r.output;
    }
    fs::remove(input);
    fs::remove(hist);
}

TEST(CliWhiteBalance, TimeListsTheStageBetweenExposureAndTheRest) {
    const fs::path input = write_grey_jpeg();
    const fs::path output = temp_file("timed_wb.png");
    const RunResult r = run_prism(quoted(input) + " -o " + quoted(output) +
                                  " --saturation 1.5 --kelvin 4000 --exposure 1 --time");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    const std::string text = plain(r.output);
    const std::size_t exposure = text.find("  exposure ");
    const std::size_t white = text.find("  white_balance ");
    const std::size_t saturation = text.find("  saturation ");
    ASSERT_NE(exposure, std::string::npos) << text;
    ASSERT_NE(white, std::string::npos) << text;
    ASSERT_NE(saturation, std::string::npos) << text;
    EXPECT_LT(exposure, white);
    EXPECT_LT(white, saturation);
    // As shot adds no stage.
    const RunResult none =
        run_prism(quoted(input) + " -o " + quoted(output) + " --kelvin 6500 --time");
    EXPECT_FALSE(contains(none.output, "white_balance")) << none.output;
    fs::remove(input);
    fs::remove(output);
}

TEST(CliWhiteBalance, HelpDescribesTheFlags) {
    const RunResult r = run_prism("--help");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(contains(r.output, "--kelvin <K>")) << r.output;
    EXPECT_TRUE(contains(r.output, "--tint <n>")) << r.output;
}

// Runs only with PRISM_TEST_RAW set; see raw_test.cpp. A RAW file's edit is relative to its own
// as-shot light, so the same Kelvin differs from photo to photo; here the direction is checked.
TEST(CliWhiteBalance, ARawPhotoIsRelativeToItsAsShotLight) {
    const fs::path raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "Prism was built without LibRaw";
    }
    const std::string plain_run = run_to_bytes(raw, "--raw-half --long-side 200");
    ASSERT_FALSE(plain_run.empty());
    const auto warm = first_pixel(run_to_bytes(raw, "--raw-half --long-side 200 --kelvin 15000"));
    const auto cool = first_pixel(run_to_bytes(raw, "--raw-half --long-side 200 --kelvin 2000"));
    EXPECT_GT(warm[0] / warm[2], cool[0] / cool[2]);
    EXPECT_NE(run_to_bytes(raw, "--raw-half --long-side 200 --tint 30"), plain_run);
}
