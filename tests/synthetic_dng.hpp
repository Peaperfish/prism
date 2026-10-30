#pragma once

// A test helper: a tiny DNG that LibRaw decodes, built in memory, so the RAW tests need no camera
// file (none is checked in). Not part of Prism.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "tiff_builder.hpp"

namespace prism_test {

// The smallest image LibRaw accepts: its identify() rejects anything under 22 x 22 pixels.
inline constexpr int synthetic_dng_size = 32;

// An uncompressed, little-endian, 16-bit Bayer (RGGB) DNG of synthetic_dng_size squared pixels,
// every photosite at `level` (black 0, white 65535). The colour matrix says the camera's RGB *is*
// linear sRGB and the as-shot white balance is neutral, so LibRaw should decode it as a uniform
// grey of level / 65535 in every channel.
inline std::vector<std::byte> synthetic_dng(std::uint16_t level) {
    constexpr int n = synthetic_dng_size;
    constexpr std::uint32_t data_size = n * n * 2;

    // ColorMatrix1 maps XYZ to camera RGB; here that is the XYZ -> linear sRGB (D65) matrix of
    // IEC 61966-2-1, in units of 1/10000.
    const std::vector<std::pair<std::int32_t, std::int32_t>> xyz_to_srgb = {
        {32406, 10000}, {-15372, 10000}, {-4986, 10000}, {-9689, 10000}, {18758, 10000},
        {415, 10000},   {557, 10000},    {-2040, 10000}, {10570, 10000}};

    // The pixel data goes after the TIFF structure, at an offset only known once that is built:
    // build it once to measure it (StripOffsets is one inline LONG, so its value doesn't change
    // the size), then again with the real offset.
    auto build = [&](std::uint32_t strip_offset) {
        TiffBuilder b(false);
        b.longs(254, {0})                // NewSubFileType: the main (full-size) image
            .longs(256, {n})             // ImageWidth
            .longs(257, {n})             // ImageLength
            .shorts(258, {16})           // BitsPerSample
            .shorts(259, {1})            // Compression: none
            .shorts(262, {32803})        // PhotometricInterpretation: CFA (colour filter array)
            .ascii(271, "Prism")         // Make
            .ascii(272, "Synthetic")     // Model
            .longs(273, {strip_offset})  // StripOffsets
            .shorts(274, {1})            // Orientation: normal
            .shorts(277, {1})            // SamplesPerPixel
            .longs(278, {n})             // RowsPerStrip: one strip
            .longs(279, {data_size})     // StripByteCounts
            .shorts(284, {1})            // PlanarConfiguration: chunky
            .shorts(33421, {2, 2})       // CFARepeatPatternDim
            .bytes(33422, TiffType::byte, {0, 1, 1, 2})  // CFAPattern: R G / G B
            .bytes(50706, TiffType::byte, {1, 4, 0, 0})  // DNGVersion 1.4
            .bytes(50707, TiffType::byte, {1, 1, 0, 0})  // DNGBackwardVersion 1.1
            .ascii(50708, "Prism Synthetic")             // UniqueCameraModel
            .shorts(50714, {0})                          // BlackLevel
            .longs(50717, {65535})                       // WhiteLevel
            .srationals(50721, xyz_to_srgb)              // ColorMatrix1
            .rationals(50728, {{1, 1}, {1, 1}, {1, 1}})  // AsShotNeutral
            .shorts(50778, {21});                        // CalibrationIlluminant1: D65
        return b.build();
    };
    const std::size_t header_size = build(0).size();
    std::vector<std::byte> file = build(static_cast<std::uint32_t>(header_size));
    file.reserve(file.size() + data_size);
    for (int i = 0; i < n * n; ++i) {
        file.push_back(static_cast<std::byte>(level & 0xff));  // little-endian
        file.push_back(static_cast<std::byte>(level >> 8));
    }
    return file;
}

}  // namespace prism_test
