#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "prism/gain_map.hpp"
#include "prism/image.hpp"

namespace prism {

// --- Ultra HDR JPEG ------------------------------------------------------------------------------
//
// An Ultra HDR JPEG is an ordinary JPEG (the SDR picture, the "primary" image, which every viewer
// shows) followed by a second JPEG holding the gain map (see prism/gain_map.hpp), which an HDR
// viewer uses to brighten the SDR picture. Prism writes both kinds of gain-map metadata: Adobe's
// hdrgm XMP (what the Ultra HDR v1.1 format and Chrome before 137 read) and ISO 21496-1 (what
// ImageIO, Safari 26 and newer Chrome read). Both JPEGs are stb_image_write's own output; Prism
// adds only marker segments around them, so the primary decodes to exactly the pixels of the
// plain JPEG save_image would write.
//
// The file, in order:
//   primary:   SOI
//              APP0  JFIF (stb's)
//              APP1  XMP: hdrgm:Version and the GContainer directory (Primary, GainMap and the gain
//                    map's length in bytes)
//              APP2  ISO 21496-1 version (minimum and writer version 0)
//              APP2  ICC profile (srgb_icc_profile(), in one chunk)
//              APP2  MPF: the two images' sizes, and the gain map's offset counted from the start
//                    of the MPF segment's TIFF header (8 bytes into the segment)
//              DQT, SOF0, DHT, SOS ... EOI (stb's)
//   gain map:  SOI
//              APP1  XMP: hdrgm metadata
//              APP2  ISO 21496-1 metadata (one channel, in the base picture's colour space)
//              stb's JPEG after its SOI
// The MPF segment comes before DQT: ImageIO ignores the hdrgm metadata when MPF comes after the
// tables (spike "Ultra HDR JPEG layout", #98).
//
// This product includes Gain Map technology under license by Adobe.

// The JPEG quality of the gain map image (the SDR picture keeps save_image's 95). Chosen in #104 as
// the smallest quality whose reconstructed HDR picture (weight 1) stays within 0.05 stops of
// process_hdr's at the 99.9th percentile of pixels, on the test RAW under Natural with N = 2: 97
// gives 0.048 stops at full size and 0.046 at a long side of 2048 (96 gives 0.061 and 0.058, 95
// gives 0.075 and 0.070). Its cost there: the file is 64% (full size) to 104% (2048) larger than
// the plain JPEG.
inline constexpr int gain_map_jpeg_quality = 97;

// The sRGB ICC profile every primary image carries: "sRGB-v2-magic" from Compact-ICC-Profiles
// (CC0 1.0), 736 bytes. See lib/srgb_icc.cpp and third_party/icc/.
[[nodiscard]] std::span<const std::uint8_t> srgb_icc_profile() noexcept;

// Wraps two JPEGs as stb_image_write writes them (SOI, APP0 JFIF, then tables, SOS ... EOI, and
// no other APPn segment) into an Ultra HDR JPEG with `metadata`, in the layout above. `base_jpeg`
// is the SDR picture, `gain_map_jpeg` the gain map encoded as a grey JPEG. stb writes a grey
// picture as three-component YCbCr with neutral chroma: libultrahdr 2.x decodes it as RGB with
// equal channels and ImageIO, Chrome and libultrahdr all read it (spike #98); older Android
// decoders are unchecked. Throws std::invalid_argument if either is not such a JPEG, if a metadata
// value can't be written as an ISO 21496-1 fraction or as XMP, if a segment would pass 64 KB, or
// if the file would pass 4 GB.
[[nodiscard]] std::vector<std::uint8_t> assemble_ultra_hdr_jpeg(
    std::span<const std::uint8_t> base_jpeg, std::span<const std::uint8_t> gain_map_jpeg,
    const GainMapMetadata& metadata);

struct UltraHdrJpeg {
    std::vector<std::uint8_t> bytes;  // the whole file
    GainMap gain_map;                 // the map it holds, as make_gain_map made it
};

// The Ultra HDR JPEG of an SDR picture and its HDR picture, both width x height:
// - `sdr_srgb8`: the SDR picture as 8-bit sRGB (encode_srgb8 of process_hdr's `img`);
// - `hdr`: the HDR picture (process_hdr's `hdr`);
// - `headroom_stops`: N, as for make_gain_map (HDRCapacityMax = N).
// The SDR picture is encoded as save_image does (stb, quality 95), then decoded again, and the
// gain map is made from those decoded pixels, since that is what a viewer starts from. The map is
// encoded at gain_map_jpeg_quality. Throws std::invalid_argument if the sizes disagree, as
// make_gain_map and assemble_ultra_hdr_jpeg do, or if the SDR JPEG passes 2 GB; and
// std::runtime_error if stb fails to encode the picture or to decode it again.
[[nodiscard]] UltraHdrJpeg encode_ultra_hdr_jpeg(std::span<const std::uint8_t> sdr_srgb8,
                                                 const Image& hdr, float headroom_stops,
                                                 const GainMapOptions& options = {},
                                                 int threads = 0);

// encode_ultra_hdr_jpeg, written to `path` atomically (write_file_atomically): a failed write
// leaves any file already at `path` as it was. Throws std::runtime_error naming the file when it
// can't be written, and otherwise as encode_ultra_hdr_jpeg does (those messages don't name it).
void save_ultra_hdr_jpeg(const std::filesystem::path& path, std::span<const std::uint8_t> sdr_srgb8,
                         const Image& hdr, float headroom_stops, int threads = 0);

}  // namespace prism
