#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/image.hpp"
#include "run_command.hpp"

// End-to-end tests of --recipe and of batches (several inputs with --out-dir), through the real
// prism executable.

namespace fs = std::filesystem;

namespace {

struct RunResult {
    int exit_code;
    std::string output;  // stdout and stderr together
};

std::string read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string quoted(const fs::path& path) {
    return "\"" + path.string() + "\"";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

// Each test works in a folder of its own, named after the test and the process (ctest -j runs
// tests side by side, possibly from two build directories), removed again afterwards.
class CliRecipeTest : public testing::Test {
protected:
    void SetUp() override {
        const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
        const int pid = _getpid();
#else
        const int pid = static_cast<int>(getpid());
#endif
        dir_ = fs::path(testing::TempDir()) /
               ("prism_cli_batch_" + std::string(test->name()) + "_" + std::to_string(pid));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    [[nodiscard]] fs::path path(const std::string& name) const { return dir_ / name; }

    RunResult run_prism(const std::string& args) const {
        const fs::path log = dir_.parent_path() / (dir_.filename().string() + "_log.txt");
        const std::string command =
            "\"" + std::string(PRISM_EXE) + "\" " + args + " > " + quoted(log) + " 2>&1";
        const int exit_code = prism_test::run_command(command);
        std::string text = read_bytes(log);
        fs::remove(log);
        return {exit_code, std::move(text)};
    }

    // A small colourful picture (a different one for each `seed`), so that every stage changes it.
    fs::path write_input(const std::string& name, int seed = 0) const {
        prism::Image img;
        img.width = 6;
        img.height = 4;
        img.pixels.resize(6 * 4 * 3);
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            img.pixels[i] =
                static_cast<float>((i * 7 + static_cast<std::size_t>(seed) * 13) % 23) / 23.0f;
        }
        const fs::path p = path(name);
        prism::save_image(p, img);
        return p;
    }

    fs::path write_text(const std::string& name, const std::string& text) const {
        const fs::path p = path(name);
        std::ofstream(p, std::ios::binary) << text;
        return p;
    }

    // Files in the test's folder, by name.
    [[nodiscard]] std::vector<std::string> files() const {
        std::vector<std::string> names;
        for (const fs::directory_entry& e : fs::directory_iterator(dir_)) {
            names.push_back(e.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    fs::path dir_;
};

const char* const every_stage_recipe =
    "prism_recipe = 1\n"
    "[exposure]\n"
    "stops = +0.5\n"
    "[matrix]\n"
    "red = [1.05, 0.03, -0.01]\n"
    "green = [0.01, 1.0, 0.0]\n"
    "blue = [-0.02, -0.02, 0.94]\n"
    "[saturation]\n"
    "amount = 1.3\n"
    "[contrast]\n"
    "amount = 1.2\n"
    "[curve]\n"
    "shape = \"reinhard\"\n"
    "white = 3.0\n";

const char* const every_stage_matrix = "1.05 0.03 -0.01\n0.01 1.0 0.0\n-0.02 -0.02 0.94\n";

const std::string every_stage_flags =
    " --exposure 0.5 --saturation 1.3 --contrast 1.2 --curve reinhard:3";

}  // namespace

using CliRecipe = CliRecipeTest;

TEST_F(CliRecipe, GivesTheSamePixelsAsTheSameValuesAsFlags) {
    const fs::path input = write_input("in.png");
    const fs::path recipe = write_text("look.prism", every_stage_recipe);
    const fs::path matrix = write_text("grade.txt", every_stage_matrix);

    const RunResult by_recipe =
        run_prism(quoted(input) + " --recipe " + quoted(recipe) + " -o " + quoted(path("r.png")));
    ASSERT_EQ(by_recipe.exit_code, 0) << by_recipe.output;
    const RunResult by_flags = run_prism(quoted(input) + " --matrix " + quoted(matrix) +
                                         every_stage_flags + " -o " + quoted(path("f.png")));
    ASSERT_EQ(by_flags.exit_code, 0) << by_flags.output;

    EXPECT_EQ(read_bytes(path("r.png")), read_bytes(path("f.png")));
    EXPECT_NE(read_bytes(path("r.png")), read_bytes(input)) << "the recipe should change it";
    EXPECT_TRUE(contains(by_recipe.output, "(6x4)")) << by_recipe.output;
}

TEST_F(CliRecipe, AFlagBeatsTheRecipeForItsStageOnly) {
    const fs::path input = write_input("in.png");
    const fs::path recipe = write_text("look.prism",
                                       "prism_recipe = 1\n"
                                       "[exposure]\nstops = +2.0\n"
                                       "[saturation]\namount = 0.5\non = false\n"
                                       "[curve]\nshape = \"filmic\"\n");

    // --exposure replaces the recipe's +2; --saturation turns the switched-off stage on with its
    // own value; the curve still comes from the recipe.
    const RunResult mixed =
        run_prism(quoted(input) + " --recipe " + quoted(recipe) +
                  " --exposure -1 --saturation 1.5 -o " + quoted(path("m.png")));
    ASSERT_EQ(mixed.exit_code, 0) << mixed.output;
    const RunResult flags =
        run_prism(quoted(input) + " --exposure -1 --saturation 1.5 --curve filmic" + " -o " +
                  quoted(path("f.png")));
    ASSERT_EQ(flags.exit_code, 0) << flags.output;
    EXPECT_EQ(read_bytes(path("m.png")), read_bytes(path("f.png")));

    // Without the flags, the stage that is off stays off.
    const RunResult plain =
        run_prism(quoted(input) + " --recipe " + quoted(recipe) + " -o " + quoted(path("p.png")));
    ASSERT_EQ(plain.exit_code, 0) << plain.output;
    const RunResult same =
        run_prism(quoted(input) + " --exposure 2 --curve filmic -o " + quoted(path("s.png")));
    ASSERT_EQ(same.exit_code, 0) << same.output;
    EXPECT_EQ(read_bytes(path("p.png")), read_bytes(path("s.png")));
}

TEST_F(CliRecipe, GivenTwiceIsAUsageError) {
    const fs::path input = write_input("in.png");
    const fs::path recipe = write_text("look.prism", every_stage_recipe);
    const RunResult r = run_prism(quoted(input) + " --recipe " + quoted(recipe) + " --recipe " +
                                  quoted(recipe) + " -o " + quoted(path("out.png")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "error: --recipe given more than once")) << r.output;
    EXPECT_FALSE(fs::exists(path("out.png")));
}

TEST_F(CliRecipe, EachMistakeIsItsOwnErrorLineAndNothingIsWritten) {
    const fs::path input = write_input("in.png");
    const fs::path recipe = write_text("bad.prism",
                                       "prism_recipe = 1\n"
                                       "\n"
                                       "[saturation]\n"
                                       "amout = 1.2\n"
                                       "amount = 1.2\n"
                                       "[contrast]\n"
                                       "amount = 50\n");
    const RunResult r =
        run_prism(quoted(input) + " --recipe " + quoted(recipe) + " -o " + quoted(path("out.png")));
    EXPECT_EQ(r.exit_code, 1) << r.output;
    EXPECT_FALSE(fs::exists(path("out.png")));
    EXPECT_FALSE(contains(r.output, "Usage:")) << r.output;

    // Every line of the output is one mistake, each with its own "error: " and line number.
    const std::vector<std::string> lines = lines_of(r.output);
    ASSERT_EQ(lines.size(), 2u) << r.output;
    EXPECT_EQ(lines[0].rfind("error: recipe '", 0), 0u) << lines[0];
    EXPECT_TRUE(contains(lines[0], "bad.prism' line 4: unknown setting 'amout'")) << lines[0];
    EXPECT_EQ(lines[1].rfind("error: recipe '", 0), 0u) << lines[1];
    EXPECT_TRUE(contains(lines[1], "bad.prism' line 7: ")) << lines[1];
}

TEST_F(CliRecipe, AMissingRecipeFileIsAnErrorAndNothingIsWritten) {
    const fs::path input = write_input("in.png");
    const RunResult r = run_prism(quoted(input) + " --recipe " + quoted(path("nowhere.prism")) +
                                  " -o " + quoted(path("out.png")));
    EXPECT_EQ(r.exit_code, 1) << r.output;
    EXPECT_FALSE(fs::exists(path("out.png")));
    const std::vector<std::string> lines = lines_of(r.output);
    ASSERT_EQ(lines.size(), 1u) << r.output;
    EXPECT_EQ(lines[0].rfind("error: recipe '", 0), 0u) << lines[0];
    EXPECT_TRUE(contains(lines[0], "nowhere.prism': the file does not exist")) << lines[0];
}
