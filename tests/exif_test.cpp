#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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
