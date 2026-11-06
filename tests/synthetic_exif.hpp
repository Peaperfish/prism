#pragma once

// Test helpers: EXIF blocks and JPEG files built in memory, so the EXIF tests need no camera
// file (none is checked in). Not part of Prism.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "prism/image.hpp"

namespace prism_test {

// One IFD entry of an EXIF block under construction.
struct ExifEntry {
    std::uint16_t tag = 0;
    std::uint16_t type = 0;  // TIFF field type: 2 ASCII, 3 SHORT, 4 LONG, 5 RATIONAL
    std::uint32_t count = 0;
    std::vector<std::uint8_t> data;          // already in the block's byte order
    std::optional<std::uint32_t> raw_field;  // written as the value field, no data stored
};

// Builds the TIFF structure of an EXIF block (what follows "Exif\0\0" in a JPEG's APP1): header,
// IFD 0 and, when it has entries, an Exif IFD that IFD 0's ExifIFD pointer (0x8769) points to.
class ExifBuilder {
public:
    explicit ExifBuilder(bool big_endian) : big_(big_endian) {}

    // Entries of IFD 0 / of the Exif IFD.
    ExifBuilder& ifd0_ascii(std::uint16_t tag, const std::string& s) {
        return add(ifd0_, ascii(tag, s));
    }
    ExifBuilder& ifd0_short(std::uint16_t tag, std::uint16_t v) {
        return add(ifd0_, shorts(tag, v));
    }
    ExifBuilder& exif_ascii(std::uint16_t tag, const std::string& s) {
        return add(exif_, ascii(tag, s));
    }
    ExifBuilder& exif_short(std::uint16_t tag, std::uint16_t v) {
        return add(exif_, shorts(tag, v));
    }
    ExifBuilder& exif_rational(std::uint16_t tag, std::uint32_t num, std::uint32_t den) {
        return add(exif_, rational(tag, num, den));
    }
    ExifBuilder& ifd0_entry(ExifEntry e) { return add(ifd0_, std::move(e)); }
    ExifBuilder& exif_entry(ExifEntry e) { return add(exif_, std::move(e)); }

    // Makes IFD 0's ExifIFD pointer point at this offset instead of at the Exif IFD (and writes
    // it even if there is no Exif IFD).
    ExifBuilder& exif_pointer(std::uint32_t offset) {
        pointer_ = offset;
        return *this;
    }

    [[nodiscard]] std::vector<std::uint8_t> build() const {
        const bool has_exif = !exif_.empty() || pointer_;
        std::vector<ExifEntry> ifd0 = ifd0_;
        if (has_exif) {
            ifd0.push_back(longs(0x8769, 0));  // value patched below
        }
        // Layout: header | IFD 0 | its out-of-line data | Exif IFD | its data.
        const std::uint32_t ifd0_at = 8;
        const std::uint32_t ifd0_size = ifd_size(ifd0);
        const std::uint32_t exif_at = ifd0_at + ifd0_size;
        if (has_exif) {
            ifd0.back() = longs(0x8769, pointer_.value_or(exif_at));
        }
        std::vector<std::uint8_t> out;
        out.push_back(big_ ? 'M' : 'I');
        out.push_back(big_ ? 'M' : 'I');
        put(out, 42, 2);
        put(out, ifd0_at, 4);
        write_ifd(out, ifd0, ifd0_at);
        if (!exif_.empty()) {
            write_ifd(out, exif_, exif_at);
        }
        return out;
    }

    // The entry constructors, in this builder's byte order.
    [[nodiscard]] ExifEntry ascii(std::uint16_t tag, const std::string& s) const {
        ExifEntry e{tag, 2, static_cast<std::uint32_t>(s.size() + 1), {}, std::nullopt};
        e.data.assign(s.begin(), s.end());
        e.data.push_back(0);
        return e;
    }
    [[nodiscard]] ExifEntry shorts(std::uint16_t tag, std::uint16_t v) const {
        ExifEntry e{tag, 3, 1, {}, std::nullopt};
        put(e.data, v, 2);
        return e;
    }
    [[nodiscard]] ExifEntry longs(std::uint16_t tag, std::uint32_t v) const {
        ExifEntry e{tag, 4, 1, {}, std::nullopt};
        put(e.data, v, 4);
        return e;
    }
    [[nodiscard]] ExifEntry rational(std::uint16_t tag, std::uint32_t num,
                                     std::uint32_t den) const {
        ExifEntry e{tag, 5, 1, {}, std::nullopt};
        put(e.data, num, 4);
        put(e.data, den, 4);
        return e;
    }

private:
    ExifBuilder& add(std::vector<ExifEntry>& to, ExifEntry e) {
        to.push_back(std::move(e));
        return *this;
    }

    // Size of an IFD with its out-of-line data (each value on an even offset).
    static std::uint32_t ifd_size(const std::vector<ExifEntry>& entries) {
        std::uint32_t size = 2 + 12 * static_cast<std::uint32_t>(entries.size()) + 4;
        for (const ExifEntry& e : entries) {
            if (!e.raw_field && e.data.size() > 4) {
                size += static_cast<std::uint32_t>(e.data.size() + (e.data.size() % 2));
            }
        }
        return size;
    }

    void write_ifd(std::vector<std::uint8_t>& out, const std::vector<ExifEntry>& entries,
                   std::uint32_t at) const {
        std::uint32_t blob_at = at + 2 + 12 * static_cast<std::uint32_t>(entries.size()) + 4;
        std::vector<std::uint8_t> blob;
        put(out, static_cast<std::uint32_t>(entries.size()), 2);
        for (const ExifEntry& e : entries) {
            put(out, e.tag, 2);
            put(out, e.type, 2);
            put(out, e.count, 4);
            if (e.raw_field) {
                put(out, *e.raw_field, 4);
            } else if (e.data.size() > 4) {
                put(out, blob_at + static_cast<std::uint32_t>(blob.size()), 4);
                blob.insert(blob.end(), e.data.begin(), e.data.end());
                if (blob.size() % 2) {
                    blob.push_back(0);
                }
            } else {
                for (std::size_t k = 0; k < 4; ++k) {
                    out.push_back(k < e.data.size() ? e.data[k] : 0);
                }
            }
        }
        put(out, 0, 4);  // no next IFD
        out.insert(out.end(), blob.begin(), blob.end());
    }

    void put(std::vector<std::uint8_t>& out, std::uint32_t v, int size) const {
        for (int k = 0; k < size; ++k) {
            const int shift = 8 * (big_ ? size - 1 - k : k);
            out.push_back(static_cast<std::uint8_t>(v >> shift));
        }
    }

    bool big_;
    std::vector<ExifEntry> ifd0_;
    std::vector<ExifEntry> exif_;
    std::optional<std::uint32_t> pointer_;
};

// A marker segment: FF <marker> <2-byte length incl. itself> <payload>.
inline std::vector<std::uint8_t> jpeg_segment(std::uint8_t marker,
                                              const std::vector<std::uint8_t>& payload) {
    const std::size_t length = payload.size() + 2;
    std::vector<std::uint8_t> seg = {0xFF, marker, static_cast<std::uint8_t>(length >> 8),
                                     static_cast<std::uint8_t>(length & 0xFF)};
    seg.insert(seg.end(), payload.begin(), payload.end());
    return seg;
}

// An APP1 segment holding an EXIF block.
inline std::vector<std::uint8_t> exif_app1(const std::vector<std::uint8_t>& tiff) {
    std::vector<std::uint8_t> payload = {'E', 'x', 'i', 'f', 0, 0};
    payload.insert(payload.end(), tiff.begin(), tiff.end());
    return jpeg_segment(0xE1, payload);
}

// An APP1 segment holding XMP, which also lives in APP1 and may come before the EXIF one.
inline std::vector<std::uint8_t> xmp_app1() {
    const std::string ns = "http://ns.adobe.com/xap/1.0/";
    std::vector<std::uint8_t> payload(ns.begin(), ns.end());
    payload.push_back(0);
    const std::string xml = "<x:xmpmeta/>";
    payload.insert(payload.end(), xml.begin(), xml.end());
    return jpeg_segment(0xE1, payload);
}

inline std::vector<std::uint8_t> read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void write_all(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

// The quadrant picture's size and colours: top-left red, top-right green, bottom-left blue,
// bottom-right yellow, each a solid 32 x 16 block (whole JPEG blocks, so chroma subsampling
// doesn't blur their middles).
inline constexpr int quadrant_width = 64;
inline constexpr int quadrant_height = 32;
inline constexpr std::uint8_t quadrant_rgb[4][3] = {
    {230, 30, 30}, {30, 230, 30}, {30, 30, 230}, {230, 230, 30}};

// A JPEG of the quadrant picture with `segments` placed right after SOI. `scratch` is a file name
// to encode through (save_srgb8 writes files).
inline std::vector<std::uint8_t> quadrant_jpeg(
    const std::filesystem::path& scratch, const std::vector<std::vector<std::uint8_t>>& segments) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(quadrant_width) * quadrant_height * 3);
    for (int y = 0; y < quadrant_height; ++y) {
        for (int x = 0; x < quadrant_width; ++x) {
            const int q = (y >= quadrant_height / 2 ? 2 : 0) + (x >= quadrant_width / 2 ? 1 : 0);
            for (int c = 0; c < 3; ++c) {
                rgb[(static_cast<std::size_t>(y) * quadrant_width + x) * 3 + c] =
                    quadrant_rgb[q][c];
            }
        }
    }
    prism::save_srgb8(scratch, rgb, quadrant_width, quadrant_height);
    std::vector<std::uint8_t> jpeg = read_all(scratch);
    std::error_code ec;
    std::filesystem::remove(scratch, ec);
    std::vector<std::uint8_t> spliced(jpeg.begin(), jpeg.begin() + 2);  // SOI
    for (const auto& seg : segments) {
        spliced.insert(spliced.end(), seg.begin(), seg.end());
    }
    spliced.insert(spliced.end(), jpeg.begin() + 2, jpeg.end());
    return spliced;
}

// The quadrant picture with an Orientation tag (and nothing else) in its EXIF, written to `path`.
inline void write_oriented_jpeg(const std::filesystem::path& path, int orientation) {
    ExifBuilder b(false);
    b.ifd0_short(0x0112, static_cast<std::uint16_t>(orientation));
    std::filesystem::path scratch = path;
    scratch += ".tmp.jpg";
    write_all(path, quadrant_jpeg(scratch, {exif_app1(b.build())}));
}

}  // namespace prism_test
