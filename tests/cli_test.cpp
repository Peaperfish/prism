#include <gtest/gtest.h>

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
#include "prism/image.hpp"
#include "run_command.hpp"

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
