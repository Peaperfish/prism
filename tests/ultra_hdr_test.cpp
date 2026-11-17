// Ultra HDR JPEG writer (#104). The files are read back with a parser written here from the
// formats (JPEG markers, CIPA MPF, the hdrgm XMP, ISO 21496-1 as libultrahdr lays it out), never
// with the writer's own code, and decoded with stb_image. Expected values are literals, the stored
// ICC profile (third_party/icc/), the stored golden JPEGs (tests/data/golden/), or what stb itself
// gives for the same pixels. Whole files are never compared byte for byte: stb's float DCT can
// write different bytes with another compiler.
//
// On macOS, ImageIO (Apple's own reader) must find the gain map and decode the file to HDR.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include <stb_image.h>
#include <stb_image_write.h>

#include "golden.hpp"
#include "prism/frame.hpp"
#include "prism/gain_map.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/recipe.hpp"
#include "prism/ultra_hdr.hpp"

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

namespace {

// --- Reading the file ----------------------------------------------------------------------------

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_uhdr_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

// Removes a file when it goes out of scope, so a failed ASSERT leaves nothing behind.
struct RemoveOnExit {
    fs::path path;
    ~RemoveOnExit() {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
};

Bytes read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::uint32_t be16(std::span<const std::uint8_t> b, std::size_t at) {
    return (std::uint32_t(b[at]) << 8) | b[at + 1];
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t at) {
    return (be16(b, at) << 16) | be16(b, at + 2);
}

// One marker segment between SOI and SOS (inclusive): its marker, where its 0xFF is, its length
// field (which counts itself and the payload) and the payload.
struct Segment {
    std::uint8_t marker = 0;
    std::size_t at = 0;
    std::size_t length = 0;
    std::span<const std::uint8_t> payload;
};

// The segments of the JPEG starting at `start` (at its SOI), up to and including SOS; nothing if
// it doesn't start with SOI or a segment runs past the end.
std::optional<std::vector<Segment>> segments(std::span<const std::uint8_t> file,
                                             std::size_t start) {
    if (start + 2 > file.size() || file[start] != 0xFF || file[start + 1] != 0xD8) {
        return std::nullopt;
    }
    std::vector<Segment> out;
    std::size_t p = start + 2;
    while (true) {
        if (p + 4 > file.size() || file[p] != 0xFF) {
            return std::nullopt;
        }
        Segment s;
        s.marker = file[p + 1];
        s.at = p;
        s.length = be16(file, p + 2);
        if (s.length < 2 || p + 2 + s.length > file.size()) {
            return std::nullopt;
        }
        s.payload = file.subspan(p + 4, s.length - 2);
        out.push_back(s);
        if (s.marker == 0xDA) {
            return out;
        }
        p += 2 + s.length;
    }
}

bool starts_with(std::span<const std::uint8_t> payload, std::string_view text) {
    return payload.size() >= text.size() &&
           std::memcmp(payload.data(), text.data(), text.size()) == 0;
}

// What a segment is, from its marker and the signature its payload starts with.
std::string kind(const Segment& s) {
    using namespace std::string_view_literals;
    switch (s.marker) {
        case 0xE0: return starts_with(s.payload, "JFIF\0"sv) ? "APP0 JFIF" : "APP0 ?";
        case 0xE1:
            return starts_with(s.payload, "http://ns.adobe.com/xap/1.0/\0"sv) ? "APP1 XMP"
                                                                              : "APP1 ?";
        case 0xE2:
            if (starts_with(s.payload, "urn:iso:std:iso:ts:21496:-1\0"sv)) {
                return "APP2 ISO";
            }
            if (starts_with(s.payload, "ICC_PROFILE\0"sv)) {
                return "APP2 ICC";
            }
            if (starts_with(s.payload, "MPF\0"sv)) {
                return "APP2 MPF";
            }
            return "APP2 ?";
        case 0xDB: return "DQT";
        case 0xC0: return "SOF0";
        case 0xC4: return "DHT";
        case 0xDA: return "SOS";
        default: return "other";
    }
}

std::vector<std::string> kinds(const std::vector<Segment>& segs) {
    std::vector<std::string> out;
    for (const Segment& s : segs) {
        out.push_back(kind(s));
    }
    return out;
}

const Segment* find(const std::vector<Segment>& segs, const std::string& what) {
    for (const Segment& s : segs) {
        if (kind(s) == what) {
            return &s;
        }
    }
    return nullptr;
}

// The MPF segment's MP Index IFD, read as CIPA DC-007 lays it out (big or little endian).
struct MpEntry {
    std::uint32_t attribute = 0;
    std::uint32_t size = 0;
    std::uint32_t offset = 0;  // from the TIFF header; 0 for the first image
};
struct Mpf {
    std::size_t tiff_at = 0;  // where the TIFF header is in the file
    std::string version;
    std::uint32_t images = 0;
    std::vector<MpEntry> entries;
};

std::optional<Mpf> read_mpf(std::span<const std::uint8_t> file, const Segment& s) {
    const std::span<const std::uint8_t> t = s.payload.subspan(4);  // after "MPF\0"
    if (t.size() < 8 || !(t[0] == 'M' && t[1] == 'M') || be16(t, 2) != 0x2A) {
        return std::nullopt;  // Prism writes big endian
    }
    Mpf m;
    m.tiff_at = s.at + 8;
    const std::uint32_t ifd = be32(t, 4);
    if (ifd + 2 > t.size()) {
        return std::nullopt;
    }
    const std::uint32_t count = be16(t, ifd);
    std::uint32_t entries_at = 0;
    std::uint32_t entries_bytes = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t e = ifd + 2 + 12 * i;
        if (e + 12 > t.size()) {
            return std::nullopt;
        }
        const std::uint32_t tag = be16(t, e);
        if (tag == 0xB000) {
            m.version.assign(reinterpret_cast<const char*>(&t[e + 8]), 4);
        } else if (tag == 0xB001) {
            m.images = be32(t, e + 8);
        } else if (tag == 0xB002) {
            entries_bytes = be32(t, e + 4);
            entries_at = be32(t, e + 8);
        }
    }
    if (entries_at + entries_bytes > t.size() || entries_bytes % 16 != 0) {
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < entries_bytes / 16; ++i) {
        const std::size_t e = entries_at + 16 * i;
        m.entries.push_back({be32(t, e), be32(t, e + 4), be32(t, e + 8)});
    }
    (void)file;
    return m;
}

std::string xmp_text(const Segment& s) {
    const std::size_t skip = std::string_view("http://ns.adobe.com/xap/1.0/").size() + 1;
    return std::string(reinterpret_cast<const char*>(s.payload.data()) + skip,
                       s.payload.size() - skip);
}

// The value of attribute `name` (e.g. "hdrgm:GainMapMax") in an XMP packet, or nothing.
std::optional<std::string> xmp_attribute(const std::string& xmp, const std::string& name) {
    const std::string key = " " + name + "=\"";
    const std::size_t at = xmp.find(key);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t from = at + key.size();
    const std::size_t to = xmp.find('"', from);
    if (to == std::string::npos) {
        return std::nullopt;
    }
    return xmp.substr(from, to - from);
}

float xmp_float(const std::string& xmp, const std::string& name) {
    const std::optional<std::string> v = xmp_attribute(xmp, name);
    if (!v) {
        ADD_FAILURE() << name << " is missing";
        return NAN;
    }
    char* end = nullptr;
    const float f = std::strtof(v->c_str(), &end);
    EXPECT_EQ(*end, '\0') << name << " = " << *v;
    return f;
}

// ISO 21496-1 metadata as libultrahdr 2.0.2 lays it out (gainmapmetadata.cpp), one channel.
struct IsoMetadata {
    std::uint16_t minimum_version = 0xFFFF;
    std::uint16_t writer_version = 0xFFFF;
    std::uint8_t flags = 0;
    prism::IsoGainMapFractions fractions;
};

std::optional<IsoMetadata> read_iso(const Segment& s) {
    const std::size_t skip = std::string_view("urn:iso:std:iso:ts:21496:-1").size() + 1;
    const std::span<const std::uint8_t> p = s.payload.subspan(skip);
    if (p.size() < 5) {
        return std::nullopt;
    }
    IsoMetadata m;
    m.minimum_version = static_cast<std::uint16_t>(be16(p, 0));
    m.writer_version = static_cast<std::uint16_t>(be16(p, 2));
    m.flags = p[4];
    if (m.flags & 0x80) {
        return std::nullopt;  // several channels: Prism writes one
    }
    const bool common = (m.flags & 0x08) != 0;
    const std::size_t want = 5 + (common ? 8 * 4 : 14 * 4);
    if (p.size() != want) {
        return std::nullopt;
    }
    std::size_t at = 5;
    const auto u = [&] {
        const std::uint32_t v = be32(p, at);
        at += 4;
        return v;
    };
    const auto s32 = [&] { return static_cast<std::int32_t>(u()); };
    auto& f = m.fractions;
    if (common) {
        const std::uint32_t d = u();
        f.base_hdr_headroom = {u(), d};
        f.alternate_hdr_headroom = {u(), d};
        f.gain_map_min = {s32(), d};
        f.gain_map_max = {s32(), d};
        f.gamma = {u(), d};
        f.base_offset = {s32(), d};
        f.alternate_offset = {s32(), d};
    } else {
        f.base_hdr_headroom.numerator = u();
        f.base_hdr_headroom.denominator = u();
        f.alternate_hdr_headroom.numerator = u();
        f.alternate_hdr_headroom.denominator = u();
        f.gain_map_min.numerator = s32();
        f.gain_map_min.denominator = u();
        f.gain_map_max.numerator = s32();
        f.gain_map_max.denominator = u();
        f.gamma.numerator = u();
        f.gamma.denominator = u();
        f.base_offset.numerator = s32();
        f.base_offset.denominator = u();
        f.alternate_offset.numerator = s32();
        f.alternate_offset.denominator = u();
    }
    return m;
}

// A whole file taken apart: the primary's segments, the MPF, and the gain map image.
struct Parsed {
    std::vector<Segment> primary;
    Mpf mpf;
    std::size_t primary_size = 0;  // from MPF
    std::size_t gain_map_at = 0;   // from MPF
    std::size_t gain_map_size = 0;
    std::vector<Segment> gain_map;
};

testing::AssertionResult parse(std::span<const std::uint8_t> file, Parsed& out) {
    const auto primary = segments(file, 0);
    if (!primary) {
        return testing::AssertionFailure() << "the primary image's segments don't parse";
    }
    out.primary = *primary;
    const Segment* mpf_segment = find(out.primary, "APP2 MPF");
    if (mpf_segment == nullptr) {
        return testing::AssertionFailure() << "no MPF segment";
    }
    const auto mpf = read_mpf(file, *mpf_segment);
    if (!mpf || mpf->entries.size() != 2) {
        return testing::AssertionFailure() << "the MPF segment doesn't parse as two images";
    }
    out.mpf = *mpf;
    out.primary_size = mpf->entries[0].size;
    out.gain_map_at = mpf->tiff_at + mpf->entries[1].offset;
    out.gain_map_size = mpf->entries[1].size;
    if (out.gain_map_at + out.gain_map_size > file.size()) {
        return testing::AssertionFailure()
               << "MPF puts the gain map at " << out.gain_map_at << " + " << out.gain_map_size
               << ", past the end of the file (" << file.size() << ")";
    }
    const auto map = segments(file, out.gain_map_at);
    if (!map) {
        return testing::AssertionFailure()
               << "MPF's offset " << out.gain_map_at << " doesn't point at a JPEG's SOI";
    }
    out.gain_map = *map;
    return testing::AssertionSuccess();
}

// --- Test pictures -------------------------------------------------------------------------------

struct Pictures {
    Bytes sdr;  // 8-bit sRGB
    prism::Image hdr;
};

// A small picture with light up to 4x white: a horizontal ramp from 0.01 to 4 (SDR: the same,
// cut at 1) over a vertical colour wash, 37 x 23 so neither side is a multiple of 8.
Pictures ramp(int width = 37, int height = 23) {
    Pictures p;
    p.hdr = prism::Image{width, height, std::vector<float>(std::size_t(width) * height * 3)};
    prism::Image sdr = p.hdr;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float v = 0.01f * std::pow(400.0f, float(x) / float(width - 1));
            const float tint[3] = {1.0f, 0.6f + 0.4f * float(y) / float(height - 1), 0.8f};
            for (int c = 0; c < 3; ++c) {
                const std::size_t i = (std::size_t(y) * width + x) * 3 + c;
                p.hdr.pixels[i] = std::min(v * tint[c], 4.0f);
                sdr.pixels[i] = std::min(v * tint[c], 1.0f);
            }
        }
    }
    p.sdr = prism::encode_srgb8(sdr);
    return p;
}

constexpr float ramp_headroom = 2.0f;

const prism::UltraHdrJpeg& ramp_file() {
    static const prism::UltraHdrJpeg file = [] {
        const Pictures p = ramp();
        return prism::encode_ultra_hdr_jpeg(p.sdr, p.hdr, ramp_headroom);
    }();
    return file;
}

void stb_append(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    out->insert(out->end(), static_cast<std::uint8_t*>(data),
                static_cast<std::uint8_t*>(data) + size);
}

// stb_image's decode of `jpeg` with `channels` channels per pixel; empty if it fails.
Bytes stb_decode(std::span<const std::uint8_t> jpeg, int channels, int& width, int& height) {
    int stored = 0;
    stbi_uc* data = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &width,
                                          &height, &stored, channels);
    if (data == nullptr) {
        return {};
    }
    Bytes out(data, data + std::size_t(width) * height * channels);
    stbi_image_free(data);
    return out;
}

}  // namespace

// --- The ICC profile ---------------------------------------------------------------------------

TEST(UltraHdrJpeg, TheIccProfileIsTheStoredOne) {
    const Bytes stored = read_file(PRISM_ICC_PROFILE);
    ASSERT_EQ(stored.size(), 736u);
    prism_golden::Sha256 sha;
    sha.add(stored.data(), stored.size());
    EXPECT_EQ(sha.hex(), "af4efe28f6d311799865f325ba39184a2e978b113ca74124d60af7de22b105f4");
    const std::span<const std::uint8_t> embedded = prism::srgb_icc_profile();
    EXPECT_EQ(Bytes(embedded.begin(), embedded.end()), stored);
}

// --- Layout --------------------------------------------------------------------------------------

TEST(UltraHdrJpeg, PrimaryMarkersInOrderWithMpfBeforeTheTables) {
    Parsed f;
    ASSERT_TRUE(parse(ramp_file().bytes, f));
    const std::vector<std::string> want = {
        "APP0 JFIF", "APP1 XMP", "APP2 ISO", "APP2 ICC", "APP2 MPF", "DQT", "SOF0", "DHT", "SOS"};
    EXPECT_EQ(kinds(f.primary), want);
}

TEST(UltraHdrJpeg, GainMapMarkersInOrder) {
    Parsed f;
    ASSERT_TRUE(parse(ramp_file().bytes, f));
    const std::vector<std::string> want = {"APP1 XMP", "APP2 ISO", "APP0 JFIF", "DQT",
                                           "SOF0",     "DHT",      "SOS"};
    EXPECT_EQ(kinds(f.gain_map), want);
}

TEST(UltraHdrJpeg, SegmentLengthsAndTheTwoImagesFillTheFile) {
    const Bytes& file = ramp_file().bytes;
    Parsed f;
    ASSERT_TRUE(parse(file, f));
    // Each segment starts right where the one before it ends.
    for (const std::vector<Segment>* image : {&f.primary, &f.gain_map}) {
        for (std::size_t i = 1; i < image->size(); ++i) {
            EXPECT_EQ((*image)[i].at, (*image)[i - 1].at + 2 + (*image)[i - 1].length)
                << kind((*image)[i]);
        }
    }
    EXPECT_EQ(f.primary.front().at, 2u);  // right after SOI
    EXPECT_EQ(f.gain_map.front().at, f.gain_map_at + 2);
    // Literal lengths: JFIF 16 (stb), the ISO version 2 + 28 + 4, the ICC 2 + 12 + 2 + 736,
    // MPF 2 + 4 + 8 + 2 + 36 + 4 + 32.
    EXPECT_EQ(find(f.primary, "APP0 JFIF")->length, 16u);
    EXPECT_EQ(find(f.primary, "APP2 ISO")->length, 34u);
    EXPECT_EQ(find(f.primary, "APP2 ICC")->length, 752u);
    EXPECT_EQ(find(f.primary, "APP2 MPF")->length, 88u);
    // The primary ends with EOI right before the gain map, which ends with EOI at the file's end.
    ASSERT_GE(f.primary_size, 2u);
    EXPECT_EQ(f.gain_map_at, f.primary_size);
    EXPECT_EQ(file[f.primary_size - 2], 0xFF);
    EXPECT_EQ(file[f.primary_size - 1], 0xD9);
    EXPECT_EQ(f.gain_map_at + f.gain_map_size, file.size());
    EXPECT_EQ(file[file.size() - 2], 0xFF);
    EXPECT_EQ(file[file.size() - 1], 0xD9);
}

TEST(UltraHdrJpeg, MpfPointsAtTheGainMapJpeg) {
    const Bytes& file = ramp_file().bytes;
    Parsed f;
    ASSERT_TRUE(parse(file, f));
    EXPECT_EQ(f.mpf.version, "0100");
    EXPECT_EQ(f.mpf.images, 2u);
    EXPECT_EQ(f.mpf.entries[0].attribute, 0x030000u);  // baseline MP primary image, JPEG
    EXPECT_EQ(f.mpf.entries[0].offset, 0u);
    EXPECT_EQ(f.mpf.entries[1].attribute, 0u);
    // The offset counts from the TIFF header, 8 bytes into the MPF segment.
    EXPECT_EQ(f.mpf.tiff_at, find(f.primary, "APP2 MPF")->at + 8);
    // It is a real JPEG of the picture's size.
    int w = 0;
    int h = 0;
    const Bytes map = stb_decode(std::span(file).subspan(f.gain_map_at, f.gain_map_size), 1, w, h);
    ASSERT_FALSE(map.empty());
    EXPECT_EQ(w, 37);
    EXPECT_EQ(h, 23);
}

// --- Metadata ------------------------------------------------------------------------------------

TEST(UltraHdrJpeg, PrimaryXmpNamesTheGainMapAndItsLength) {
    Parsed f;
    ASSERT_TRUE(parse(ramp_file().bytes, f));
    const std::string xmp = xmp_text(*find(f.primary, "APP1 XMP"));
    EXPECT_EQ(xmp_attribute(xmp, "hdrgm:Version"), "1.0");
    EXPECT_EQ(xmp_attribute(xmp, "xmlns:hdrgm"), "http://ns.adobe.com/hdr-gain-map/1.0/");
    EXPECT_EQ(xmp_attribute(xmp, "xmlns:Container"), "http://ns.google.com/photos/1.0/container/");
    EXPECT_EQ(xmp_attribute(xmp, "xmlns:Item"), "http://ns.google.com/photos/1.0/container/item/");
    const std::size_t primary = xmp.find("Item:Semantic=\"Primary\"");
    const std::size_t gain_map = xmp.find("Item:Semantic=\"GainMap\"");
    ASSERT_NE(primary, std::string::npos);
    ASSERT_NE(gain_map, std::string::npos);
    EXPECT_LT(primary, gain_map);
    EXPECT_EQ(xmp_attribute(xmp.substr(gain_map), "Item:Length"), std::to_string(f.gain_map_size));
    EXPECT_EQ(xmp_attribute(xmp.substr(gain_map), "Item:Mime"), "image/jpeg");
}

TEST(UltraHdrJpeg, GainMapXmpHoldsTheMetadataWithCapacityMaxN) {
    const prism::UltraHdrJpeg& file = ramp_file();
    Parsed f;
    ASSERT_TRUE(parse(file.bytes, f));
    const std::string xmp = xmp_text(*find(f.gain_map, "APP1 XMP"));
    const prism::GainMapMetadata& m = file.gain_map.metadata;
    EXPECT_EQ(xmp_attribute(xmp, "hdrgm:Version"), "1.0");
    EXPECT_EQ(xmp_float(xmp, "hdrgm:HDRCapacityMax"), 2.0f);  // N, not GainMapMax
    EXPECT_EQ(xmp_float(xmp, "hdrgm:HDRCapacityMin"), 0.0f);
    EXPECT_EQ(xmp_float(xmp, "hdrgm:Gamma"), 1.0f);
    EXPECT_EQ(xmp_float(xmp, "hdrgm:OffsetSDR"), 0.015625f);
    EXPECT_EQ(xmp_float(xmp, "hdrgm:OffsetHDR"), 0.015625f);
    EXPECT_EQ(xmp_attribute(xmp, "hdrgm:BaseRenditionIsHDR"), "False");
    // The map's own range reads back exactly (shortest round-trip text).
    EXPECT_EQ(xmp_float(xmp, "hdrgm:GainMapMin"), m.min_log2);
    EXPECT_EQ(xmp_float(xmp, "hdrgm:GainMapMax"), m.max_log2);
    // The ramp's HDR reaches 4 = 2 stops where its SDR is white, so GainMapMax is just under 2
    // (the offsets, the base's JPEG and the per-pixel limit take a little off) and not N.
    EXPECT_NEAR(m.max_log2, std::log2((4.0 + 1.0 / 64) / (1.0 + 1.0 / 64)), 0.05);
    EXPECT_LT(m.max_log2, 1.99f);
}

TEST(UltraHdrJpeg, IsoVersionInThePrimaryAndFractionsInTheGainMap) {
    const prism::UltraHdrJpeg& file = ramp_file();
    Parsed f;
    ASSERT_TRUE(parse(file.bytes, f));
    // The primary's: the namespace and two zero versions, nothing else.
    const Segment& version = *find(f.primary, "APP2 ISO");
    const std::string ns = "urn:iso:std:iso:ts:21496:-1";
    Bytes want(ns.begin(), ns.end());
    want.insert(want.end(), {0, 0, 0, 0, 0});
    EXPECT_EQ(Bytes(version.payload.begin(), version.payload.end()), want);

    const auto iso = read_iso(*find(f.gain_map, "APP2 ISO"));
    ASSERT_TRUE(iso);
    EXPECT_EQ(iso->minimum_version, 0);
    EXPECT_EQ(iso->writer_version, 0);
    EXPECT_EQ(iso->flags & 0xF7,
              0x40);  // one channel, base colour space (bit 3: common denominator)
    const auto expected = prism::iso_fractions(file.gain_map.metadata);
    ASSERT_TRUE(expected);
    EXPECT_EQ(iso->fractions, *expected);
    // Literal: HDRCapacityMax (alternate headroom) is N = 2, HDRCapacityMin 0, offsets 1/64.
    const auto& fr = iso->fractions;
    EXPECT_EQ(double(fr.alternate_hdr_headroom.numerator) / fr.alternate_hdr_headroom.denominator,
              2.0);
    EXPECT_EQ(fr.base_hdr_headroom.numerator, 0u);
    EXPECT_EQ(double(fr.base_offset.numerator) / fr.base_offset.denominator, 1.0 / 64);
    EXPECT_EQ(double(fr.alternate_offset.numerator) / fr.alternate_offset.denominator, 1.0 / 64);
    EXPECT_EQ(double(fr.gamma.numerator) / fr.gamma.denominator, 1.0);
}

namespace {

// stb's JPEGs of the ramp, to wrap with chosen metadata.
struct RampJpegs {
    Bytes base;
    Bytes map;
};
RampJpegs ramp_jpegs() {
    const Pictures p = ramp();
    RampJpegs j;
    EXPECT_NE(stbi_write_jpg_to_func(stb_append, &j.base, 37, 23, 3, p.sdr.data(), 95), 0);
    const Bytes grey(std::size_t(37) * 23, 128);
    EXPECT_NE(stbi_write_jpg_to_func(stb_append, &j.map, 37, 23, 1, grey.data(), 97), 0);
    return j;
}

}  // namespace

// Every hdrgm number is a plain decimal, never an exponent ("1e-04"), which XMP readers that
// parse decimals by hand may not take; and it still reads back exactly.
TEST(UltraHdrJpeg, XmpNumbersArePlainDecimals) {
    const RampJpegs j = ramp_jpegs();
    prism::GainMapMetadata m;
    m.min_log2 = 1e-4f;
    m.max_log2 = 1.2345678e-5f;
    m.hdr_capacity_max = 2.0f;
    const Bytes file = prism::assemble_ultra_hdr_jpeg(j.base, j.map, m);
    Parsed f;
    ASSERT_TRUE(parse(file, f));
    const std::string xmp = xmp_text(*find(f.gain_map, "APP1 XMP"));
    for (const char* name :
         {"hdrgm:GainMapMin", "hdrgm:GainMapMax", "hdrgm:Gamma", "hdrgm:OffsetSDR",
          "hdrgm:OffsetHDR", "hdrgm:HDRCapacityMin", "hdrgm:HDRCapacityMax"}) {
        const std::optional<std::string> v = xmp_attribute(xmp, name);
        ASSERT_TRUE(v) << name;
        const bool plain = !v->empty() && v->find_first_not_of("-0123456789.") == std::string::npos;
        EXPECT_TRUE(plain) << name << " = " << *v;
    }
    EXPECT_EQ(xmp_attribute(xmp, "hdrgm:GainMapMin"), "0.0001");
    EXPECT_EQ(xmp_float(xmp, "hdrgm:GainMapMax"), 1.2345678e-5f);
}

// When every value is a whole number (offsets 0), ISO 21496-1 shares one denominator: flag 0x08
// and 8 fields in libultrahdr's order (denominator, base headroom, alternate headroom, min, max,
// gamma, base offset, alternate offset).
TEST(UltraHdrJpeg, IsoWithACommonDenominator) {
    const RampJpegs j = ramp_jpegs();
    prism::GainMapMetadata m;
    m.min_log2 = -1.0f;
    m.max_log2 = 3.0f;
    m.gamma = 1.0f;
    m.offset_sdr = 0.0f;
    m.offset_hdr = 0.0f;
    m.hdr_capacity_min = 0.0f;
    m.hdr_capacity_max = 2.0f;
    const Bytes file = prism::assemble_ultra_hdr_jpeg(j.base, j.map, m);
    Parsed f;
    ASSERT_TRUE(parse(file, f));
    const Segment& iso = *find(f.gain_map, "APP2 ISO");
    const std::size_t ns = std::string_view("urn:iso:std:iso:ts:21496:-1").size() + 1;
    const Bytes want = {0,    0,    0,    0,    0x48,  //
                        0,    0,    0,    1,           // denominator
                        0,    0,    0,    0,           // base headroom 0
                        0,    0,    0,    2,           // alternate headroom 2
                        0xFF, 0xFF, 0xFF, 0xFF,        // min -1
                        0,    0,    0,    3,           // max 3
                        0,    0,    0,    1,           // gamma 1
                        0,    0,    0,    0,           // base offset 0
                        0,    0,    0,    0};          // alternate offset 0
    EXPECT_EQ(Bytes(iso.payload.begin() + ns, iso.payload.end()), want);
}

TEST(UltraHdrJpeg, IccSegmentHoldsTheStoredProfile) {
    Parsed f;
    ASSERT_TRUE(parse(ramp_file().bytes, f));
    const Segment& icc = *find(f.primary, "APP2 ICC");
    const std::size_t head = std::string_view("ICC_PROFILE").size() + 1;
    ASSERT_EQ(icc.payload.size(), head + 2 + 736);
    EXPECT_EQ(icc.payload[head], 1);      // chunk 1
    EXPECT_EQ(icc.payload[head + 1], 1);  // of 1
    const Bytes stored = read_file(PRISM_ICC_PROFILE);
    EXPECT_EQ(Bytes(icc.payload.begin() + head + 2, icc.payload.end()), stored);
    // The gain map carries no profile.
    EXPECT_EQ(find(f.gain_map, "APP2 ICC"), nullptr);
}

// --- Pixels --------------------------------------------------------------------------------------

// The primary decodes to the plain JPEG's pixels: the SDR golden reference for the sample photo
// under Vivid (exact on Apple arm64, the golden JPEG rule elsewhere), and on every platform exactly
// what stb gives for the same SDR bytes.
TEST(UltraHdrJpeg, PrimaryDecodesAsTheSdrJpeg) {
    using namespace prism_golden;
    prism::Photo photo = prism::load_photo(sample_photo());
    std::optional<prism::Chromaticity> as_shot;
    if (photo.info.as_shot_white_xy) {
        as_shot = prism::Chromaticity{(*photo.info.as_shot_white_xy)[0],
                                      (*photo.info.as_shot_white_xy)[1]};
    }
    prism::ProcessSettings settings = prism::merge_recipe_and_flags(
        prism::load_recipe(fs::path(PRISM_RECIPES_DIR) / "image" / "vivid.prism"),
        prism::ProcessSettings{}, as_shot);
    settings.headroom_stops = 2.0f;
    prism::Image img =
        prism::frame_and_reduce(std::move(photo.image), std::nullopt, long_side).image;
    prism::Image hdr;
    (void)prism::process_hdr(img, hdr, settings);
    const Bytes sdr = prism::encode_srgb8(img);

    const fs::path out = temp_file("vivid.jpg");
    const RemoveOnExit remove_out{out};
    prism::save_ultra_hdr_jpeg(out, sdr, hdr, 2.0f);
    EXPECT_TRUE(jpeg_matches(out, golden_dir() / "image" / "vivid", "Ultra HDR primary"));

    Bytes plain;
    ASSERT_NE(stbi_write_jpg_to_func(stb_append, &plain, img.width, img.height, 3, sdr.data(), 95),
              0);
    int w = 0;
    int h = 0;
    const Bytes want = stb_decode(plain, 3, w, h);
    EXPECT_TRUE(matches(decode(out), Pixels{w, h, want}, Tolerance{0, 0.0}, "plain stb JPEG"));
}

// The gain map decodes to the codes make_gain_map gave, within what JPEG at
// gain_map_jpeg_quality loses: exactly stb's own decode of those codes encoded at that quality,
// and within a few codes of them.
TEST(UltraHdrJpeg, GainMapDecodesToItsCodes) {
    const Pictures p = ramp(96, 64);
    const prism::UltraHdrJpeg file = prism::encode_ultra_hdr_jpeg(p.sdr, p.hdr, ramp_headroom);
    Parsed f;
    ASSERT_TRUE(parse(file.bytes, f));
    int w = 0;
    int h = 0;
    const Bytes got =
        stb_decode(std::span(file.bytes).subspan(f.gain_map_at, f.gain_map_size), 1, w, h);
    ASSERT_EQ(w, 96);
    ASSERT_EQ(h, 64);
    ASSERT_EQ(got.size(), file.gain_map.codes.size());

    Bytes alone;
    ASSERT_NE(stbi_write_jpg_to_func(stb_append, &alone, 96, 64, 1, file.gain_map.codes.data(),
                                     prism::gain_map_jpeg_quality),
              0);
    int aw = 0;
    int ah = 0;
    EXPECT_EQ(got, stb_decode(alone, 1, aw, ah));

    int worst = 0;
    double total = 0;
    for (std::size_t i = 0; i < got.size(); ++i) {
        const int d = std::abs(int(got[i]) - int(file.gain_map.codes[i]));
        worst = std::max(worst, d);
        total += d;
    }
    EXPECT_LE(worst, 4);
    EXPECT_LE(total / double(got.size()), 1.0);
    // Stored as grey: decoded as RGB, the three channels agree.
    const Bytes rgb =
        stb_decode(std::span(file.bytes).subspan(f.gain_map_at, f.gain_map_size), 3, w, h);
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
        ASSERT_LE(std::abs(int(rgb[i]) - int(rgb[i + 1])), 1) << i / 3;
        ASSERT_LE(std::abs(int(rgb[i]) - int(rgb[i + 2])), 1) << i / 3;
    }
}

// The gain map is made from the primary as a viewer decodes it (#96), not from the 8-bit SDR
// bytes before JPEG: make_gain_map on stb's decode of the primary gives the same map.
TEST(UltraHdrJpeg, GainMapIsMadeFromTheDecodedPrimary) {
    const Pictures p = ramp(96, 64);
    const prism::UltraHdrJpeg file = prism::encode_ultra_hdr_jpeg(p.sdr, p.hdr, ramp_headroom);
    int w = 0;
    int h = 0;
    const Bytes primary = stb_decode(file.bytes, 3, w, h);
    ASSERT_EQ(primary.size(), p.sdr.size());
    ASSERT_NE(primary, p.sdr);  // the JPEG changes some pixels, or this test proves nothing
    const prism::GainMap want = prism::make_gain_map(primary, p.hdr, ramp_headroom);
    EXPECT_EQ(file.gain_map.codes, want.codes);
    EXPECT_EQ(file.gain_map.metadata, want.metadata);
}

// --- Writing -----------------------------------------------------------------------------------

TEST(UltraHdrJpeg, SaveWritesTheEncodedFileAndReplacesAnOldOne) {
    const Pictures p = ramp();
    const fs::path out = temp_file("ramp.jpg");
    const RemoveOnExit remove_out{out};
    {
        std::ofstream old(out, std::ios::binary);
        old << "an older file";
    }
    prism::save_ultra_hdr_jpeg(out, p.sdr, p.hdr, ramp_headroom);
    EXPECT_EQ(read_file(out), ramp_file().bytes);
    EXPECT_FALSE(fs::exists(fs::path(out).concat(".tmp")));
}

TEST(UltraHdrJpeg, SaveIntoAMissingFolderThrowsAndLeavesNothing) {
    const Pictures p = ramp();
    const fs::path folder = temp_file("missing");
    const fs::path out = folder / "ramp.jpg";
    try {
        prism::save_ultra_hdr_jpeg(out, p.sdr, p.hdr, ramp_headroom);
        FAIL() << "no exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("cannot save image"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(fs::exists(folder));
}

TEST(UltraHdrJpeg, BadInputsAreRefused) {
    const Pictures p = ramp();
    // Sizes that disagree.
    EXPECT_THROW((void)prism::encode_ultra_hdr_jpeg(std::span(p.sdr).first(p.sdr.size() - 3), p.hdr,
                                                    ramp_headroom),
                 std::invalid_argument);
    EXPECT_THROW((void)prism::encode_ultra_hdr_jpeg(p.sdr, p.hdr, 0.0f), std::invalid_argument);
    // A JPEG that already has an APP1 (EXIF, XMP) is not stb's: refused, not mixed in.
    const prism::GainMapMetadata m = ramp_file().gain_map.metadata;
    Bytes base;
    ASSERT_NE(stbi_write_jpg_to_func(stb_append, &base, 37, 23, 3, p.sdr.data(), 95), 0);
    Bytes with_app1 = base;
    const Bytes app1 = {0xFF, 0xE1, 0x00, 0x04, 'x', 'y'};
    // After stb's APP0 (2 + 18 bytes), where an EXIF APP1 would sit.
    ASSERT_EQ(with_app1[3], 0xE0);
    with_app1.insert(with_app1.begin() + 2 + 2 + 16, app1.begin(), app1.end());
    // Before it too.
    Bytes app1_first = base;
    app1_first.insert(app1_first.begin() + 2, app1.begin(), app1.end());
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(app1_first, base, m), std::invalid_argument);
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(with_app1, base, m), std::invalid_argument);
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(base, with_app1, m), std::invalid_argument);
    // Not a JPEG, or cut short.
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(Bytes{1, 2, 3, 4}, base, m),
                 std::invalid_argument);
    EXPECT_THROW(
        (void)prism::assemble_ultra_hdr_jpeg(std::span(base).first(base.size() - 1), base, m),
        std::invalid_argument);
    // An APPn among the tables (after DQT) is refused too.
    Bytes app1_late = base;
    const std::size_t dqt = 2 + 18;
    ASSERT_EQ(app1_late[dqt + 1], 0xDB);
    const std::size_t after_dqt = dqt + 2 + ((std::size_t(base[dqt + 2]) << 8) | base[dqt + 3]);
    app1_late.insert(app1_late.begin() + std::ptrdiff_t(after_dqt), app1.begin(), app1.end());
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(app1_late, base, m), std::invalid_argument);
    // A table whose length runs past the end, in a file that still ends with EOI.
    Bytes long_dqt = {0xFF, 0xD8, 0xFF, 0xDB, 0x40, 0x00, 0x00, 0xFF, 0xD9};
    try {
        (void)prism::assemble_ultra_hdr_jpeg(long_dqt, base, m);
        ADD_FAILURE() << "no exception for a DQT running past the end";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("runs past its end"), std::string::npos) << e.what();
    }
    Bytes long_dqt_after_app0(base.begin(), base.begin() + 20);
    long_dqt_after_app0.insert(long_dqt_after_app0.end(),
                               {0xFF, 0xDB, 0x40, 0x00, 0x00, 0xFF, 0xD9});
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(long_dqt_after_app0, base, m),
                 std::invalid_argument);
    // A value no ISO 21496-1 fraction holds.
    prism::GainMapMetadata nan = m;
    nan.max_log2 = NAN;
    EXPECT_THROW((void)prism::assemble_ultra_hdr_jpeg(base, base, nan), std::invalid_argument);
}

// --- ImageIO (macOS) ---------------------------------------------------------------------------

#ifdef __APPLE__

namespace {

struct CfRelease {
    void operator()(const void* p) const noexcept {
        if (p != nullptr) {
            CFRelease(p);
        }
    }
};
template <class T>
using Cf = std::unique_ptr<std::remove_pointer_t<T>, CfRelease>;

// `img` drawn into extended linear sRGB floats, RGBA per pixel, top row first.
std::vector<float> render_linear(CGImageRef img) {
    const std::size_t w = CGImageGetWidth(img);
    const std::size_t h = CGImageGetHeight(img);
    std::vector<float> buf(w * h * 4);
    const Cf<CGColorSpaceRef> cs(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
    const Cf<CGContextRef> ctx(
        CGBitmapContextCreate(buf.data(), w, h, 32, w * 16, cs.get(),
                              static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
                                  kCGBitmapFloatComponents | kCGBitmapByteOrder32Little));
    CGContextDrawImage(ctx.get(), CGRectMake(0, 0, double(w), double(h)), img);
    return buf;
}

}  // namespace

// ImageIO finds the ISO gain map (it reports Ultra HDR, hdrgm too, as ISOGainMap; its own
// HDRGainMap type is Apple's format and never appears for these files, spike #98), and decodes the
// file to HDR: a patch whose HDR light is 4x white comes back at about 4, a grey one unchanged.
TEST(UltraHdrJpeg, ImageIOFindsTheGainMapAndDecodesToHdr) {
    if (__builtin_available(macOS 15, *)) {
        constexpr int w = 64;
        constexpr int h = 32;
        prism::Image hdr{w, h, std::vector<float>(std::size_t(w) * h * 3)};
        prism::Image sdr = hdr;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 3; ++c) {
                    const std::size_t i = (std::size_t(y) * w + x) * 3 + c;
                    hdr.pixels[i] = x < w / 2 ? 4.0f : 0.18f;
                    sdr.pixels[i] = x < w / 2 ? 1.0f : 0.18f;
                }
            }
        }
        const fs::path out = temp_file("patches.jpg");
        const RemoveOnExit remove_out{out};
        prism::save_ultra_hdr_jpeg(out, prism::encode_srgb8(sdr), hdr, 2.0f);

        const std::string path = out.string();
        const Cf<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8*>(path.c_str()), CFIndex(path.size()), false));
        const Cf<CGImageSourceRef> source(CGImageSourceCreateWithURL(url.get(), nullptr));
        ASSERT_NE(source, nullptr);
        const Cf<CFDictionaryRef> info(CGImageSourceCopyAuxiliaryDataInfoAtIndex(
            source.get(), 0, kCGImageAuxiliaryDataTypeISOGainMap));
        EXPECT_NE(info, nullptr) << "ImageIO found no ISO gain map";

        const void* keys[] = {kCGImageSourceDecodeRequest};
        const void* values[] = {kCGImageSourceDecodeToHDR};
        const Cf<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 1,
                                                             &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks));
        const Cf<CGImageRef> image(CGImageSourceCreateImageAtIndex(source.get(), 0, options.get()));
        ASSERT_NE(image, nullptr);
        ASSERT_EQ(CGImageGetWidth(image.get()), std::size_t(w));
        const std::vector<float> px = render_linear(image.get());
        const auto mean_green = [&](int x0, int x1) {
            double sum = 0;
            int n = 0;
            for (int y = 8; y < h - 8; ++y) {
                for (int x = x0; x < x1; ++x) {
                    sum += px[(std::size_t(y) * w + x) * 4 + 1];
                    ++n;
                }
            }
            return sum / n;
        };
        EXPECT_NEAR(mean_green(8, w / 2 - 8), 4.0, 0.08);
        EXPECT_NEAR(mean_green(w / 2 + 8, w - 8), 0.18, 0.01);
    } else {
        GTEST_SKIP() << "ImageIO reads ISO gain maps from macOS 15";
    }
}

#endif  // __APPLE__
