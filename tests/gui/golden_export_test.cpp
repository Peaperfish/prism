// SDR golden tests, the app's export (headless: no window, a Session and export_image as the Export
// button runs them). Opening the sample photo (and, with PRISM_TEST_RAW, the maintainer's test
// RAW), choosing each built-in look and exporting at a long side of 256 px must give the pixels
// 0.7.1 wrote, the same references the command line is held to in tests/golden_sdr_test.cpp.
//
// The references are files in tests/data/golden/ (see its README), made by
// tools/make_sdr_goldens.sh from prism built at 0.7.1. Regenerate them only on purpose, when the
// SDR output is meant to change, never to make these tests pass. Tolerances and why:
// tests/golden.hpp (exact when built for Apple arm64).
//
//   Looks/GoldenExportImage.Export/*  decode_for_export + finish_export, encoded, against the PNG
//                                     reference; export_image as PNG must decode to exactly those
//                                     bytes, and as JPEG to the decoded reference JPEG.
//   GoldenExport.RawNatural           the same for the test RAW under Natural only (every RAW look
//                                     is held by GoldenSdr.EngineRawLooks); skips unless
//                                     PRISM_TEST_RAW has the SHA-256 in golden/raw/source.sha256.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "golden.hpp"
#include "prism/gui/presets.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/raw.hpp"

namespace fs = std::filesystem;
using namespace prism_golden;
using prism::gui::InputKind;

namespace {

// A temp path no other test (or other run of this one) is using: see session_test.cpp.
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
           ("prism_golden_export_" + test_name + "_" + std::to_string(pid) + "_" + name);
}

const prism::gui::Preset* find_preset(InputKind kind, const std::string& id) {
    for (const prism::gui::Preset& p : prism::gui::presets_for(kind)) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

// Chooses `look` in the open session, as clicking it in the Looks list does, and checks its export
// against `<golden>.png` and `<golden>.jpg`. The photo is decoded for each look and moved into
// finish_export, so a full-size RAW is held only once at a time.
void check_export(prism::gui::Session& session, const std::string& look, const fs::path& golden) {
    SCOPED_TRACE(look);
    const prism::gui::Preset* preset = find_preset(session.kind(), look);
    ASSERT_NE(preset, nullptr) << "no built-in look " << look;
    prism::gui::EditSettings s = session.settings();
    prism::gui::apply_preset(s, *preset);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    prism::gui::ExportJob job = session.export_job();
    job.long_side = long_side;

    std::variant<prism::Photo, prism::gui::Error> decoded =
        prism::gui::decode_for_export(job.source, job.kind, job.settings, job.threads, {});
    if (const auto* e = std::get_if<prism::gui::Error>(&decoded)) {
        FAIL() << "decode_for_export: " << e->message;
    }
    std::variant<prism::Image, prism::gui::Error> finished =
        prism::gui::finish_export(std::move(std::get<prism::Photo>(decoded)), job.settings,
                                  job.long_side, job.threads, {}, {});
    if (const auto* e = std::get_if<prism::gui::Error>(&finished)) {
        FAIL() << "finish_export: " << e->message;
    }
    const prism::Image& img = std::get<prism::Image>(finished);
    const std::vector<std::uint8_t> bytes = prism::encode_srgb8(img);
    EXPECT_TRUE(matches(Pixels{img.width, img.height, bytes},
                        decode(fs::path(golden).concat(".png")), png_tolerance(), "finish_export"));

    // PNG round trip: the exported file holds exactly those bytes, on every platform.
    const fs::path png = temp_file("out.png");
    ASSERT_EQ(prism::gui::export_image(job, png), std::nullopt);
    EXPECT_TRUE(matches(decode(png), Pixels{img.width, img.height, bytes}, Tolerance{0, 0.0},
                        "exported PNG"));
    fs::remove(png);

    const fs::path jpg = temp_file("out.jpg");
    ASSERT_EQ(prism::gui::export_image(job, jpg), std::nullopt);
    EXPECT_TRUE(jpeg_matches(jpg, golden, "exported JPEG"));
    fs::remove(jpg);
}

class GoldenExportImage : public testing::TestWithParam<const char*> {};

}  // namespace

TEST_P(GoldenExportImage, Export) {
    prism::gui::Session session;
    ASSERT_EQ(session.open_file(sample_photo()), std::nullopt);
    ASSERT_EQ(session.kind(), InputKind::image);
    check_export(session, GetParam(), golden_dir() / "image" / GetParam());
}

INSTANTIATE_TEST_SUITE_P(Looks, GoldenExportImage, testing::ValuesIn(image_looks),
                         [](const testing::TestParamInfo<const char*>& info) {
                             return std::string(info.param);
                         });

// The export of the test RAW under Natural, the default RAW look, only: the check decodes the
// full-size RAW (about 730 MB of float pixels for 61 MP) three times (decode_for_export and two
// export_image calls), and GoldenSdr.EngineRawLooks
// (tests/golden_sdr_test.cpp) already holds every RAW look; this checks the app reaches the same
// pixels.
TEST(GoldenExport, RawNatural) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "this build has no RAW support";
    }
    const GoldenRaw raw = golden_raw();
    if (!raw.path) {
        GTEST_SKIP() << raw.skip_reason;
    }
    prism::gui::Session session;
    ASSERT_EQ(session.open_file(*raw.path), std::nullopt);
    ASSERT_EQ(session.kind(), InputKind::raw);
    check_export(session, "natural", golden_dir() / "raw" / "natural");
}
