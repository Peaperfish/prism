#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "prism/process.hpp"
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
        const std::string bytes = read_bytes(log);
        fs::remove(log);
        // On Windows prism's stdout and stderr are in text mode, so each "\n" reaches the log as
        // "\r\n"; the tests compare against "\n".
        std::string text;
        text.reserve(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                continue;
            }
            text += bytes[i];
        }
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

    // As write_input, at another size.
    fs::path write_sized_input(const std::string& name, int width, int height) const {
        prism::Image img;
        img.width = width;
        img.height = height;
        img.pixels.resize(static_cast<std::size_t>(width) * height * 3);
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            img.pixels[i] = static_cast<float>((i * 5) % 19) / 19.0f;
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

using CliBatch = CliRecipeTest;

TEST_F(CliBatch, SeveralInputsWithoutOutDirAreAUsageError) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    const RunResult r = run_prism(quoted(a) + " " + quoted(b) + " --exposure 1");
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "error: 2 inputs need --out-dir <dir>")) << r.output;
    EXPECT_TRUE(contains(r.output, "Usage:")) << r.output;
}

TEST_F(CliBatch, SeveralInputsWithOAreAUsageError) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    fs::create_directory(path("out"));
    for (const std::string& extra : {std::string(), " --out-dir " + quoted(path("out"))}) {
        const RunResult r =
            run_prism(quoted(a) + " " + quoted(b) + " -o " + quoted(path("x.jpg")) + extra);
        EXPECT_EQ(r.exit_code, 2) << r.output;
        EXPECT_TRUE(
            contains(r.output, "error: -o names one output file, so it takes a single input"))
            << r.output;
        EXPECT_FALSE(fs::exists(path("x.jpg")));
    }
    EXPECT_TRUE(fs::is_empty(path("out")));
}

TEST_F(CliBatch, HistogramWithSeveralInputsIsAUsageError) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    fs::create_directory(path("out"));
    const RunResult r = run_prism(quoted(a) + " " + quoted(b) + " --out-dir " +
                                  quoted(path("out")) + " --histogram " + quoted(path("hist.png")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output,
                         "error: --histogram writes one histogram file, so it takes a "
                         "single input"))
        << r.output;
    EXPECT_TRUE(fs::is_empty(path("out")));
    EXPECT_FALSE(fs::exists(path("hist.png")));
}

TEST_F(CliBatch, OutDirMustBeAnExistingFolder) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    const fs::path not_a_folder = write_text("notes.txt", "hello");

    RunResult r = run_prism(quoted(a) + " " + quoted(b) + " --out-dir " + quoted(path("missing")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "does not exist (create the folder first)")) << r.output;
    EXPECT_FALSE(fs::exists(path("missing")));

    r = run_prism(quoted(a) + " --out-dir " + quoted(not_a_folder));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "' is not a folder")) << r.output;
}

TEST_F(CliBatch, OAndOutDirTogetherAreAUsageError) {
    const fs::path a = write_input("a.png", 1);
    fs::create_directory(path("out"));
    const RunResult r =
        run_prism(quoted(a) + " -o " + quoted(path("x.jpg")) + " --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "error: -o and --out-dir can't be used together")) << r.output;
    EXPECT_FALSE(fs::exists(path("x.jpg")));
    EXPECT_TRUE(fs::is_empty(path("out")));
}

TEST_F(CliBatch, SeveralInputsAreEachWrittenAsStemPrismJpg) {
    fs::create_directory(path("in"));
    fs::create_directory(path("out"));
    const fs::path a = write_input("in/a.png", 1);
    const fs::path b = write_input("in/b.png", 2);
    const fs::path c = write_input("c.png", 3);
    const std::string flags = " --exposure 0.5 --curve filmic";

    const RunResult r = run_prism(quoted(a) + " " + quoted(b) + " " + quoted(c) + flags +
                                  " --out-dir " + quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;

    std::vector<std::string> written;
    for (const fs::directory_entry& e : fs::directory_iterator(path("out"))) {
        written.push_back(e.path().filename().string());
    }
    std::sort(written.begin(), written.end());
    EXPECT_EQ(written, (std::vector<std::string>{"a_prism.jpg", "b_prism.jpg", "c_prism.jpg"}));
    EXPECT_TRUE(contains(r.output, "3 of 3 photos written.\n")) << r.output;
    EXPECT_FALSE(contains(r.output, "error")) << r.output;

    // Each is what a run of its own with -o gives, and each gets its "wrote" line.
    for (const fs::path& input : {a, b, c}) {
        const std::string stem = input.stem().string();
        const fs::path single = path(stem + "_single.jpg");
        const RunResult one = run_prism(quoted(input) + flags + " -o " + quoted(single));
        ASSERT_EQ(one.exit_code, 0) << one.output;
        EXPECT_EQ(read_bytes(path("out") / (stem + "_prism.jpg")), read_bytes(single)) << stem;
        EXPECT_TRUE(contains(r.output, stem + "_prism.jpg (6x4)\n")) << r.output;
    }
}

TEST_F(CliBatch, OneInputWithOutDirIsTheSameAsO) {
    const fs::path a = write_input("photo.png", 1);
    fs::create_directory(path("out"));
    const RunResult r = run_prism(quoted(a) + " --curve filmic --out-dir " + quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    const RunResult o = run_prism(quoted(a) + " --curve filmic -o " + quoted(path("o.jpg")));
    ASSERT_EQ(o.exit_code, 0) << o.output;
    EXPECT_EQ(read_bytes(path("out") / "photo_prism.jpg"), read_bytes(path("o.jpg")));
}

TEST_F(CliBatch, TwoInputsWithTheSameNameAreAUsageErrorBeforeAnythingIsWritten) {
    fs::create_directory(path("one"));
    fs::create_directory(path("two"));
    fs::create_directory(path("out"));
    const fs::path first = write_input("first.png", 1);
    const fs::path a = write_input("one/photo.png", 2);
    // A different case gives the same file on macOS and Windows, so it counts as the same name.
    const fs::path b = write_input("two/PHOTO.png", 3);

    const RunResult r = run_prism(quoted(first) + " " + quoted(a) + " " + quoted(b) +
                                  " --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "photo.png' and '")) << r.output;
    EXPECT_TRUE(contains(r.output, "PHOTO.png' would both be written as 'PHOTO_prism.jpg'"))
        << r.output;
    EXPECT_TRUE(fs::is_empty(path("out"))) << "nothing may be written, not even first.png";
}

TEST_F(CliBatch, RawFlagsWithSeveralInputsNameTheInputThatIsNotRaw) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    fs::create_directory(path("out"));
    const RunResult r =
        run_prism(quoted(a) + " " + quoted(b) + " --raw-half --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "--raw-half only applies to RAW input")) << r.output;
    EXPECT_TRUE(contains(r.output, "a.png' is not a RAW file")) << r.output;
}

// Rule for every change to the command line: a run with one input and the options that existed
// before recipes and batches prints, writes and returns exactly what it did then.
TEST_F(CliBatch, ASingleInputWithTheExistingOptionsIsUnchanged) {
    const fs::path input = write_input("in.png", 4);
    const fs::path out = path("out.jpg");

    // Success: the same bytes as the library's own pipeline, and exactly one "wrote" line.
    const RunResult ok = run_prism(quoted(input) +
                                   " --exposure 0.5 --saturation 1.2 --curve "
                                   "reinhard:3 -o " +
                                   quoted(out));
    ASSERT_EQ(ok.exit_code, 0) << ok.output;
    EXPECT_EQ(ok.output, "wrote " + out.string() + " (6x4)\n");
    prism::Image img = prism::load_image(input, 1);
    prism::ProcessSettings settings;
    settings.exposure_stops = 0.5f;
    settings.saturation = 1.2f;
    settings.curve = prism::parse_curve("reinhard:3");
    ASSERT_EQ(prism::process(img, settings, 1), prism::ProcessResult::completed);
    const fs::path expected = path("expected.jpg");
    prism::save_image(expected, img, 1);
    EXPECT_EQ(read_bytes(out), read_bytes(expected));

    // A file that cannot be read: one error line, exit 1, no usage text.
    const fs::path missing = path("missing.png");
    const RunResult bad = run_prism(quoted(missing) + " -o " + quoted(path("never.jpg")));
    EXPECT_EQ(bad.exit_code, 1) << bad.output;
    EXPECT_EQ(bad.output.rfind("error: cannot load image '" + missing.string() + "'", 0), 0u)
        << bad.output;
    EXPECT_EQ(lines_of(bad.output).size(), 1u) << bad.output;
    EXPECT_FALSE(fs::exists(path("never.jpg")));

    // A usage error: the same message and exit code, then the usage text.
    const RunResult usage = run_prism(quoted(input) + " --exposure 1");
    EXPECT_EQ(usage.exit_code, 2) << usage.output;
    EXPECT_EQ(usage.output.rfind(
                  "error: no output given (use -o <output>, --histogram <file>, or both)\n\n", 0),
              0u)
        << usage.output;
    const RunResult extra = run_prism(quoted(input) + " -o " + quoted(out) + " --nope");
    EXPECT_EQ(extra.exit_code, 2) << extra.output;
    EXPECT_EQ(extra.output.rfind("error: unexpected argument '--nope'\n\n", 0), 0u) << extra.output;
}

TEST_F(CliBatch, FormatPngWritesPngFiles) {
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    fs::create_directory(path("out"));
    const RunResult r = run_prism(quoted(a) + " " + quoted(b) + " --curve filmic --format png " +
                                  "--out-dir " + quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    for (const fs::path& input : {a, b}) {
        const std::string stem = input.stem().string();
        const std::string written = read_bytes(path("out") / (stem + "_prism.png"));
        EXPECT_EQ(written.substr(0, 8), "\x89PNG\r\n\x1a\n") << stem;
        const RunResult one =
            run_prism(quoted(input) + " --curve filmic -o " + quoted(path(stem + "_o.png")));
        ASSERT_EQ(one.exit_code, 0) << one.output;
        EXPECT_EQ(written, read_bytes(path(stem + "_o.png"))) << stem;
    }
}

TEST_F(CliBatch, FormatJpgIsTheDefaultAndTheSameJpegAsO) {
    const fs::path a = write_input("a.png", 1);
    fs::create_directory(path("default"));
    fs::create_directory(path("jpg"));
    ASSERT_EQ(run_prism(quoted(a) + " --out-dir " + quoted(path("default"))).exit_code, 0);
    ASSERT_EQ(run_prism(quoted(a) + " --format jpg --out-dir " + quoted(path("jpg"))).exit_code, 0);
    ASSERT_EQ(run_prism(quoted(a) + " -o " + quoted(path("o.jpg"))).exit_code, 0);

    const std::string by_default = read_bytes(path("default") / "a_prism.jpg");
    EXPECT_EQ(by_default.substr(0, 2), "\xff\xd8") << "a JPEG starts with FF D8";
    EXPECT_EQ(by_default, read_bytes(path("jpg") / "a_prism.jpg"));
    EXPECT_EQ(by_default, read_bytes(path("o.jpg")));
}

TEST_F(CliBatch, FormatNeedsOutDirAndJpgOrPng) {
    const fs::path a = write_input("a.png", 1);
    fs::create_directory(path("out"));

    RunResult r = run_prism(quoted(a) + " --format png -o " + quoted(path("x.jpg")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "error: --format only applies to --out-dir")) << r.output;
    EXPECT_FALSE(fs::exists(path("x.jpg")));

    for (const char* bad : {"jpeg", "PNG", "tiff", ""}) {
        r = run_prism(quoted(a) + " --format \"" + bad + "\" --out-dir " + quoted(path("out")));
        EXPECT_EQ(r.exit_code, 2) << bad << ": " << r.output;
        EXPECT_TRUE(
            contains(r.output, "error: --format needs jpg or png, got '" + std::string(bad) + "'"))
            << r.output;
    }
    r = run_prism(quoted(a) + " --format png --format jpg --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "--format given more than once")) << r.output;
    EXPECT_TRUE(fs::is_empty(path("out")));
}

TEST_F(CliBatch, AFileThatFailsIsNamedAndTheOthersAreStillWritten) {
    fs::create_directory(path("out"));
    const fs::path a = write_input("a.png", 1);
    const fs::path broken = write_text("broken.png", "not a picture");
    const fs::path missing = path("missing.jpg");
    const fs::path d = write_input("d.png", 4);

    const RunResult r = run_prism(quoted(a) + " " + quoted(broken) + " " + quoted(missing) + " " +
                                  quoted(d) + " --exposure 0.5 --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 1) << r.output;
    EXPECT_TRUE(fs::exists(path("out") / "a_prism.jpg"));
    EXPECT_TRUE(fs::exists(path("out") / "d_prism.jpg"));
    EXPECT_FALSE(fs::exists(path("out") / "broken_prism.jpg"));
    EXPECT_FALSE(fs::exists(path("out") / "missing_prism.jpg"));
    EXPECT_TRUE(contains(r.output, "error: cannot load image '" + broken.string() + "'"))
        << r.output;
    EXPECT_TRUE(contains(r.output, "error: cannot load image '" + missing.string() + "'"))
        << r.output;
    EXPECT_TRUE(contains(r.output, "2 of 4 photos written; 2 failed.\n")) << r.output;
    EXPECT_FALSE(contains(r.output, "Usage:")) << r.output;
}

TEST_F(CliBatch, AFailedSaveNamesTheInput) {
    fs::create_directory(path("out"));
    const fs::path a = write_input("a.png", 1);
    const fs::path b = write_input("b.png", 2);
    // A folder where b's result should go: saving it fails on every system.
    fs::create_directory(path("out") / "b_prism.jpg");

    const RunResult r =
        run_prism(quoted(a) + " " + quoted(b) + " --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 1) << r.output;
    EXPECT_TRUE(fs::exists(path("out") / "a_prism.jpg"));
    EXPECT_TRUE(contains(r.output, "error: '" + b.string() + "': cannot save image '")) << r.output;
    EXPECT_TRUE(contains(r.output, "1 of 2 photos written; 1 failed.\n")) << r.output;
}

TEST_F(CliBatch, ASingleInputPrintsNoSummaryLine) {
    const fs::path a = write_input("a.png", 1);
    fs::create_directory(path("out"));
    const RunResult r = run_prism(quoted(a) + " --out-dir " + quoted(path("out")));
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_FALSE(contains(r.output, "photos written")) << r.output;
    EXPECT_EQ(lines_of(r.output).size(), 1u) << r.output;
}

// --- --long-side (#52) --------------------------------------------------------------------------

TEST_F(CliBatch, LongSideNamesEveryOutputWithTheSizeAndShrinksWhatIsLarger) {
    fs::create_directory(path("out"));
    const fs::path wide = write_sized_input("wide.png", 60, 40);
    const fs::path tall = write_sized_input("tall.png", 30, 50);
    const fs::path tiny = write_sized_input("tiny.png", 6, 4);
    const RunResult r = run_prism(quoted(wide) + " " + quoted(tall) + " " + quoted(tiny) +
                                  " --long-side 20 --exposure 0.5 --out-dir " + quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;

    std::vector<fs::path> written;
    for (const fs::directory_entry& e : fs::directory_iterator(path("out"))) {
        written.push_back(e.path().filename());
    }
    std::sort(written.begin(), written.end());
    EXPECT_EQ(written, (std::vector<fs::path>{"tall_prism_20.jpg", "tiny_prism_20.jpg",
                                              "wide_prism_20.jpg"}));

    const auto size_of = [&](const std::string& name) {
        const prism::Image img = prism::load_image(path("out") / name);
        return std::make_pair(img.width, img.height);
    };
    EXPECT_EQ(size_of("wide_prism_20.jpg"), std::make_pair(20, 13));  // 40 * 20 / 60 = 13.3
    EXPECT_EQ(size_of("tall_prism_20.jpg"), std::make_pair(12, 20));
    EXPECT_EQ(size_of("tiny_prism_20.jpg"), std::make_pair(6, 4)) << "not enlarged";

    // The note is only for the photo that was not larger, and goes with its own name.
    const std::string output = r.output;
    EXPECT_TRUE(contains(output, "note: " + tiny.string() +
                                     " is 6 × 4, not larger than --long-side 20; kept its size\n"))
        << output;
    EXPECT_EQ(output.find("note:"), output.rfind("note:")) << "one note only: " << output;
    EXPECT_TRUE(contains(output, "3 of 3 photos written.\n")) << output;
    for (const std::string& name : {"wide_prism_20.jpg (20x13)", "tall_prism_20.jpg (12x20)",
                                    "tiny_prism_20.jpg (6x4)"}) {
        EXPECT_TRUE(contains(output, name + "\n")) << name << output;
    }
}

TEST_F(CliBatch, LongSideNameSuffixAlsoAppliesToOneInputAndToPng) {
    fs::create_directory(path("out"));
    const fs::path wide = write_sized_input("wide.png", 60, 40);
    RunResult r = run_prism(quoted(wide) + " --long-side 30 --format png --out-dir " +
                            quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(path("out") / "wide_prism_30.png"));
    EXPECT_FALSE(fs::exists(path("out") / "wide_prism.png"));
}

TEST_F(CliBatch, AnExplicitOutputPathIsUsedAsIsWithLongSide) {
    const fs::path wide = write_sized_input("wide.png", 60, 40);
    const fs::path chosen = path("my_name.png");
    const RunResult r = run_prism(quoted(wide) + " --long-side 30 -o " + quoted(chosen));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(chosen));
    EXPECT_EQ(prism::load_image(chosen).width, 30);
    EXPECT_FALSE(fs::exists(path("wide_prism_30.png")));
    EXPECT_FALSE(fs::exists(path("my_name_30.png")));
}

TEST_F(CliBatch, WithoutLongSideTheNamesHaveNoSizeInThem) {
    fs::create_directory(path("out"));
    const fs::path wide = write_sized_input("wide.png", 60, 40);
    const RunResult r = run_prism(quoted(wide) + " --out-dir " + quoted(path("out")));
    ASSERT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(path("out") / "wide_prism.jpg"));
}

TEST_F(CliBatch, LongSideMakesNoDifferenceToWhichInputsClash) {
    // a.png and a.jpg both become a_prism_20.jpg: still caught before anything is written.
    fs::create_directory(path("out"));
    const fs::path a = write_sized_input("a.png", 60, 40);
    const fs::path b = write_text("a.jpg", "not a picture");
    const RunResult r = run_prism(quoted(a) + " " + quoted(b) + " --long-side 20 --out-dir " +
                                  quoted(path("out")));
    EXPECT_EQ(r.exit_code, 2) << r.output;
    EXPECT_TRUE(contains(r.output, "would both be written as 'a_prism_20.jpg'")) << r.output;
    EXPECT_TRUE(fs::is_empty(path("out")));
}
