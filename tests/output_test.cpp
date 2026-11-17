#include "prism/output.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "prism/pipeline.hpp"
#include "prism/ultra_hdr.hpp"

namespace fs = std::filesystem;

using prism::OutputFile;

namespace {

fs::path temp_path(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
    return fs::path(testing::TempDir()) / ("prism_output_" + std::string(test->name()) + "_" + name);
}

// Removes a file when the test ends, however it ends.
struct RemoveOnExit {
    fs::path path;
    ~RemoveOnExit() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::vector<char> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    return bytes;
}

// 37 x 29 light from black to past white, so the HDR branch has something above white.
prism::Image ramp() {
    prism::Image img;
    img.width = 37;
    img.height = 29;
    img.pixels.resize(static_cast<std::size_t>(img.width) * img.height * 3);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            float* p = &img.pixels[(static_cast<std::size_t>(y) * img.width + x) * 3];
            const float t = 2.0f * static_cast<float>(x) / static_cast<float>(img.width - 1);
            p[0] = t;
            p[1] = t * 0.5f + 0.01f * static_cast<float>(y);
            p[2] = t * t * 0.25f;
        }
    }
    return img;
}

prism::ProcessSettings settings_with(std::optional<float> headroom) {
    prism::ProcessSettings s;
    s.exposure_stops = 0.5f;
    s.curve = prism::parse_curve("filmic");
    s.headroom_stops = headroom;
    return s;
}

std::vector<std::uint32_t> bits(const prism::Image& img) {
    std::vector<std::uint32_t> out(img.pixels.size());
    std::memcpy(out.data(), img.pixels.data(), out.size() * sizeof(float));
    return out;
}

}  // namespace

TEST(OutputFile, TheExtensionAndTheHeadroomChooseIt) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const std::optional<float> sdr : {std::optional<float>{}, std::optional<float>{0.0f},
                                           std::optional<float>{-0.0f}, std::optional<float>{nan},
                                           std::optional<float>{-1.0f}}) {
        EXPECT_FALSE(prism::makes_hdr(sdr));
        EXPECT_EQ(prism::output_file("a.png", sdr), OutputFile::png);
        EXPECT_EQ(prism::output_file("a.PNG", sdr), OutputFile::png);
        EXPECT_EQ(prism::output_file("a.jpg", sdr), OutputFile::jpeg);
        EXPECT_EQ(prism::output_file("dir/a.JPEG", sdr), OutputFile::jpeg);
        EXPECT_EQ(prism::output_file("a.tif", sdr), OutputFile::unsupported);
        EXPECT_EQ(prism::output_file("a", sdr), OutputFile::unsupported);
    }
    for (const float hdr : {0.1f, 2.0f, 4.0f, std::numeric_limits<float>::denorm_min()}) {
        EXPECT_TRUE(prism::makes_hdr(hdr));
        EXPECT_EQ(prism::output_file("a.png", hdr), OutputFile::hdr_needs_jpeg);
        EXPECT_EQ(prism::output_file("a.Png", hdr), OutputFile::hdr_needs_jpeg);
        EXPECT_EQ(prism::output_file("a.jpg", hdr), OutputFile::ultra_hdr_jpeg);
        EXPECT_EQ(prism::output_file("a.JPG", hdr), OutputFile::ultra_hdr_jpeg);
        EXPECT_EQ(prism::output_file("a.jpeg", hdr), OutputFile::ultra_hdr_jpeg);
        EXPECT_EQ(prism::output_file("a.webp", hdr), OutputFile::unsupported);
    }
}

TEST(OutputFile, ProcessForOutputRunsProcessHdrOnlyForAnUltraHdrJpeg) {
    const prism::ProcessSettings s = settings_with(2.0f);
    prism::Image want_sdr = ramp();
    prism::Image want_hdr;
    ASSERT_EQ(prism::process_hdr(want_sdr, want_hdr, s, 1), prism::ProcessResult::completed);
    ASSERT_FALSE(want_hdr.pixels.empty());

    prism::Image img = ramp();
    prism::Image hdr;
    ASSERT_EQ(prism::process_for_output(img, hdr, s, OutputFile::ultra_hdr_jpeg, 1),
              prism::ProcessResult::completed);
    EXPECT_EQ(bits(img), bits(want_sdr));
    EXPECT_EQ(bits(hdr), bits(want_hdr));
    EXPECT_EQ(hdr.width, want_hdr.width);

    // Anything else is process(), whatever the headroom, and leaves `hdr` alone.
    for (const OutputFile f : {OutputFile::png, OutputFile::jpeg, OutputFile::hdr_needs_jpeg,
                               OutputFile::unsupported}) {
        prism::Image other = ramp();
        prism::Image untouched;
        untouched.width = 1;
        untouched.height = 1;
        untouched.pixels = {7.0f, 8.0f, 9.0f};
        ASSERT_EQ(prism::process_for_output(other, untouched, s, f, 1),
                  prism::ProcessResult::completed);
        EXPECT_EQ(bits(other), bits(want_sdr));
        EXPECT_EQ(untouched.pixels, (std::vector<float>{7.0f, 8.0f, 9.0f}));
    }
}

TEST(OutputFile, SaveOutputWritesTheSdrFilesAsBeforeAndTheUltraHdrJpegAtTheHeadroom) {
    prism::Image img = ramp();
    prism::Image hdr;
    ASSERT_EQ(prism::process_hdr(img, hdr, settings_with(1.5f), 1),
              prism::ProcessResult::completed);
    const std::vector<std::uint8_t> srgb8 = prism::encode_srgb8(img, 1);

    for (const char* name : {"a.png", "a.jpg"}) {
        const RemoveOnExit want{temp_path(std::string("want_") + name)};
        const RemoveOnExit got{temp_path(name)};
        prism::save_image(want.path, img, 1);
        for (const std::optional<float> headroom :
             {std::optional<float>{}, std::optional<float>{0.0f}}) {
            prism::save_output(got.path, img, hdr, headroom, 1);
            EXPECT_EQ(read_file(got.path), read_file(want.path)) << name;
            fs::remove(got.path);
            prism::save_output(got.path, img, hdr, headroom, 1, srgb8);
            EXPECT_EQ(read_file(got.path), read_file(want.path)) << name << " from bytes";
            fs::remove(got.path);
        }
    }

    const prism::UltraHdrJpeg want = prism::encode_ultra_hdr_jpeg(srgb8, hdr, 1.5f, {}, 1);
    const std::vector<char> want_bytes(want.bytes.begin(), want.bytes.end());
    const RemoveOnExit got{temp_path("hdr.JPEG")};
    prism::save_output(got.path, img, hdr, 1.5f, 1);
    EXPECT_TRUE(read_file(got.path) == want_bytes);
    fs::remove(got.path);
    prism::save_output(got.path, img, hdr, 1.5f, 1, srgb8);
    EXPECT_TRUE(read_file(got.path) == want_bytes);
}

TEST(OutputFile, SaveOutputRefusesAPngWithAHeadroomAndWritesNothing) {
    prism::Image img = ramp();
    prism::Image hdr;
    ASSERT_EQ(prism::process_hdr(img, hdr, settings_with(2.0f), 1),
              prism::ProcessResult::completed);
    const RemoveOnExit png{temp_path("hdr.png")};
    fs::remove(png.path);
    try {
        prism::save_output(png.path, img, hdr, 2.0f, 1);
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("HDR files need .jpg"), std::string::npos)
            << e.what();
    }
    EXPECT_FALSE(fs::exists(png.path));
    const RemoveOnExit tif{temp_path("x.tif")};
    EXPECT_THROW(prism::save_output(tif.path, img, hdr, 2.0f, 1), std::runtime_error);
    EXPECT_THROW(prism::save_output(tif.path, img, hdr, std::nullopt, 1), std::runtime_error);
    EXPECT_FALSE(fs::exists(tif.path));
}
