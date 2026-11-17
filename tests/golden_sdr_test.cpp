// SDR golden tests, engine and command line: Prism's 8-bit sRGB output for the sample photo (and,
// with PRISM_TEST_RAW, the maintainer's test RAW) at --long-side 256 under every built-in look must
// stay what 0.7.1 wrote, so HDR work can't change SDR output unnoticed (issue "Lock SDR output
// before HDR"). The app's export has the same tests in tests/gui/golden_export_test.cpp.
//
// The references are files in tests/data/golden/ (see its README), made by
// tools/make_sdr_goldens.sh from prism built at 0.7.1. Regenerate them only on purpose, when the
// SDR output is meant to change, never to make these tests pass. Tolerances and why:
// tests/golden.hpp (exact when built for Apple arm64).
//
//   Looks/GoldenSdrImage.Engine/*  load, reduce, process and encode_srgb8 in-process, as prism
//                                  does; then save_image as PNG must decode to exactly those
//                                  bytes, and as JPEG to the decoded reference JPEG.
//   Looks/GoldenSdrImage.Cli/*     the prism executable writing PNG and JPEG.
//   GoldenSdr.EngineRawLooks       the engine for the test RAW under every RAW look (one decode);
//   GoldenSdr.CliRawNatural        the prism executable for it under Natural only. Both skip
//                                  unless PRISM_TEST_RAW has the SHA-256 in
//                                  golden/raw/source.sha256.
//   GoldenSdr.EngineSyntheticDng, GoldenSdr.CliSyntheticDng  every RAW look on the synthetic DNG
//                                  of synthetic_dng.hpp, written to the temp folder: RAW coverage
//                                  CI has wherever LibRaw is built (skipped where it isn't).
//   GoldenSdr.SyntheticDngIsTheStoredOne  the helper still writes golden/synthetic/synthetic.dng,
//                                  the file those references were made from.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
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
#include "synthetic_dng.hpp"

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
    EXPECT_TRUE(
        matches(Pixels{img.width, img.height, bytes}, want_png, png_tolerance(), "encode_srgb8"));

    // PNG round trip: the file holds exactly the bytes encode_srgb8 gave, on every platform.
    const fs::path png = temp_file("out.png");
    prism::save_image(png, img);
    EXPECT_TRUE(matches(decode(png), Pixels{img.width, img.height, bytes}, Tolerance{0, 0.0},
                        "PNG round trip"));
    fs::remove(png);

    const fs::path jpg = temp_file("out.jpg");
    prism::save_image(jpg, img);
    EXPECT_TRUE(jpeg_matches(jpg, golden, "JPEG"));
    fs::remove(jpg);
}

// Runs the prism executable for one look and checks the PNG and JPEG it writes.
void check_cli(const fs::path& input, const fs::path& recipe, const fs::path& golden) {
    SCOPED_TRACE(recipe.filename().string());
    for (const char* ext : {".png", ".jpg"}) {
        const fs::path out = temp_file(std::string("out") + ext);
        const fs::path log = temp_file("log.txt");
        const std::string command = "\"" + std::string(PRISM_EXE) + "\" \"" + input.string() +
                                    "\" -o \"" + out.string() + "\" --recipe \"" + recipe.string() +
                                    "\" --long-side " + std::to_string(long_side) + " > \"" +
                                    log.string() + "\" 2>&1";
        const int exit_code = prism_test::run_command(command);
        if (exit_code != 0) {
            std::ifstream in(log, std::ios::binary);
            std::stringstream output;
            output << in.rdbuf();
            in.close();
            ADD_FAILURE() << command << "\nexited with " << exit_code << ":\n" << output.str();
        }
        if (std::string(ext) == ".png") {
            EXPECT_TRUE(matches(decode(out), decode(fs::path(golden).concat(ext)), png_tolerance(),
                                "prism .png"));
        } else {
            EXPECT_TRUE(jpeg_matches(out, golden, "prism .jpg"));
        }
        fs::remove(out);
        fs::remove(log);
    }
}

// The synthetic DNG's photosite level, as tools/make_synthetic_dng.cpp writes it (which says why
// 13990: every look is at least 35 codes from its next 8-bit level).
constexpr std::uint16_t synthetic_level = 13990;

std::vector<std::uint8_t> synthetic_dng_bytes() {
    const std::vector<std::byte> bytes = prism_test::synthetic_dng(synthetic_level);
    std::vector<std::uint8_t> out(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(bytes[i]);
    }
    return out;
}

// Writes the synthetic DNG into the test's temp folder, for prism to read as any RAW file.
fs::path write_synthetic_dng() {
    const std::vector<std::uint8_t> bytes = synthetic_dng_bytes();
    const fs::path path = temp_file("synthetic.dng");
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    EXPECT_TRUE(out) << "cannot write " << path.string();
    return path;
}

// Removes a temp file when the test ends, also when a load throws.
struct RemoveOnExit {
    fs::path path;
    ~RemoveOnExit() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

class GoldenSdrImage : public testing::TestWithParam<const char*> {};

}  // namespace

TEST(GoldenSdr, EveryBuiltInLookHasAReference) {
    const auto check = [](const char* folder, const auto& looks) {
        std::vector<std::string> in_folder;
        for (const fs::directory_entry& e :
             fs::directory_iterator(fs::path(PRISM_RECIPES_DIR) / folder)) {
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
    for (const char* look : raw_looks) {
        for (const char* ext : {".png", ".jpg"}) {
            EXPECT_TRUE(fs::is_regular_file(golden_dir() / "synthetic" / (std::string(look) + ext)))
                << "synthetic/" << look << ext;
        }
    }
    EXPECT_TRUE(fs::is_regular_file(golden_dir() / "SOURCE.txt"));
    EXPECT_TRUE(fs::is_regular_file(golden_dir() / "raw" / "SOURCE.txt"));
    const std::string sha = recorded_raw_sha256();
    EXPECT_EQ(sha.size(), 64u) << "raw/source.sha256: " << sha;
    EXPECT_EQ(sha.find_first_not_of("0123456789abcdef"), std::string::npos) << sha;
}

// The comparison rules themselves, both platforms' on every platform, against the stored files
// alone: each reference passes, and a wrong or slightly changed picture fails.
TEST(GoldenSdr, TolerancesWork) {
    for (const bool exact : {true, false}) {
        SCOPED_TRACE(exact ? "Apple arm64 rule" : "rule elsewhere");
        for (const char* look : image_looks) {
            const fs::path golden = golden_dir() / "image" / look;
            const Pixels png = decode(fs::path(golden).concat(".png"));
            EXPECT_TRUE(matches(png, png, png_tolerance(exact), look));
            EXPECT_TRUE(jpeg_matches(fs::path(golden).concat(".jpg"), golden, look, exact));
        }
        const fs::path vivid = golden_dir() / "image" / "vivid";
        const fs::path original = golden_dir() / "image" / "original";
        // Another look's JPEG is not this look's.
        EXPECT_FALSE(jpeg_matches(fs::path(vivid).concat(".jpg"), original, "vivid", exact));
        const Pixels png = decode(fs::path(original).concat(".png"));
        ASSERT_FALSE(png.rgb.empty());
        // One sample 2 levels off fails either PNG rule.
        Pixels two_off = png;
        two_off.rgb[0] = static_cast<std::uint8_t>(two_off.rgb[0] < 128 ? two_off.rgb[0] + 2
                                                                        : two_off.rgb[0] - 2);
        EXPECT_FALSE(matches(two_off, png, png_tolerance(exact), "2 levels"));
        // 1 level off in 1% of the samples fails either PNG rule; in 0.1% only the exact one.
        const auto one_off_in = [&](std::size_t every) {
            Pixels p = png;
            for (std::size_t i = 0; i < p.rgb.size(); i += every) {
                p.rgb[i] = static_cast<std::uint8_t>(p.rgb[i] < 128 ? p.rgb[i] + 1 : p.rgb[i] - 1);
            }
            return p;
        };
        EXPECT_FALSE(matches(one_off_in(100), png, png_tolerance(exact), "1% off"));
        EXPECT_EQ(bool(matches(one_off_in(1000), png, png_tolerance(exact), "0.1% off")), !exact);
    }
}

TEST_P(GoldenSdrImage, Engine) {
    const Reduced reduced = load_reduced(sample_photo());
    check_engine(reduced, recipe_path("image", GetParam()), golden_dir() / "image" / GetParam());
}

TEST_P(GoldenSdrImage, Cli) {
    check_cli(sample_photo(), recipe_path("image", GetParam()),
              golden_dir() / "image" / GetParam());
}

INSTANTIATE_TEST_SUITE_P(Looks, GoldenSdrImage, testing::ValuesIn(image_looks),
                         [](const testing::TestParamInfo<const char*>& info) {
                             return std::string(info.param);
                         });

// Every RAW look on one decode of the test RAW (about 730 MB of float pixels for 61 MP), reduced
// once: the looks only differ after the reduction.
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

// The command line on the test RAW under Natural only: each run decodes the full-size RAW again,
// and GoldenSdr.EngineRawLooks already holds every RAW look; this checks that prism reaches the
// same pixels.
TEST(GoldenSdr, CliRawNatural) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const GoldenRaw raw = golden_raw();
    if (!raw.path) {
        GTEST_SKIP() << raw.skip_reason;
    }
    check_cli(*raw.path, recipe_path("raw", "natural"), golden_dir() / "raw" / "natural");
}

TEST(GoldenSdr, SyntheticDngIsTheStoredOne) {
    // If synthetic_dng.hpp changed, golden/synthetic/ was made from other bytes: write the new file
    // with tools/make_synthetic_dng.cpp and regenerate (tests/data/golden/README.md), on purpose.
    const std::vector<std::uint8_t> stored =
        read_bytes(golden_dir() / "synthetic" / "synthetic.dng");
    ASSERT_FALSE(stored.empty());
    EXPECT_TRUE(stored == synthetic_dng_bytes())
        << "synthetic_dng(" << synthetic_level
        << ") no longer writes golden/synthetic/synthetic.dng";
}

TEST(GoldenSdr, EngineSyntheticDng) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const RemoveOnExit dng{write_synthetic_dng()};
    const Reduced reduced = load_reduced(dng.path);
    for (const char* look : raw_looks) {
        check_engine(reduced, recipe_path("raw", look), golden_dir() / "synthetic" / look);
    }
}

TEST(GoldenSdr, CliSyntheticDng) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const RemoveOnExit dng{write_synthetic_dng()};
    for (const char* look : raw_looks) {
        check_cli(dng.path, recipe_path("raw", look), golden_dir() / "synthetic" / look);
    }
}
