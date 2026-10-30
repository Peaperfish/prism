#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/gui/presets.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/raw.hpp"
#include "prism/resize.hpp"
#include "synthetic_dng.hpp"

namespace fs = std::filesystem;
using prism::gui::EditSettings;
using prism::gui::ErrorKind;
using prism::gui::InputKind;
using prism::gui::MaybeError;
using prism::gui::Session;

namespace {

// A file name no other test (or test process: ctest -j) uses, as in cli_test.cpp.
fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_session_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// A width x height PNG of smooth colour gradients, written to a temporary file.
fs::path write_png(const std::string& name, int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img.pixels.push_back(static_cast<float>(x) / width);
            img.pixels.push_back(static_cast<float>(y) / height);
            img.pixels.push_back(0.18f);
        }
    }
    const fs::path path = temp_file(name);
    prism::save_image(path, img);
    return path;
}

fs::path write_bytes(const std::string& name, const std::vector<std::byte>& bytes) {
    const fs::path path = temp_file(name);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

constexpr std::uint16_t dng_level = 16384;  // a quarter of white

fs::path write_dng(const std::string& name) {
    return write_bytes(name, prism_test::synthetic_dng(dng_level));
}

fs::path write_text(const std::string& name, const std::string& text) {
    const fs::path path = temp_file(name);
    std::ofstream(path) << text;
    return path;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out;
    for (const float v : img.pixels) {
        out.push_back(std::bit_cast<std::uint32_t>(v));
    }
    return out;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

EditSettings vivid_look() {
    EditSettings s;
    s.exposure_stops = 0.5f;
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = prism::gui::CurveKind::gamma;
    s.gamma = 1.3f;
    return s;
}

}  // namespace

// --- opening ----------------------------------------------------------------------------------

TEST(Session, OpensAPngWithItsDefaultPreset) {
    const fs::path png = write_png("in.png", 300, 200);
    Session session;
    EXPECT_FALSE(session.has_file());
    ASSERT_EQ(session.open_file(png), std::nullopt);
    EXPECT_TRUE(session.has_file());
    EXPECT_EQ(session.path(), png);
    EXPECT_EQ(session.kind(), InputKind::image);
    EXPECT_EQ(session.decoded_width(), 300);
    EXPECT_EQ(session.decoded_height(), 200);
    // Smaller than 2560 px: the preview base is the image itself.
    EXPECT_EQ(bits(*session.preview_base()), bits(prism::load_image(png)));
    EXPECT_EQ(prism::gui::matching_preset(InputKind::image, session.settings())->id, "original");
    EXPECT_FALSE(session.history().can_undo());
    fs::remove(png);
}

TEST(Session, ThePreviewBaseIsTheDownscaledImage) {
    const fs::path png = write_png("in.png", 300, 200);
    Session session({.preview_long_side = 100, .threads = 1});
    ASSERT_EQ(session.open_file(png), std::nullopt);
    const prism::Image& base = *session.preview_base();
    EXPECT_EQ(base.width, 100);
    EXPECT_EQ(base.height, 67);
    EXPECT_EQ(bits(base), bits(prism::downscale(prism::load_image(png), 100, 1)));
    fs::remove(png);
}

TEST(Session, RejectsFilesItCannotOpenWithClearMessages) {
    Session session;
    MaybeError e = session.open_file(temp_file("photo.tiff"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::unsupported_format);
    EXPECT_TRUE(contains(e->message, "Prism opens PNG, JPEG and camera RAW files")) << e->message;
    EXPECT_TRUE(contains(e->message, "photo.tiff' isn't one of them")) << e->message;

    e = session.open_file(temp_file("missing.jpg"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::not_found);
    EXPECT_TRUE(contains(e->message, "missing.jpg")) << e->message;

    const fs::path junk = write_text("junk.png", "this is not a PNG");
    e = session.open_file(junk);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::unreadable);
    EXPECT_TRUE(contains(e->message, "may be damaged, or not really a PNG")) << e->message;
    EXPECT_FALSE(e->details.empty()) << "the library's own message goes in the details";
    EXPECT_FALSE(session.has_file());
    fs::remove(junk);
}

TEST(Session, AFailedOpenKeepsThePreviousFile) {
    const fs::path png = write_png("in.png", 40, 30);
    const fs::path junk = write_text("junk.jpg", "not a JPEG either");
    Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    ASSERT_EQ(session.set_settings(vivid_look()), std::nullopt);

    ASSERT_TRUE(session.open_file(junk));
    EXPECT_EQ(session.path(), png);
    EXPECT_EQ(session.settings(), vivid_look());
    EXPECT_TRUE(session.history().can_undo());
    fs::remove(png);
    fs::remove(junk);
}

TEST(Session, OpensARawFileAtHalfSizeOrSaysWhyNot) {
    const fs::path dng = write_dng("in.dng");
    Session session;
    const MaybeError e = session.open_file(dng);
    if (!prism::raw_support_available()) {
        ASSERT_TRUE(e);
        EXPECT_EQ(e->kind, ErrorKind::no_raw_support);
        EXPECT_TRUE(contains(e->message, "built without RAW support")) << e->message;
        fs::remove(dng);
        return;
    }
    ASSERT_EQ(e, std::nullopt) << e->message << " / " << e->details;
    EXPECT_EQ(session.kind(), InputKind::raw);
    EXPECT_EQ(session.decoded_width(), prism_test::synthetic_dng_size / 2);
    EXPECT_EQ(session.preview_base()->width, prism_test::synthetic_dng_size / 2);
    EXPECT_EQ(prism::gui::matching_preset(InputKind::raw, session.settings())->id, "natural");
    fs::remove(dng);
}

TEST(Session, AnotherFileStartsOverWithItsOwnDefault) {
    const fs::path a = write_png("a.png", 40, 30);
    const fs::path b = write_png("b.jpg", 20, 10);
    Session session;
    ASSERT_EQ(session.open_file(a), std::nullopt);
    ASSERT_EQ(session.set_settings(vivid_look()), std::nullopt);
    ASSERT_EQ(session.open_file(b), std::nullopt);
    EXPECT_EQ(session.path(), b);
    EXPECT_EQ(session.decoded_width(), 20);
    EXPECT_EQ(session.settings(), EditSettings{});
    EXPECT_FALSE(session.history().can_undo());
    fs::remove(a);
    fs::remove(b);
}

// --- editing ----------------------------------------------------------------------------------

TEST(Session, InvalidSettingsAreRefused) {
    Session session;
    EditSettings bad;
    bad.saturation = 11.0f;
    const MaybeError e = session.set_settings(bad);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::invalid_settings);
    EXPECT_TRUE(contains(e->message, "Saturation must be from 0 to 10")) << e->message;
    EXPECT_EQ(session.settings(), EditSettings{});
}

TEST(Session, UndoAndRedoGoThroughTheHistory) {
    Session session;
    session.begin_gesture();
    for (float stops = 0.1f; stops < 1.0f; stops += 0.1f) {
        EditSettings s = session.settings();
        s.exposure_stops = stops;
        ASSERT_EQ(session.set_settings(s), std::nullopt);
    }
    session.end_gesture();
    EXPECT_EQ(session.history().undo_steps(), 1u);
    ASSERT_EQ(session.undo(), std::nullopt);
    EXPECT_EQ(session.settings(), EditSettings{});
    ASSERT_EQ(session.redo(), std::nullopt);
    EXPECT_NE(session.settings(), EditSettings{});
}

TEST(Session, ChangingTheRawCameraMatrixDecodesAgain) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "needs LibRaw";
    }
    const fs::path dng = write_dng("in.dng");
    // The synthetic DNG's camera RGB is already linear sRGB, so a camera matrix of 0.5 * identity
    // halves every value.
    const fs::path half = write_text("half.txt", "0.5 0 0\n0 0.5 0\n0 0 0.5\n");
    Session session;
    ASSERT_EQ(session.open_file(dng), std::nullopt);
    const float before = session.preview_base()->pixels[0];

    EditSettings s = session.settings();
    s.raw_camera_matrix = prism::gui::MatrixFile::load(half);
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    EXPECT_NEAR(session.preview_base()->pixels[0], 0.5f * before, 1e-3f);

    ASSERT_EQ(session.undo(), std::nullopt);  // back to LibRaw's own matrix
    EXPECT_NEAR(session.preview_base()->pixels[0], before, 1e-6f);
    fs::remove(dng);
    fs::remove(half);
}

// --- export -----------------------------------------------------------------------------------

TEST(Session, ExportMatchesProcessingTheFullImage) {
    const fs::path png = write_png("in.png", 300, 200);
    const fs::path out = temp_file("out.png");
    Session session({.preview_long_side = 64, .threads = 0});
    ASSERT_EQ(session.open_file(png), std::nullopt);
    ASSERT_EQ(session.set_settings(vivid_look()), std::nullopt);
    ASSERT_EQ(prism::gui::export_image(session.export_job(), out), std::nullopt);

    // What the CLI does: load at full size, process, encode.
    prism::Image expected = prism::load_image(png);
    ASSERT_EQ(prism::process(expected, prism::gui::to_process_settings(vivid_look())),
              prism::ProcessResult::completed);
    const prism::Image written = prism::load_image(out);
    EXPECT_EQ(written.width, 300) << "export is full size, not preview size";
    EXPECT_EQ(prism::encode_srgb8(written), prism::encode_srgb8(expected));
    fs::remove(png);
    fs::remove(out);
}

TEST(Session, ExportsJpegToo) {
    const fs::path png = write_png("in.png", 50, 40);
    const fs::path out = temp_file("out.JPG");
    Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    ASSERT_EQ(prism::gui::export_image(session.export_job(), out), std::nullopt);
    EXPECT_EQ(prism::load_image(out).width, 50);
    fs::remove(png);
    fs::remove(out);
}

TEST(Session, TheExportJobIsACopy) {
    const fs::path png = write_png("in.png", 30, 20);
    Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    const prism::gui::ExportJob job = session.export_job();
    ASSERT_EQ(session.set_settings(vivid_look()), std::nullopt);
    EXPECT_EQ(job.settings, EditSettings{}) << "editing after export started must not change it";
    EXPECT_EQ(job.source, png);
    fs::remove(png);
}

TEST(Session, ExportCancelledBeforehandOrMidwayWritesNothing) {
    const fs::path png = write_png("in.png", 60, 40);
    const fs::path out = temp_file("out.png");
    Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    ASSERT_EQ(session.set_settings(vivid_look()), std::nullopt);

    std::stop_source before;
    before.request_stop();
    MaybeError e = prism::gui::export_image(session.export_job(), out, before.get_token());
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::cancelled);
    EXPECT_FALSE(fs::exists(out));

    std::stop_source midway;
    std::vector<std::string> stages;
    e = prism::gui::export_image(session.export_job(), out, midway.get_token(),
                                 [&](const prism::StageTime& t) {
                                     stages.emplace_back(t.name);
                                     midway.request_stop();  // after the first stage
                                 });
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::cancelled);
    EXPECT_EQ(stages, std::vector<std::string>{"exposure"});
    EXPECT_FALSE(fs::exists(out));
    fs::remove(png);
}

TEST(Session, ExportErrorsAreExplained) {
    const fs::path png = write_png("in.png", 20, 20);
    Session session;
    ASSERT_EQ(session.open_file(png), std::nullopt);
    const prism::gui::ExportJob job = session.export_job();

    MaybeError e = prism::gui::export_image(job, temp_file("out.webp"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::unsupported_format);
    EXPECT_TRUE(contains(e->message, ".png, .jpg or .jpeg")) << e->message;

    e = prism::gui::export_image(job, png);
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::save_failed);
    EXPECT_TRUE(contains(e->message, "won't overwrite the photo you opened")) << e->message;

    e = prism::gui::export_image(job, temp_file("no_such_folder") / "out.png");
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::save_failed);
    EXPECT_TRUE(contains(e->message, "Check that the folder exists")) << e->message;
    EXPECT_FALSE(e->details.empty());

    fs::remove(png);
    e = prism::gui::export_image(job, temp_file("out.png"));
    ASSERT_TRUE(e);
    EXPECT_EQ(e->kind, ErrorKind::not_found) << "the source was deleted after opening";
}

TEST(Session, RawExportIsFullSizeUnlessHalfSizeIsChosen) {
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "needs LibRaw";
    }
    const fs::path dng = write_dng("in.dng");
    const fs::path out = temp_file("out.png");
    Session session;
    ASSERT_EQ(session.open_file(dng), std::nullopt);
    ASSERT_EQ(prism::gui::export_image(session.export_job(), out), std::nullopt);
    EXPECT_EQ(prism::load_image(out).width, prism_test::synthetic_dng_size);

    EditSettings s = session.settings();
    s.raw_half_size = true;
    ASSERT_EQ(session.set_settings(s), std::nullopt);
    ASSERT_EQ(prism::gui::export_image(session.export_job(), out), std::nullopt);
    EXPECT_EQ(prism::load_image(out).width, prism_test::synthetic_dng_size / 2);
    fs::remove(dng);
    fs::remove(out);
}
