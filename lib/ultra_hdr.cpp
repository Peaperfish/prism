// Ultra HDR JPEG writer (#104): the layout of the spike "Ultra HDR JPEG layout" (#98), written
// around stb_image_write's own JPEGs. See prism/ultra_hdr.hpp for the file layout.
//
// Formats followed:
// - Ultra HDR image format v1.1 (https://developer.android.com/media/platform/hdr-image-format):
//   the GContainer XMP in the primary image, the hdrgm XMP in the gain map, MPF.
// - Adobe's Gain Map specification (the hdrgm XMP namespace,
// http://ns.adobe.com/hdr-gain-map/1.0/).
//   This product includes Gain Map technology under license by Adobe.
// - ISO 21496-1 gain-map metadata, as libultrahdr 2.0.2 writes it (lib/src/gainmapmetadata.cpp,
//   encodeGainmapMetadata; the spike's ISO-only file was byte-identical to libultrahdr's).
// - CIPA DC-007 Multi-Picture Format, as libultrahdr writes it (lib/src/multipictureformat.cpp).
// Two differences from libultrahdr 2.0.2's own files, both on purpose: its primary has the ICC
// profile before the ISO version segment (the spec's Figure 2 puts ISO right after the XMP, which
// Prism follows), and its MPF comes after DQT/SOF/DHT, just before SOS, where ImageIO ignores the
// hdrgm metadata; Prism's MPF ends the APP block (spike #98).

#include "prism/ultra_hdr.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include "prism/atomic_file.hpp"
#include "prism/utf8_path.hpp"
#include "stb_jpeg.hpp"

namespace prism {

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t marker_soi = 0xD8;
constexpr std::uint8_t marker_eoi = 0xD9;
constexpr std::uint8_t marker_sos = 0xDA;
constexpr std::uint8_t marker_app0 = 0xE0;
constexpr std::uint8_t marker_app1 = 0xE1;
constexpr std::uint8_t marker_app2 = 0xE2;
constexpr std::uint8_t marker_app15 = 0xEF;

// Namespaces that open an APPn payload, each followed by a 0 byte.
constexpr std::string_view xmp_namespace = "http://ns.adobe.com/xap/1.0/";
constexpr std::string_view iso_namespace = "urn:iso:std:iso:ts:21496:-1";
constexpr std::string_view icc_signature = "ICC_PROFILE";
constexpr std::string_view mpf_signature = "MPF";

// ISO 21496-1 flags byte: bit 6 says the gain map applies in the base picture's colour space; bit
// 7 (several channels) is clear, as Prism's map has one; bit 3 says every value shares one
// denominator.
constexpr std::uint8_t iso_flag_use_base_colour_space = 0x40;
constexpr std::uint8_t iso_flag_common_denominator = 0x08;

// MPF: the MP entry attributes of the two images (libultrahdr's kMPEntryAttributeTypePrimary and
// 0: a JPEG, and for the first image "Baseline MP Primary Image").
constexpr std::uint32_t mpf_primary_attribute = 0x030000;
constexpr std::uint32_t mpf_secondary_attribute = 0x000000;

void put8(Bytes& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}
void put16(Bytes& out, unsigned v) {  // big endian, as JPEG, MPF (here) and ISO 21496-1 are
    put8(out, v >> 8);
    put8(out, v);
}
void put32(Bytes& out, std::uint32_t v) {
    put16(out, v >> 16);
    put16(out, v & 0xFFFF);
}
void put_text(Bytes& out, std::string_view s) {
    out.insert(out.end(), s.begin(), s.end());
}
void put_text0(Bytes& out, std::string_view s) {  // with its terminating 0
    put_text(out, s);
    put8(out, 0);
}
void append(Bytes& out, std::span<const std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// One marker segment: 0xFF, the marker, a 2-byte length counting itself and the payload.
Bytes segment(std::uint8_t marker, const Bytes& payload) {
    if (payload.size() + 2 > 0xFFFF) {
        throw std::invalid_argument("Ultra HDR JPEG: a marker segment is too long");
    }
    Bytes out;
    put8(out, 0xFF);
    put8(out, marker);
    put16(out, static_cast<unsigned>(payload.size() + 2));
    append(out, payload);
    return out;
}

// The parts of a JPEG as stb_image_write writes it.
struct StbJpeg {
    std::span<const std::uint8_t> app0;    // the APP0 segment, whole (empty if there is none)
    std::span<const std::uint8_t> tables;  // from the first segment after the APPn ones to EOI
};

// Splits a JPEG into its APP0 segment and everything from the first non-APPn segment on. Throws
// std::invalid_argument naming `what` if it doesn't start with SOI, a segment runs past the end,
// it has an APPn other than APP0, it reaches no SOS or doesn't end with EOI.
StbJpeg split_stb_jpeg(std::span<const std::uint8_t> jpeg, const char* what) {
    const auto bad = [&](const std::string& why) {
        return std::invalid_argument(std::string("Ultra HDR JPEG: the ") + what + " " + why);
    };
    if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != marker_soi) {
        throw bad("does not start with SOI");
    }
    if (jpeg[jpeg.size() - 2] != 0xFF || jpeg[jpeg.size() - 1] != marker_eoi) {
        throw bad("does not end with EOI");
    }
    StbJpeg parts;
    std::size_t p = 2;
    while (true) {
        if (p + 4 > jpeg.size() || jpeg[p] != 0xFF) {
            throw bad("has no marker segment where one should start");
        }
        const std::uint8_t marker = jpeg[p + 1];
        const std::size_t length = (std::size_t(jpeg[p + 2]) << 8) | jpeg[p + 3];
        if (length < 2 || p + 2 + length > jpeg.size()) {
            throw bad("has a marker segment that runs past its end");
        }
        if (marker >= marker_app0 && marker <= marker_app15) {
            if (marker != marker_app0 || !parts.app0.empty()) {
                throw bad("has an APPn segment besides one APP0");
            }
            parts.app0 = jpeg.subspan(p, 2 + length);
            p += 2 + length;
            continue;
        }
        parts.tables = jpeg.subspan(p);
        break;
    }
    // The tables must reach a SOS (which ends the walk: entropy-coded data follows), with no APPn
    // among them.
    for (std::size_t q = p;;) {
        if (q + 4 > jpeg.size() || jpeg[q] != 0xFF) {
            throw bad("has no SOS segment");
        }
        const std::uint8_t marker = jpeg[q + 1];
        const std::size_t length = (std::size_t(jpeg[q + 2]) << 8) | jpeg[q + 3];
        if (length < 2 || q + 2 + length > jpeg.size()) {
            throw bad("has a marker segment that runs past its end");
        }
        if (marker >= marker_app0 && marker <= marker_app15) {
            throw bad("has an APPn segment besides one APP0");
        }
        if (marker == marker_sos) {
            break;
        }
        q += 2 + length;
    }
    return parts;
}

// The shortest plain decimal that reads back as exactly `v` (std::to_chars in fixed format, never
// an exponent, which XMP readers parsing decimals by hand may not take; locale-independent).
std::string xmp_number(float v) {
    char buffer[64];
    const std::to_chars_result r =
        std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::fixed);
    if (r.ec != std::errc{}) {
        // Only a float of 1e39 or more needs over 64 characters; no gain-map value comes near.
        throw std::invalid_argument("Ultra HDR JPEG: a gain map value is too large for XMP");
    }
    return std::string(buffer, r.ptr);
}

// The primary image's XMP: the hdrgm version, and the GContainer directory naming the gain map and
// its length (the whole gain map image, SOI to EOI, with its own metadata).
std::string primary_xmp(std::size_t gain_map_length) {
    return "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Prism\">"
           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
           "<rdf:Description rdf:about=\"\""
           " xmlns:Container=\"http://ns.google.com/photos/1.0/container/\""
           " xmlns:Item=\"http://ns.google.com/photos/1.0/container/item/\""
           " xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
           " hdrgm:Version=\"1.0\">"
           "<Container:Directory><rdf:Seq>"
           "<rdf:li rdf:parseType=\"Resource\">"
           "<Container:Item Item:Semantic=\"Primary\" Item:Mime=\"image/jpeg\"/></rdf:li>"
           "<rdf:li rdf:parseType=\"Resource\">"
           "<Container:Item Item:Semantic=\"GainMap\" Item:Mime=\"image/jpeg\" Item:Length=\"" +
           std::to_string(gain_map_length) +
           "\"/></rdf:li>"
           "</rdf:Seq></Container:Directory>"
           "</rdf:Description></rdf:RDF></x:xmpmeta>";
}

// The gain map's XMP: the hdrgm metadata. GainMapMin/Max and HDRCapacityMin/Max are in stops
// (log2), as hdrgm stores them.
std::string gain_map_xmp(const GainMapMetadata& m) {
    return "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Prism\">"
           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
           "<rdf:Description rdf:about=\"\""
           " xmlns:hdrgm=\"http://ns.adobe.com/hdr-gain-map/1.0/\""
           " hdrgm:Version=\"1.0\""
           " hdrgm:GainMapMin=\"" +
           xmp_number(m.min_log2) + "\" hdrgm:GainMapMax=\"" + xmp_number(m.max_log2) +
           "\" hdrgm:Gamma=\"" + xmp_number(m.gamma) + "\" hdrgm:OffsetSDR=\"" +
           xmp_number(m.offset_sdr) + "\" hdrgm:OffsetHDR=\"" + xmp_number(m.offset_hdr) +
           "\" hdrgm:HDRCapacityMin=\"" + xmp_number(m.hdr_capacity_min) +
           "\" hdrgm:HDRCapacityMax=\"" + xmp_number(m.hdr_capacity_max) +
           "\" hdrgm:BaseRenditionIsHDR=\"False\"/>"
           "</rdf:RDF></x:xmpmeta>";
}

Bytes xmp_payload(const std::string& xmp) {
    Bytes p;
    put_text0(p, xmp_namespace);
    put_text(p, xmp);
    return p;
}

// The primary image's ISO 21496-1 segment: only the versions, saying the file has a gain map.
Bytes iso_version_payload() {
    Bytes p;
    put_text0(p, iso_namespace);
    put16(p, 0);  // minimum_version
    put16(p, 0);  // writer_version
    return p;
}

// The gain map's ISO 21496-1 metadata: one channel, the base picture's colour space, the fractions
// of iso_fractions(), in libultrahdr's order.
Bytes iso_metadata_payload(const GainMapMetadata& m) {
    const std::optional<IsoGainMapFractions> f = iso_fractions(m);
    if (!f) {
        throw std::invalid_argument(
            "Ultra HDR JPEG: a gain map value can't be written as an ISO 21496-1 fraction");
    }
    Bytes p;
    put_text0(p, iso_namespace);
    put16(p, 0);  // minimum_version
    put16(p, 0);  // writer_version
    const std::uint32_t d = f->base_hdr_headroom.denominator;
    const bool common = f->alternate_hdr_headroom.denominator == d &&
                        f->gain_map_min.denominator == d && f->gain_map_max.denominator == d &&
                        f->gamma.denominator == d && f->base_offset.denominator == d &&
                        f->alternate_offset.denominator == d;
    const auto s32 = [](std::int32_t v) { return static_cast<std::uint32_t>(v); };
    put8(p, iso_flag_use_base_colour_space | (common ? iso_flag_common_denominator : 0));
    if (common) {
        put32(p, d);
        put32(p, f->base_hdr_headroom.numerator);
        put32(p, f->alternate_hdr_headroom.numerator);
        put32(p, s32(f->gain_map_min.numerator));
        put32(p, s32(f->gain_map_max.numerator));
        put32(p, f->gamma.numerator);
        put32(p, s32(f->base_offset.numerator));
        put32(p, s32(f->alternate_offset.numerator));
    } else {
        put32(p, f->base_hdr_headroom.numerator);
        put32(p, f->base_hdr_headroom.denominator);
        put32(p, f->alternate_hdr_headroom.numerator);
        put32(p, f->alternate_hdr_headroom.denominator);
        put32(p, s32(f->gain_map_min.numerator));
        put32(p, f->gain_map_min.denominator);
        put32(p, s32(f->gain_map_max.numerator));
        put32(p, f->gain_map_max.denominator);
        put32(p, f->gamma.numerator);
        put32(p, f->gamma.denominator);
        put32(p, s32(f->base_offset.numerator));
        put32(p, f->base_offset.denominator);
        put32(p, s32(f->alternate_offset.numerator));
        put32(p, f->alternate_offset.denominator);
    }
    return p;
}

// The ICC profile in one APP2 chunk: "ICC_PROFILE\0", chunk 1 of 1, the profile.
Bytes icc_payload() {
    Bytes p;
    put_text0(p, icc_signature);
    put8(p, 1);
    put8(p, 1);
    append(p, srgb_icc_profile());
    return p;
}

// MPF: "MPF\0", then a big-endian TIFF header and the MP Index IFD with three tags (version, the
// number of images, the MP entries) and the two 16-byte MP entries. Offsets are from the TIFF
// header; the primary's offset is 0 by definition.
constexpr std::size_t mpf_payload_size = 4 + 8 + 2 + 3 * 12 + 4 + 2 * 16;  // 86

Bytes mpf_payload(std::uint32_t primary_size, std::uint32_t gain_map_size,
                  std::uint32_t gain_map_offset) {
    constexpr std::uint32_t ifd_offset = 8;  // right after the TIFF header
    constexpr std::uint32_t entries_offset = ifd_offset + 2 + 3 * 12 + 4;  // 50
    Bytes p;
    put_text0(p, mpf_signature);
    put_text(p, "MM");
    put16(p, 0x002A);
    put32(p, ifd_offset);
    put16(p, 3);       // tags
    put16(p, 0xB000);  // MPFVersion: UNDEFINED, 4 bytes "0100"
    put16(p, 7);
    put32(p, 4);
    put_text(p, "0100");
    put16(p, 0xB001);  // NumberOfImages: LONG
    put16(p, 4);
    put32(p, 1);
    put32(p, 2);
    put16(p, 0xB002);  // MPEntry: UNDEFINED, 2 x 16 bytes
    put16(p, 7);
    put32(p, 2 * 16);
    put32(p, entries_offset);
    put32(p, 0);  // no further IFD
    put32(p, mpf_primary_attribute);
    put32(p, primary_size);
    put32(p, 0);  // the primary's offset
    put16(p, 0);  // dependent images
    put16(p, 0);
    put32(p, mpf_secondary_attribute);
    put32(p, gain_map_size);
    put32(p, gain_map_offset);
    put16(p, 0);
    put16(p, 0);
    if (p.size() != mpf_payload_size) {
        throw std::logic_error("MPF payload size");  // cannot happen
    }
    return p;
}

// Offset of the TIFF header inside the MPF segment: marker (2), length (2), "MPF\0" (4).
constexpr std::size_t mpf_tiff_header_at = 8;

void stb_append(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

Bytes stb_jpeg(std::span<const std::uint8_t> pixels, int width, int height, int channels,
               int quality, const char* what) {
    Bytes out;
    if (stbi_write_jpg_to_func(stb_append, &out, width, height, channels, pixels.data(), quality) ==
        0) {
        throw std::runtime_error(std::string("Ultra HDR JPEG: stb could not encode the ") + what);
    }
    return out;
}

struct StbiDeleter {
    void operator()(unsigned char* p) const noexcept { stbi_image_free(p); }
};

}  // namespace

std::vector<std::uint8_t> assemble_ultra_hdr_jpeg(std::span<const std::uint8_t> base_jpeg,
                                                  std::span<const std::uint8_t> gain_map_jpeg,
                                                  const GainMapMetadata& metadata) {
    const StbJpeg base = split_stb_jpeg(base_jpeg, "SDR picture");
    const StbJpeg map = split_stb_jpeg(gain_map_jpeg, "gain map");

    // The gain map image first: the primary's XMP and MPF need its length.
    Bytes gain_map = {0xFF, marker_soi};
    append(gain_map, segment(marker_app1, xmp_payload(gain_map_xmp(metadata))));
    append(gain_map, segment(marker_app2, iso_metadata_payload(metadata)));
    append(gain_map, map.app0);
    append(gain_map, map.tables);

    Bytes primary = {0xFF, marker_soi};
    append(primary, base.app0);
    append(primary, segment(marker_app1, xmp_payload(primary_xmp(gain_map.size()))));
    append(primary, segment(marker_app2, iso_version_payload()));
    append(primary, segment(marker_app2, icc_payload()));
    const std::size_t mpf_at = primary.size();
    const std::size_t primary_size = mpf_at + 4 + mpf_payload_size + base.tables.size();
    if (primary_size + gain_map.size() > 0xFFFFFFFFu) {
        throw std::invalid_argument("Ultra HDR JPEG: the file would be over 4 GB");
    }
    const std::size_t gain_map_offset = primary_size - (mpf_at + mpf_tiff_header_at);
    append(primary, segment(marker_app2, mpf_payload(static_cast<std::uint32_t>(primary_size),
                                                     static_cast<std::uint32_t>(gain_map.size()),
                                                     static_cast<std::uint32_t>(gain_map_offset))));
    append(primary, base.tables);
    if (primary.size() != primary_size) {
        throw std::logic_error("Ultra HDR JPEG: primary size");  // cannot happen
    }

    primary.insert(primary.end(), gain_map.begin(), gain_map.end());
    return primary;
}

UltraHdrJpeg encode_ultra_hdr_jpeg(std::span<const std::uint8_t> sdr_srgb8, const Image& hdr,
                                   float headroom_stops, const GainMapOptions& options,
                                   int threads) {
    if (hdr.width <= 0 || hdr.height <= 0 ||
        sdr_srgb8.size() != std::size_t(hdr.width) * std::size_t(hdr.height) * 3 ||
        hdr.pixels.size() != sdr_srgb8.size()) {
        throw std::invalid_argument(
            "Ultra HDR JPEG: the SDR and HDR pictures must be the same size and not empty");
    }
    const int width = hdr.width;
    const int height = hdr.height;
    UltraHdrJpeg out;
    const Bytes base =
        stb_jpeg(sdr_srgb8, width, height, 3, detail::sdr_jpeg_quality, "SDR picture");

    // The base as a viewer decodes it.
    if (base.size() > std::size_t(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Ultra HDR JPEG: the SDR picture's JPEG is over 2 GB");
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    const std::unique_ptr<unsigned char, StbiDeleter> decoded(
        stbi_load_from_memory(base.data(), static_cast<int>(base.size()), &w, &h, &channels, 3));
    if (!decoded || w != width || h != height) {
        throw std::runtime_error("Ultra HDR JPEG: stb could not decode the SDR picture again");
    }
    out.gain_map = make_gain_map(std::span<const std::uint8_t>(decoded.get(), sdr_srgb8.size()),
                                 hdr, headroom_stops, options, threads);

    const Bytes map =
        stb_jpeg(out.gain_map.codes, width, height, 1, gain_map_jpeg_quality, "gain map");
    out.bytes = assemble_ultra_hdr_jpeg(base, map, out.gain_map.metadata);
    return out;
}

void save_ultra_hdr_jpeg(const std::filesystem::path& path, std::span<const std::uint8_t> sdr_srgb8,
                         const Image& hdr, float headroom_stops, int threads) {
    const UltraHdrJpeg file = encode_ultra_hdr_jpeg(sdr_srgb8, hdr, headroom_stops, {}, threads);
    const std::optional<AtomicWriteError> error = write_file_atomically(
        path,
        std::string_view(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()));
    if (!error) {
        return;
    }
    std::string message = "cannot save image '" + utf8(path) + "': ";
    if (error->step == AtomicWriteError::Step::write) {
        message += "cannot write '" + utf8(error->file) + "'";
    } else {
        message += error->reason;
    }
    throw std::runtime_error(message);
}

}  // namespace prism
