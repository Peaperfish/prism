#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/image.hpp"
#include "prism/photo_info.hpp"
#include "synthetic_exif.hpp"

using prism::PhotoInfo;
using prism::read_exif;
using prism_test::ExifBuilder;

namespace {

// Every tag read_exif knows, with distinct values.
ExifBuilder full_block(bool big_endian) {
    ExifBuilder b(big_endian);
    b.ifd0_ascii(0x010F, "SONY")
        .ifd0_ascii(0x0110, "ILCE-7RM5  ")  // trailing spaces are trimmed
        .ifd0_short(0x0112, 6)
        .exif_rational(0x829A, 1, 250)
        .exif_rational(0x829D, 28, 10)
        .exif_short(0x8827, 3200)
        .exif_ascii(0x9003, "2024:05:06 07:08:09")
        .exif_rational(0x920A, 350, 10)
        .exif_short(0xA405, 52)
        .exif_ascii(0xA434, "FE 24-70mm F2.8 GM II");
    return b;
}

void expect_full(const PhotoInfo& info) {
    EXPECT_EQ(info.make, "SONY");
    EXPECT_EQ(info.model, "ILCE-7RM5");
    EXPECT_EQ(info.orientation, 6);
    ASSERT_TRUE(info.exposure_seconds);
    EXPECT_DOUBLE_EQ(*info.exposure_seconds, 0.004);
    ASSERT_TRUE(info.f_number);
    EXPECT_DOUBLE_EQ(*info.f_number, 2.8);
    EXPECT_EQ(info.iso, 3200);
    EXPECT_EQ(info.date_taken, "2024:05:06 07:08:09");
    ASSERT_TRUE(info.focal_length_mm);
    EXPECT_DOUBLE_EQ(*info.focal_length_mm, 35.0);
    EXPECT_EQ(info.focal_length_35mm, 52);
    EXPECT_EQ(info.lens, "FE 24-70mm F2.8 GM II");
}

}  // namespace

TEST(ReadExif, AllTagsLittleEndian) {
    expect_full(read_exif(full_block(false).build()));
}

TEST(ReadExif, AllTagsBigEndian) {
    expect_full(read_exif(full_block(true).build()));
}

TEST(ReadExif, MissingExifIfdKeepsIfd0) {
    ExifBuilder b(false);
    b.ifd0_ascii(0x010F, "Canon").ifd0_short(0x0112, 3);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Canon");
    EXPECT_EQ(info.orientation, 3);
    EXPECT_FALSE(info.iso);
    EXPECT_FALSE(info.lens);
}

TEST(ReadExif, EmptyIfdsAreFine) {
    EXPECT_TRUE(read_exif(ExifBuilder(false).build()).empty());
}

TEST(ReadExif, OrientationOutOfRangeIsAbsent) {
    for (const std::uint16_t bad : {0, 9, 65535}) {
        ExifBuilder b(true);
        b.ifd0_ascii(0x010F, "X").ifd0_short(0x0112, bad);
        const PhotoInfo info = read_exif(b.build());
        EXPECT_FALSE(info.orientation) << bad;
        EXPECT_EQ(info.make, "X");
    }
}

TEST(ReadExif, DuplicateTagFirstWins) {
    ExifBuilder b(false);
    b.ifd0_short(0x0112, 3).ifd0_short(0x0112, 6);
    EXPECT_EQ(read_exif(b.build()).orientation, 3);
}

TEST(ReadExif, ZeroMeansUnknown) {
    ExifBuilder b(false);
    b.exif_short(0x8827, 0).exif_rational(0x829A, 0, 1).exif_rational(0x829D, 1, 0);
    EXPECT_TRUE(read_exif(b.build()).empty());
}

TEST(ReadExif, ExifPointerPastEndKeepsIfd0) {
    ExifBuilder b(false);
    b.ifd0_ascii(0x010F, "Nikon").exif_pointer(100000);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Nikon");
    EXPECT_FALSE(info.iso);
}

TEST(ReadExif, ExifPointerToIfd0Stops) {
    ExifBuilder b(false);
    b.ifd0_ascii(0x010F, "Nikon").ifd0_short(0x8827, 100).exif_pointer(8);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Nikon");
    EXPECT_FALSE(info.iso);  // the pointer is not followed back into IFD 0
}

TEST(ReadExif, ExifPointerIntoHeaderKeepsIfd0) {
    ExifBuilder b(false);
    b.ifd0_ascii(0x010F, "Nikon").exif_pointer(4);
    EXPECT_EQ(read_exif(b.build()).make, "Nikon");
}

TEST(ReadExif, HugeCountDropsOnlyThatEntry) {
    ExifBuilder b(false);
    prism_test::ExifEntry bad{0x010F, 2, 0xFFFFFFFFu, {}, 20u};
    b.ifd0_entry(bad).ifd0_short(0x0112, 8);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_FALSE(info.make);
    EXPECT_EQ(info.orientation, 8);
}

TEST(ReadExif, UnknownTypeDropsOnlyThatEntry) {
    ExifBuilder b(false);
    prism_test::ExifEntry bad{0x0100, 99, 1, {}, 0u};
    b.ifd0_ascii(0x010F, "Pentax").ifd0_entry(bad).ifd0_short(0x0112, 6);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Pentax");
    EXPECT_EQ(info.orientation, 6);
}

TEST(ReadExif, UnknownTypeInExifIfdKeepsIso) {
    ExifBuilder b(false);
    prism_test::ExifEntry bad{0x9000, 99, 1, {}, 0u};
    b.ifd0_ascii(0x010F, "Pentax").exif_entry(bad).exif_short(0x8827, 400);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Pentax");
    EXPECT_EQ(info.iso, 400);
}

TEST(ReadExif, HugeLongCountDropsOnlyThatEntry) {
    ExifBuilder b(false);
    prism_test::ExifEntry bad{0x0100, 4, 0x40000000u, {}, 20u};
    b.ifd0_ascii(0x010F, "Pentax").ifd0_entry(bad).ifd0_short(0x0112, 6);
    const PhotoInfo info = read_exif(b.build());
    EXPECT_EQ(info.make, "Pentax");
    EXPECT_EQ(info.orientation, 6);
}

TEST(ReadExif, WrongTypesAreIgnored) {
    ExifBuilder b(false);
    b.ifd0_short(0x010F, 5)         // Make as a SHORT
        .ifd0_ascii(0x0112, "6")    // Orientation as text
        .exif_ascii(0x8827, "100")  // ISO as text
        .exif_short(0x829A, 1);     // ExposureTime as a SHORT
    EXPECT_TRUE(read_exif(b.build()).empty());
}

TEST(ReadExif, BadByteOrderMarkOrMagic) {
    std::vector<std::uint8_t> bytes = full_block(false).build();
    std::vector<std::uint8_t> bad_mark = bytes;
    bad_mark[0] = 'X';
    EXPECT_TRUE(read_exif(bad_mark).empty());
    std::vector<std::uint8_t> mixed = bytes;
    mixed[1] = 'M';
    EXPECT_TRUE(read_exif(mixed).empty());
    std::vector<std::uint8_t> bad_magic = bytes;
    bad_magic[2] = 43;
    EXPECT_TRUE(read_exif(bad_magic).empty());
}

TEST(ReadExif, TruncatedAtEveryLengthNeverThrows) {
    for (const bool big : {false, true}) {
        const std::vector<std::uint8_t> bytes = full_block(big).build();
        for (std::size_t n = 0; n <= bytes.size(); ++n) {
            const std::span<const std::uint8_t> part(bytes.data(), n);
            EXPECT_NO_THROW((void)read_exif(part)) << n;
        }
    }
}

TEST(ReadExif, TruncationKeepsWhatWasRead) {
    const std::vector<std::uint8_t> bytes = full_block(false).build();
    // Cut inside the Exif IFD: IFD 0's make, model and orientation are intact.
    const PhotoInfo info = read_exif(std::span(bytes.data(), bytes.size() - 20));
    EXPECT_EQ(info.make, "SONY");
    EXPECT_EQ(info.orientation, 6);
}

TEST(ReadExif, EveryByteCorruptedNeverThrows) {
    for (const bool big : {false, true}) {
        const std::vector<std::uint8_t> bytes = full_block(big).build();
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            for (const std::uint8_t v : {std::uint8_t{0}, std::uint8_t{0xFF}, std::uint8_t{0x80}}) {
                std::vector<std::uint8_t> copy = bytes;
                copy[i] = v;
                EXPECT_NO_THROW((void)read_exif(copy)) << i;
            }
        }
    }
}

TEST(ReadExif, EmptyAndTinyBuffers) {
    EXPECT_TRUE(read_exif({}).empty());
    const std::uint8_t tiny[] = {'I', 'I', 42, 0};
    EXPECT_TRUE(read_exif(tiny).empty());
}

// --- read_jpeg_exif ---

namespace {

namespace fs = std::filesystem;
using prism_test::exif_app1;
using prism_test::jpeg_segment;
using prism_test::quadrant_jpeg;
using prism_test::xmp_app1;

// A file name no other test (or test process: ctest -j) uses.
fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_exif_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// Writes `jpeg` to a temporary file and reads its EXIF block back.
std::optional<std::vector<std::uint8_t>> exif_of(const std::vector<std::uint8_t>& jpeg) {
    const fs::path path = temp_file("a.jpg");
    prism_test::write_all(path, jpeg);
    auto result = prism::read_jpeg_exif(path);
    std::error_code ec;
    fs::remove(path, ec);
    return result;
}

std::vector<std::uint8_t> sample_jpeg(const std::vector<std::vector<std::uint8_t>>& segments) {
    return quadrant_jpeg(temp_file("scratch.jpg"), segments);
}

}  // namespace

TEST(ReadJpegExif, FindsTheBlock) {
    const std::vector<std::uint8_t> tiff = full_block(true).build();
    const auto got = exif_of(sample_jpeg({exif_app1(tiff)}));
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, tiff);
    expect_full(read_exif(*got));
}

TEST(ReadJpegExif, SkipsXmpApp1BeforeExif) {
    const std::vector<std::uint8_t> tiff = full_block(false).build();
    const auto got = exif_of(sample_jpeg({xmp_app1(), exif_app1(tiff)}));
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, tiff);
}

TEST(ReadJpegExif, SkipsOtherSegmentsFirst) {
    const std::vector<std::uint8_t> tiff = full_block(false).build();
    const auto got = exif_of(sample_jpeg({jpeg_segment(0xE0, {'J', 'F', 'I', 'F', 0, 1, 1}),
                                          jpeg_segment(0xE2, {1, 2, 3}), exif_app1(tiff)}));
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, tiff);
}

TEST(ReadJpegExif, NoApp1) {
    EXPECT_FALSE(exif_of(sample_jpeg({})));
    EXPECT_FALSE(exif_of(sample_jpeg({xmp_app1()})));
}

TEST(ReadJpegExif, FillBytesBeforeMarkers) {
    const std::vector<std::uint8_t> tiff = full_block(false).build();
    std::vector<std::uint8_t> fill = {0xFF, 0xFF, 0xFF};
    std::vector<std::uint8_t> seg = exif_app1(tiff);
    fill.insert(fill.end(), seg.begin(), seg.end());  // FF FF FF FF E1 ...
    const auto got = exif_of(sample_jpeg({fill}));
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, tiff);
}

TEST(ReadJpegExif, StandaloneMarkersHaveNoLength) {
    const std::vector<std::uint8_t> tiff = full_block(false).build();
    // RST0 and TEM before the APP1: reading a length after them would swallow the APP1.
    const auto got = exif_of(sample_jpeg({{0xFF, 0xD0}, {0xFF, 0x01}, exif_app1(tiff)}));
    ASSERT_TRUE(got);
    EXPECT_EQ(*got, tiff);
}

TEST(ReadJpegExif, ExifAfterTheImageDataIsNotReached) {
    // Stops at SOS: a block placed after it is never reached.
    std::vector<std::uint8_t> jpeg = sample_jpeg({});
    const std::vector<std::uint8_t> seg = exif_app1(full_block(false).build());
    jpeg.insert(jpeg.end() - 2, seg.begin(), seg.end());  // before EOI, after the scan
    EXPECT_FALSE(exif_of(jpeg));
}

TEST(ReadJpegExif, SegmentLengthPastEndOfFile) {
    std::vector<std::uint8_t> seg = exif_app1(full_block(false).build());
    seg[2] = 0xFF;  // length 0xFFxx
    EXPECT_FALSE(exif_of(sample_jpeg({seg})));
    // Cut the file in the middle of the APP1.
    std::vector<std::uint8_t> jpeg = {0xFF, 0xD8};
    const std::vector<std::uint8_t> good = exif_app1(full_block(false).build());
    jpeg.insert(jpeg.end(), good.begin(), good.begin() + good.size() / 2);
    EXPECT_FALSE(exif_of(jpeg));
}

TEST(ReadJpegExif, NotAJpegOrMissingFile) {
    EXPECT_FALSE(exif_of({}));
    EXPECT_FALSE(exif_of({'P', 'N', 'G', 0, 0, 0, 0, 0}));
    EXPECT_FALSE(prism::read_jpeg_exif(temp_file("does_not_exist.jpg")));
}

TEST(ReadJpegExif, TruncatedAtEveryLengthNeverThrows) {
    const std::vector<std::uint8_t> jpeg =
        sample_jpeg({xmp_app1(), exif_app1(full_block(false).build())});
    const fs::path path = temp_file("cut.jpg");
    for (std::size_t n = 0; n < 300 && n < jpeg.size(); ++n) {
        prism_test::write_all(path, std::vector<std::uint8_t>(jpeg.begin(), jpeg.begin() + n));
        EXPECT_NO_THROW((void)prism::read_jpeg_exif(path)) << n;
    }
    std::error_code ec;
    fs::remove(path, ec);
}

// --- Loading: the orientation is applied, the PhotoInfo comes with the image ---

namespace {

// The colour (index into quadrant_rgb: 0 red, 1 green, 2 blue, 3 yellow) expected in the top-left,
// top-right, bottom-left and bottom-right of the loaded picture, for each Orientation 1-8. The
// stored picture is   red   green
//                     blue  yellow
constexpr int expected_quadrants[9][4] = {
    {0, 0, 0, 0},  // (unused: orientations start at 1)
    {0, 1, 2, 3},  // 1 as stored
    {1, 0, 3, 2},  // 2 mirrored left-right
    {3, 2, 1, 0},  // 3 turned 180 degrees
    {2, 3, 0, 1},  // 4 mirrored top-bottom
    {0, 2, 1, 3},  // 5 transposed
    {2, 0, 3, 1},  // 6 turned 90 degrees clockwise
    {3, 1, 2, 0},  // 7 transversed
    {1, 3, 0, 2},  // 8 turned 90 degrees counter-clockwise
};

// The 8-bit sRGB colour at the middle of quadrant q (0..3 = TL, TR, BL, BR) of `img`.
std::array<int, 3> quadrant_centre(const prism::Image& img, int q) {
    const int x = (q % 2 == 0 ? 1 : 3) * img.width / 4;
    const int y = (q < 2 ? 1 : 3) * img.height / 4;
    const std::vector<std::uint8_t> bytes = prism::encode_srgb8(img);
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 3;
    return {bytes[i], bytes[i + 1], bytes[i + 2]};
}

void expect_quadrants(const prism::Image& img, const int (&expected)[4]) {
    for (int q = 0; q < 4; ++q) {
        const std::array<int, 3> got = quadrant_centre(img, q);
        for (int c = 0; c < 3; ++c) {
            // JPEG is lossy: a solid block's middle is within a few levels of its colour.
            EXPECT_NEAR(got[c], prism_test::quadrant_rgb[expected[q]][c], 12)
                << "quadrant " << q << " channel " << c;
        }
    }
}

}  // namespace

TEST(LoadPhoto, EveryOrientationLandsTheQuadrantsWhereTheyBelong) {
    for (int o = 1; o <= 8; ++o) {
        SCOPED_TRACE("orientation " + std::to_string(o));
        const fs::path path = temp_file("o" + std::to_string(o) + ".jpg");
        prism_test::write_oriented_jpeg(path, o);

        const prism::Photo photo = prism::load_photo(path);
        const bool turned = o >= 5;
        EXPECT_EQ(photo.image.width,
                  turned ? prism_test::quadrant_height : prism_test::quadrant_width);
        EXPECT_EQ(photo.image.height,
                  turned ? prism_test::quadrant_width : prism_test::quadrant_height);
        expect_quadrants(photo.image, expected_quadrants[o]);
        EXPECT_EQ(photo.info.orientation, o);

        // load_image is the same picture.
        const prism::Image image = prism::load_image(path);
        EXPECT_EQ(image.width, photo.image.width);
        EXPECT_EQ(image.pixels, photo.image.pixels);
        std::error_code ec;
        fs::remove(path, ec);
    }
}

TEST(LoadPhoto, AnUppercaseExtensionIsStillAJpeg) {
    const fs::path path = temp_file("upper.JPG");
    prism_test::write_oriented_jpeg(path, 6);
    const prism::Photo photo = prism::load_photo(path);
    EXPECT_EQ(photo.image.width, prism_test::quadrant_height);
    std::error_code ec;
    fs::remove(path, ec);
}

TEST(LoadPhoto, TheWholeBlockComesWithTheImage) {
    const fs::path path = temp_file("full.jpg");
    prism_test::write_all(path, sample_jpeg({xmp_app1(), exif_app1(full_block(true).build())}));
    const prism::Photo photo = prism::load_photo(path);
    expect_full(photo.info);
    EXPECT_EQ(photo.image.width, prism_test::quadrant_height);  // Orientation 6 in full_block
    std::error_code ec;
    fs::remove(path, ec);
}

TEST(LoadPhoto, BrokenExifStillLoadsUnrotatedWithEmptyInfo) {
    const std::vector<std::uint8_t> good = full_block(false).build();
    std::vector<std::uint8_t> bad_mark = good;
    bad_mark[0] = 'Z';
    const std::vector<std::uint8_t> garbage(40, 0xAB);
    const std::vector<std::uint8_t> too_short = {'I', 'I', 42, 0};
    const std::vector<std::vector<std::uint8_t>> blocks = {bad_mark, garbage, too_short, {}};
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        SCOPED_TRACE("block " + std::to_string(i));
        const fs::path path = temp_file("broken" + std::to_string(i) + ".jpg");
        prism_test::write_all(path, sample_jpeg({exif_app1(blocks[i])}));
        const prism::Photo photo = prism::load_photo(path);
        EXPECT_TRUE(photo.info.empty());
        EXPECT_EQ(photo.image.width, prism_test::quadrant_width);
        EXPECT_EQ(photo.image.height, prism_test::quadrant_height);
        expect_quadrants(photo.image, expected_quadrants[1]);
        std::error_code ec;
        fs::remove(path, ec);
    }
}

TEST(LoadPhoto, AJpegWithoutExifAndAPngHaveEmptyInfo) {
    const fs::path jpg = temp_file("plain.jpg");
    prism_test::write_all(jpg, sample_jpeg({}));
    EXPECT_TRUE(prism::load_photo(jpg).info.empty());

    prism::Image img;
    img.width = 4;
    img.height = 2;
    img.pixels.assign(4 * 2 * 3, 0.5f);
    const fs::path png = temp_file("plain.png");
    prism::save_image(png, img);
    const prism::Photo photo = prism::load_photo(png);
    EXPECT_TRUE(photo.info.empty());
    EXPECT_EQ(photo.image.width, 4);
    std::error_code ec;
    fs::remove(jpg, ec);
    fs::remove(png, ec);
}

TEST(LoadPhoto, AMissingFileStillThrows) {
    EXPECT_THROW((void)prism::load_photo(temp_file("missing.jpg")), std::runtime_error);
}
