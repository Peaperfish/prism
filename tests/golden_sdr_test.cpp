// SDR golden tests, engine and command line: Prism's 8-bit sRGB output for the sample photo (and,
// with PRISM_TEST_RAW, the maintainer's test RAW) at --long-side 256 under every built-in look must
// stay what 0.7.1 wrote, so HDR work can't change SDR output unnoticed (issue "Lock SDR output
// before HDR"). The app's export has the same tests in tests/gui/golden_export_test.cpp.
//
// The references are files in tests/data/golden/ (see its README), made by
// tools/make_sdr_goldens.sh from prism built at 0.7.1. Regenerate them only on purpose, when the SDR
// output is meant to change, never to make these tests pass. Tolerances and why: tests/golden.hpp
// (exact when built for Apple arm64).
//
//   Looks/GoldenSdrImage.Engine/*  load, reduce, process and encode_srgb8 in-process, as prism does;
//                                 then save_image as PNG must decode to exactly those bytes, and as
//                                 JPEG to the decoded reference JPEG.
//   Looks/GoldenSdrImage.Cli/*     the prism executable writing PNG and JPEG.
//   GoldenSdr.EngineRawLooks, GoldenSdr.CliRawLooks  the same for the test RAW: they skip unless
//                                 PRISM_TEST_RAW has the SHA-256 in golden/raw/source.sha256.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "golden.hpp"
#include "prism/frame.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/raw.hpp"
#include "prism/recipe.hpp"
#include "run_command.hpp"

namespace fs = std::filesystem;
using namespace prism_golden;

namespace {

// A temp path no other test (or other run of this one) is using: see cli_test.cpp.
fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    std::string test_name = test->name();
    for (char& c : test_name) {
        if (c == '/') {
            c = '_';
        }
    }
    return fs::path(testing::TempDir()) /
           ("prism_golden_" + test_name + "_" + std::to_string(pid) + "_" + name);
}

fs::path recipe_path(const char* folder, const std::string& look) {
    return fs::path(PRISM_RECIPES_DIR) / folder / (look + ".prism");
}

std::optional<prism::Chromaticity> as_shot_of(const prism::PhotoInfo& info) {
    if (!info.as_shot_white_xy) {
        return std::nullopt;
    }
    return prism::Chromaticity{(*info.as_shot_white_xy)[0], (*info.as_shot_white_xy)[1]};
}

// The decoded photo reduced to the golden size, as prism does before any stage runs.
struct Reduced {
    prism::Image image;
    std::optional<prism::Chromaticity> as_shot;
};

Reduced load_reduced(const fs::path& input) {
    prism::Photo photo = prism::load_photo(input);
    Reduced r;
    r.as_shot = as_shot_of(photo.info);
    r.image = prism::frame_and_reduce(std::move(photo.image), std::nullopt, long_side).image;
    return r;
}

// Runs one look on a copy of `reduced` in-process, the way prism's command line does, and checks
// the bytes and both file formats against the references `<golden>.png` and `<golden>.jpg`.
void check_engine(const Reduced& reduced, const fs::path& recipe, const fs::path& golden) {
    SCOPED_TRACE(recipe.filename().string());
    const prism::ProcessSettings settings = prism::merge_recipe_and_flags(
        prism::load_recipe(recipe), prism::ProcessSettings{}, reduced.as_shot);
    prism::Image img = reduced.image;
    (void)prism::process(img, settings);
    const std::vector<std::uint8_t> bytes = prism::encode_srgb8(img);

    const Pixels want_png = decode(fs::path(golden).concat(".png"));
    EXPECT_TRUE(matches(Pixels{img.width, img.height, bytes}, want_png, png_tolerance(),
                        "encode_srgb8"));

    // PNG round trip: the file holds exactly the bytes encode_srgb8 gave, on every platform.
    const fs::path png = temp_file("out.png");
    prism::save_image(png, img);
    EXPECT_TRUE(matches(decode(png), Pixels{img.width, img.height, bytes}, Tolerance{0, 0.0},
                        "PNG round trip"));
    fs::remove(png);

    const fs::path jpg = temp_file("out.jpg");
    prism::save_image(jpg, img);
    EXPECT_TRUE(matches(decode(jpg), decode(fs::path(golden).concat(".jpg")), jpeg_tolerance(),
                        "JPEG"));
    fs::remove(jpg);
}

// Runs the prism executable for one look and checks the PNG and JPEG it writes.
void check_cli(const fs::path& input, const fs::path& recipe, const fs::path& golden) {
    SCOPED_TRACE(recipe.filename().string());
    for (const char* ext : {".png", ".jpg"}) {
        const fs::path out = temp_file(std::string("out") + ext);
        const fs::path log = temp_file("log.txt");
        const std::string command = "\"" + std::string(PRISM_EXE) + "\" \"" + input.string() +
                                    "\" -o \"" + out.string() + "\" --recipe \"" +
                                    recipe.string() + "\" --long-side " +
                                    std::to_string(long_side) + " > \"" + log.string() + "\" 2>&1";
        EXPECT_EQ(prism_test::run_command(command), 0) << command;
        const Tolerance t = std::string(ext) == ".png" ? png_tolerance() : jpeg_tolerance();
        EXPECT_TRUE(matches(decode(out), decode(fs::path(golden).concat(ext)), t,
                            std::string("prism ") + ext));
        fs::remove(out);
        fs::remove(log);
    }
}

class GoldenSdrImage : public testing::TestWithParam<const char*> {};

}  // namespace

TEST(GoldenSdr, EveryBuiltInLookHasAReference) {
    const auto check = [](const char* folder, const auto& looks) {
        std::vector<std::string> in_folder;
        for (const fs::directory_entry& e : fs::directory_iterator(fs::path(PRISM_RECIPES_DIR) /
                                                                   folder)) {
            if (e.path().extension() == fs::path(".prism")) {
                in_folder.push_back(e.path().stem().string());
            }
        }
        EXPECT_EQ(in_folder.size(), looks.size()) << folder;
        for (const char* look : looks) {
            EXPECT_TRUE(fs::is_regular_file(recipe_path(folder, look))) << folder << "/" << look;
            for (const char* ext : {".png", ".jpg"}) {
                EXPECT_TRUE(fs::is_regular_file(golden_dir() / folder / (std::string(look) + ext)))
                    << folder << "/" << look << ext;
            }
        }
    };
    check("image", image_looks);
    check("raw", raw_looks);
}

TEST_P(GoldenSdrImage, Engine) {
    const Reduced reduced = load_reduced(sample_photo());
    check_engine(reduced, recipe_path("image", GetParam()), golden_dir() / "image" / GetParam());
}

TEST_P(GoldenSdrImage, Cli) {
    check_cli(sample_photo(), recipe_path("image", GetParam()), golden_dir() / "image" / GetParam());
}

INSTANTIATE_TEST_SUITE_P(Looks, GoldenSdrImage, testing::ValuesIn(image_looks),
                         [](const testing::TestParamInfo<const char*>& info) {
                             return std::string(info.param);
                         });

// One test per kind for all RAW looks: each decodes a full-size RAW, and ctest -j running five at
// once would need several GB.
TEST(GoldenSdr, EngineRawLooks) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const GoldenRaw raw = golden_raw();
    if (!raw.path) {
        GTEST_SKIP() << raw.skip_reason;
    }
    const Reduced reduced = load_reduced(*raw.path);
    for (const char* look : raw_looks) {
        check_engine(reduced, recipe_path("raw", look), golden_dir() / "raw" / look);
    }
}

TEST(GoldenSdr, CliRawLooks) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const GoldenRaw raw = golden_raw();
    if (!raw.path) {
        GTEST_SKIP() << raw.skip_reason;
    }
    for (const char* look : raw_looks) {
        check_cli(*raw.path, recipe_path("raw", look), golden_dir() / "raw" / look);
    }
}
