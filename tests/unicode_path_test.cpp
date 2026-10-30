#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/camera_profile.hpp"
#include "prism/dng_profile.hpp"
#include "prism/image.hpp"
#include "prism/pipeline.hpp"
#include "run_command.hpp"
#include "tiff_builder.hpp"

// Every path Prism opens, writes or prints must survive characters outside ASCII. On macOS and
// Linux a path is just bytes (UTF-8 by convention), so these tests mostly guard against
// regressions there. On Windows a path is UTF-16, and anything that goes through the "ANSI" code
// page (std::filesystem::path::string(), fopen, a narrow argv) loses or throws on characters the
// code page can't represent: CJK on a Western system, emoji everywhere.

namespace fs = std::filesystem;

namespace {

// u-escapes, not the characters themselves, so the bytes are the same whatever encoding the
// compiler reads this file in; u8"" makes them char8_t, which fs::path knows is UTF-8 (a plain
// "" string would be read in the ANSI code page on Windows). Latin, CJK and an emoji (outside
// the Basic Multilingual Plane, so two UTF-16 units): "ü_写真_📷".
const std::u8string unicode_part = u8"ü_写真_\U0001F4F7";

// As in cli_test.cpp: the test's name and the process id keep `ctest -j` runs apart. The
// non-ASCII part comes before the extension, e.g. ".../prism_unicode_Test_123_ü_写真_📷.png".
fs::path temp_file(const std::string& extension) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    fs::path path = fs::path(testing::TempDir()) / ("prism_unicode_" + std::string(test->name()) +
                                                    "_" + std::to_string(pid) + "_");
    path += unicode_part;
    path += extension;
    return path;
}

// The path as UTF-8 bytes in a std::string, which is what the error messages and the command
// lines hold. u8string() is std::u8string in C++20; copying it element by element turns each
// char8_t into the char with the same value.
std::string utf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Runs `call`, which must throw std::runtime_error, and returns its message. std::system_error
// derives from std::runtime_error, so the tests check the message too: a failed conversion of
// the path (what path::string() throws on Windows) has the wrong one.
template <typename F>
std::string runtime_error_message(F call) {
    try {
        call();
    } catch (const std::runtime_error& e) {
        return e.what();
    } catch (...) {
        ADD_FAILURE() << "threw something other than std::runtime_error";
        return {};
    }
    ADD_FAILURE() << "did not throw";
    return {};
}

prism::Image make_test_image() {
    // Values that are exactly representable in 8-bit sRGB, so a PNG round trip is lossless.
    const std::vector<std::uint8_t> bytes = {0, 64, 128, 255, 10, 200, 30, 90, 150, 220, 5, 118};
    return prism::decode_srgb8(bytes, 2, 2);
}

void write_text(const fs::path& path, const std::string& text) {
    // ofstream takes the fs::path itself, so it opens the UTF-16 name on Windows.
    std::ofstream(path, std::ios::binary) << text;
}

const std::string identity_matrix = "1 0 0\n0 1 0\n0 0 1\n";

const std::string lightcraft_json = R"({"version": 1, "model": "TEST-CAM", "files": 1, )"
                                    R"("samples": 1, "matrix": [[1, 0, 0], [0, 1, 0], [0, 0, 1]]})";

// The smallest DCP prism_profile converts: one calibration (D65) with only a ColorMatrix.
std::vector<std::byte> minimal_dcp() {
    std::vector<std::pair<std::int32_t, std::int32_t>> matrix;
    for (const std::int32_t n : {8100, -2700, -500, -4400, 12200, 2400, -800, 1700, 6200}) {
        matrix.emplace_back(n, 10000);
    }
    prism_test::TiffBuilder b(false, 0x4352);
    b.ascii(50708, "TEST-CAM").srationals(50721, matrix).shorts(50778, {21});
    return b.build();
}

void write_bytes(const fs::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::string quoted(const fs::path& path) {
    return "\"" + utf8(path) + "\"";
}

struct RunResult {
    int exit_code;
    std::string output;  // stdout and stderr together
};

RunResult run(const char* exe, const std::string& args) {
    const fs::path log = temp_file(".log");
    const int exit_code = prism_test::run_command("\"" + std::string(exe) + "\" " + args + " > " +
                                                  quoted(log) + " 2>&1");
    std::ifstream file(log);
    std::stringstream text;
    text << file.rdbuf();
    file.close();
    fs::remove(log);
    return {exit_code, text.str()};
}

}  // namespace

// --- Library ------------------------------------------------------------------------------------

TEST(UnicodePath, SaveAndLoadImageRoundTrip) {
    for (const char* extension : {".png", ".jpg"}) {
        const fs::path path = temp_file(extension);
        const prism::Image original = make_test_image();
        prism::save_image(path, original);
        // fs::exists asks the OS for the real (UTF-16 on Windows) name: a file saved under a
        // mangled name would not be found.
        ASSERT_TRUE(fs::exists(path)) << utf8(path);

        const prism::Image loaded = prism::load_image(path);
        EXPECT_EQ(loaded.width, 2);
        EXPECT_EQ(loaded.height, 2);
        if (std::string(extension) == ".png") {
            EXPECT_EQ(prism::encode_srgb8(loaded), prism::encode_srgb8(original));
        }
        fs::remove(path);
    }
}

TEST(UnicodePath, ImageErrorsNameTheFile) {
    const fs::path missing = temp_file(".png");
    std::string message = runtime_error_message([&] { (void)prism::load_image(missing); });
    EXPECT_TRUE(contains(message, "cannot load image")) << message;
    EXPECT_TRUE(contains(message, utf8(missing.filename()))) << message;

    const fs::path no_dir = temp_file("") / "out.png";
    message = runtime_error_message([&] { prism::save_image(no_dir, make_test_image()); });
    EXPECT_TRUE(contains(message, "write failed")) << message;
    EXPECT_TRUE(contains(message, utf8(no_dir))) << message;

    const fs::path bad_extension = temp_file(".bmp");
    message = runtime_error_message([&] { prism::save_image(bad_extension, make_test_image()); });
    EXPECT_TRUE(contains(message, "unsupported extension")) << message;
    EXPECT_TRUE(contains(message, utf8(bad_extension.filename()))) << message;
}

TEST(UnicodePath, NonAsciiExtensionIsNotAnImage) {
    // The extension itself is non-ASCII: checking it for .png or .arw must not fail.
    fs::path path = temp_file(".");
    path += u8"写";
    const std::string message = runtime_error_message([&] { (void)prism::load_image(path); });
    EXPECT_TRUE(contains(message, "cannot load image")) << message;
}

TEST(UnicodePath, LoadMatrix) {
    const fs::path path = temp_file(".txt");
    write_text(path, identity_matrix);
    const prism::Matrix3 m = prism::load_matrix(path);
    EXPECT_EQ(m, (prism::Matrix3{1, 0, 0, 0, 1, 0, 0, 0, 1}));

    write_text(path, "1 0 0\n0 1\n");
    std::string message = runtime_error_message([&] { (void)prism::load_matrix(path); });
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
    fs::remove(path);

    message = runtime_error_message([&] { (void)prism::load_matrix(path); });
    EXPECT_TRUE(contains(message, "cannot open matrix file")) << message;
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
}

TEST(UnicodePath, LoadLightCraftProfile) {
    const fs::path path = temp_file(".json");
    write_text(path, lightcraft_json);
    const prism::LightCraftProfile p = prism::load_lightcraft_profile(path);
    EXPECT_EQ(p.model, "TEST-CAM");

    write_text(path, "{");
    std::string message =
        runtime_error_message([&] { (void)prism::load_lightcraft_profile(path); });
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
    fs::remove(path);

    message = runtime_error_message([&] { (void)prism::load_lightcraft_profile(path); });
    EXPECT_TRUE(contains(message, "cannot open profile")) << message;
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
}

TEST(UnicodePath, LoadDngProfile) {
    const fs::path path = temp_file(".dcp");
    write_bytes(path, minimal_dcp());
    const prism::DngProfile p = prism::load_dng_profile(path);
    EXPECT_EQ(p.source, prism::DngProfile::Source::dcp);
    EXPECT_EQ(p.unique_camera_model, "TEST-CAM");

    write_text(path, "not a TIFF file");
    std::string message = runtime_error_message([&] { (void)prism::load_dng_profile(path); });
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
    fs::remove(path);

    message = runtime_error_message([&] { (void)prism::load_dng_profile(path); });
    EXPECT_TRUE(contains(message, "cannot open DNG profile")) << message;
    EXPECT_TRUE(contains(message, utf8(path.filename()))) << message;
}

// --- Command-line tools -------------------------------------------------------------------------

TEST(UnicodePath, PrismReadsAndWritesNonAsciiPaths) {
    const fs::path input = temp_file(".png");
    const fs::path matrix = temp_file(".txt");
    const fs::path output = temp_file(".jpg");
    const fs::path hist = temp_file("_hist.png");
    prism::save_image(input, make_test_image());
    write_text(matrix, identity_matrix);

    const RunResult r = run(PRISM_EXE, quoted(input) + " --matrix " + quoted(matrix) + " -o " +
                                           quoted(output) + " --histogram " + quoted(hist));
    EXPECT_EQ(r.exit_code, 0) << r.output;
    EXPECT_TRUE(fs::exists(output)) << r.output;
    EXPECT_TRUE(fs::exists(hist)) << r.output;
    EXPECT_TRUE(contains(r.output, "wrote " + utf8(output))) << r.output;

    // A missing input names the file in the error.
    fs::remove(input);
    const RunResult missing = run(PRISM_EXE, quoted(input) + " -o " + quoted(output));
    EXPECT_EQ(missing.exit_code, 1) << missing.output;
    EXPECT_TRUE(contains(missing.output, utf8(input.filename()))) << missing.output;

    for (const fs::path& p : {matrix, output, hist}) {
        fs::remove(p);
    }
}

TEST(UnicodePath, PrismProfileReadsAndWritesNonAsciiPaths) {
    const fs::path json = temp_file(".json");
    const fs::path dcp = temp_file(".dcp");
    const fs::path output = temp_file(".txt");
    write_text(json, lightcraft_json);
    write_bytes(dcp, minimal_dcp());

    for (const fs::path& input : {json, dcp}) {
        const RunResult r = run(PRISM_PROFILE_EXE, quoted(input) + " -o " + quoted(output));
        EXPECT_EQ(r.exit_code, 0) << r.output;
        EXPECT_TRUE(contains(r.output, "wrote " + utf8(output))) << r.output;
        ASSERT_TRUE(fs::exists(output)) << r.output;
        EXPECT_NO_THROW((void)prism::load_matrix(output)) << utf8(input.filename());
        fs::remove(output);
    }
    fs::remove(json);
    fs::remove(dcp);
}
