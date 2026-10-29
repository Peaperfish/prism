#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "prism/image.hpp"

// End-to-end tests: run the real prism executable (path supplied by CMake as PRISM_EXE)
// and check its exit code and output.

namespace fs = std::filesystem;

namespace {

struct RunResult {
    int exit_code;
    std::string output;  // stdout and stderr together
};

RunResult run_prism(const std::string& args) {
    const fs::path log = fs::path(testing::TempDir()) / "prism_cli_output.txt";
    const std::string command =
        "\"" + std::string(PRISM_EXE) + "\" " + args + " > \"" + log.string() + "\" 2>&1";
    const int status = std::system(command.c_str());

    std::ifstream file(log);
    std::stringstream text;
    text << file.rdbuf();
    file.close();
    fs::remove(log);

#ifdef _WIN32
    return {status, text.str()};
#else
    // On POSIX, std::system returns a wait status; WEXITSTATUS extracts the exit code.
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, text.str()};
#endif
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
    const fs::path path = fs::path(testing::TempDir()) / "prism_cli_grey.png";
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
    const fs::path hist = fs::path(testing::TempDir()) / "prism_cli_hist.png";
    const fs::path zero_matrix = fs::path(testing::TempDir()) / "prism_cli_zero_matrix.txt";
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

TEST(Cli, BadThreadCountIsUsageError) {
    const fs::path input = write_grey_input();
    const fs::path hist = fs::path(testing::TempDir()) / "prism_cli_hist_threads.png";
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
    const fs::path output = fs::path(testing::TempDir()) / "prism_cli_timed.png";

    RunResult r = run_prism(quoted(input) + " -o " + quoted(output) +
                            " --exposure 1 --curve filmic --threads 4 --time");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    // A 4x4 image is far below the ~1 MP threshold, so it runs on one thread whatever was asked.
    EXPECT_TRUE(contains(r.output, "time (1 thread):")) << r.output;
    for (const char* stage : {"load", "exposure", "curve", "save", "total"}) {
        EXPECT_TRUE(contains(r.output, std::string("  ") + stage + " ")) << stage << r.output;
    }
    EXPECT_FALSE(contains(r.output, "matrix")) << "no --matrix was given: " << r.output;

    // Without --time, no timings.
    r = run_prism(quoted(input) + " -o " + quoted(output) + " --threads 1");
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_FALSE(contains(r.output, "time (")) << r.output;

    fs::remove(input);
    fs::remove(output);
}
