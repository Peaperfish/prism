#pragma once

// Test helpers for the SDR golden tests (tests/golden_sdr_test.cpp,
// tests/gui/golden_export_test.cpp); not part of Prism. The references are in tests/data/golden/
// (its README says what they are and how to regenerate them, only on purpose:
// tools/make_sdr_goldens.sh).
//
// Everything is compared as decoded 8-bit sRGB pixels, never as file bytes. The tests decode with
// stb_image (the stb target Prism itself links, used here directly), so an expected value is always
// read from a stored file and never recomputed by the code under test.
//
// Tolerances. Built for Apple arm64 (the macOS CI runners and the maintainer's Mac, where the
// references were made) the pixels must match exactly, PNG and JPEG. That assumes Apple's libm and
// compiler round as on that Mac: if a macOS runner ever differs, it is the place to relax.
// Elsewhere other compilers may round a float differently at the end of the pipeline, which moves a
// sample by at most 1 level, rarely: a PNG may then differ by 1 level in at most 0.5% of the
// samples. stb_image_write's JPEG encoder uses a float DCT, so its bytes can differ between
// compilers, and one input level or one coefficient rounding the other way moves a whole 8 x 8
// block by several levels (a 1-level change in a quarter of the input samples moved the decoded
// JPEG by up to 9). So elsewhere the decoded JPEG is held to the JPEG's own tolerance: no further
// from the decoded reference JPEG, at worst (plus a margin of 2 levels) and on average, than that
// reference is from the reference PNG it encodes (6 to 18 levels at worst, 0.4 to 1.1 on average,
// for these files). Both are computed from the stored files alone. What keeps a correct JPEG inside
// that bound is the PNG rule's 0.5% cap: re-encoding a reference with 0.5% of its samples off by 1
// moved it by at most its own error (by exactly that on raw/bw, hence the margin). Off Apple arm64
// the PNG checks carry the precision; the JPEG check catches a wrong or broken JPEG (quality 92 or
// lower, chroma subsampling, another look). GoldenSdr.TolerancesWork runs both rules on every
// platform.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <stb_image.h>

#include "env_var.hpp"

namespace prism_golden {

namespace fs = std::filesystem;

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
inline constexpr bool exact_platform = true;
#else
inline constexpr bool exact_platform = false;
#endif

inline constexpr int long_side = 256;

// The built-in looks, as in recipes/image/ and recipes/raw/
// (GoldenSdr.EveryBuiltInLookHasAReference checks that these lists and the folders agree).
inline const std::array<const char*, 4> image_looks = {"original", "vivid", "soft", "bw"};
inline const std::array<const char*, 5> raw_looks = {"natural", "original", "vivid", "soft", "bw"};

inline fs::path golden_dir() {
    return fs::path(PRISM_TEST_DATA) / "golden";
}
inline fs::path sample_photo() {
    return golden_dir() / "input" / "prism-window.jpg";
}

struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;  // width * height * 3, row-major
};

struct Tolerance {
    int max_difference = 0;   // per channel, in 8-bit levels
    double max_share = 0.0;   // of all samples, that may differ at all
    double max_mean = 255.0;  // mean absolute difference, in levels
};

inline Tolerance png_tolerance(bool exact = exact_platform) {
    return exact ? Tolerance{0, 0.0} : Tolerance{1, 0.005};
}

inline std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    return bytes;
}

// Decodes a PNG or JPEG to 8-bit RGB with stb_image; empty on failure.
inline Pixels decode(const fs::path& path) {
    const std::vector<std::uint8_t> bytes = read_bytes(path);
    Pixels p;
    if (bytes.empty()) {
        return p;
    }
    int channels = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &p.width,
                                          &p.height, &channels, 3);
    if (data == nullptr) {
        return Pixels{};
    }
    p.rgb.assign(data, data + static_cast<std::size_t>(p.width) * p.height * 3);
    stbi_image_free(data);
    return p;
}

// Compares pixels with a reference; on failure says the largest difference, the share of samples
// that differ and the worst pixel.
inline testing::AssertionResult matches(const Pixels& got, const Pixels& want, const Tolerance& t,
                                        const std::string& what) {
    if (want.rgb.empty()) {
        return testing::AssertionFailure() << what << ": the reference did not decode";
    }
    if (got.width != want.width || got.height != want.height || got.rgb.size() != want.rgb.size()) {
        return testing::AssertionFailure()
               << what << ": " << got.width << " × " << got.height << ", the reference is "
               << want.width << " × " << want.height;
    }
    int max_difference = 0;
    std::size_t differing = 0;
    std::size_t worst = 0;
    double total = 0.0;
    for (std::size_t i = 0; i < got.rgb.size(); ++i) {
        const int d = std::abs(int(got.rgb[i]) - int(want.rgb[i]));
        total += d;
        if (d != 0) {
            ++differing;
        }
        if (d > max_difference) {
            max_difference = d;
            worst = i;
        }
    }
    const double share = double(differing) / double(got.rgb.size());
    const double mean = total / double(got.rgb.size());
    if (max_difference <= t.max_difference && share <= t.max_share && mean <= t.max_mean) {
        return testing::AssertionSuccess();
    }
    const std::size_t pixel = worst / 3;
    const std::size_t first = pixel * 3;
    const auto rgb = [&](const std::vector<std::uint8_t>& v) {
        std::ostringstream s;
        s << "(" << int(v[first]) << ", " << int(v[first + 1]) << ", " << int(v[first + 2]) << ")";
        return s.str();
    };
    return testing::AssertionFailure()
           << what << ": max difference " << max_difference << " (allowed " << t.max_difference
           << "), " << differing << " of " << got.rgb.size() << " samples differ (" << share * 100.0
           << "%, allowed " << t.max_share * 100.0 << "%), mean " << mean << " (allowed "
           << t.max_mean << "); worst pixel (" << pixel % got.width << ", " << pixel / got.width
           << ") is " << rgb(got.rgb) << ", the reference " << rgb(want.rgb);
}

// Checks a JPEG Prism wrote against the reference `<golden>.jpg`, both decoded: exactly on Apple
// arm64, elsewhere within the reference's own error against `<golden>.png` (see the top). `exact`
// picks the rule, so a test can run the other platform's rule too.
inline testing::AssertionResult jpeg_matches(const fs::path& jpeg, const fs::path& golden,
                                             const std::string& what, bool exact = exact_platform) {
    const Pixels want = decode(fs::path(golden).concat(".jpg"));
    if (exact) {
        return matches(decode(jpeg), want, Tolerance{0, 0.0}, what);
    }
    const Pixels source = decode(fs::path(golden).concat(".png"));
    if (want.rgb.empty() || want.rgb.size() != source.rgb.size()) {
        return testing::AssertionFailure() << what
                                           << ": the reference JPEG and PNG don't decode "
                                              "to the same size";
    }
    Tolerance own{0, 1.0, 0.0};
    for (std::size_t i = 0; i < want.rgb.size(); ++i) {
        const int d = std::abs(int(want.rgb[i]) - int(source.rgb[i]));
        own.max_difference = std::max(own.max_difference, d);
        own.max_mean += d;
    }
    own.max_mean /= double(want.rgb.size());
    own.max_difference += 2;
    return matches(decode(jpeg), want, own, what);
}

// --- SHA-256 (FIPS 180-4), for checking the test RAW is the one the references came from --------

// One use per object: add() the data, then call hex() once (it pads the message).
class Sha256 {
public:
    void add(const std::uint8_t* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            block_[fill_++] = data[i];
            if (fill_ == 64) {
                compress();
                fill_ = 0;
            }
        }
        bits_ += std::uint64_t(n) * 8;
    }

    std::string hex() {
        const std::uint64_t bits = bits_;
        const std::uint8_t one = 0x80;
        add(&one, 1);
        const std::uint8_t zero = 0;
        while (fill_ != 56) {
            add(&zero, 1);
        }
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i) {
            length[i] = std::uint8_t(bits >> (56 - 8 * i));
        }
        add(length, 8);
        static const char* digits = "0123456789abcdef";
        std::string s;
        for (const std::uint32_t word : h_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                s += digits[(word >> shift) & 0xf];
            }
        }
        return s;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress() {
        static constexpr std::array<std::uint32_t, 64> k = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        std::array<std::uint32_t, 64> w{};
        for (int i = 0; i < 16; ++i) {
            w[i] = std::uint32_t(block_[4 * i]) << 24 | std::uint32_t(block_[4 * i + 1]) << 16 |
                   std::uint32_t(block_[4 * i + 2]) << 8 | std::uint32_t(block_[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = h_;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; ++i) {
            h_[i] += v[i];
        }
    }

    std::array<std::uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block_{};
    std::size_t fill_ = 0;
    std::uint64_t bits_ = 0;
};

inline std::string sha256_of_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    Sha256 sha;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        sha.add(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                static_cast<std::size_t>(in.gcount()));
    }
    return sha.hex();
}

inline std::string recorded_raw_sha256() {
    std::ifstream in(golden_dir() / "raw" / "source.sha256", std::ios::binary);
    std::string s;
    in >> s;
    return s;
}

// The RAW the references were made from, or why the RAW tests skip: PRISM_TEST_RAW must name a
// file whose SHA-256 is the one in tests/data/golden/raw/source.sha256.
struct GoldenRaw {
    std::optional<fs::path> path;
    std::string skip_reason;
};

inline GoldenRaw golden_raw() {
    const std::string env = prism_test::env_var("PRISM_TEST_RAW");
    if (env.empty()) {
        return {std::nullopt,
                "set PRISM_TEST_RAW to the RAW file in tests/data/golden/raw/source.sha256 to run "
                "this test (CI has none)"};
    }
    const fs::path path(env);
    if (!fs::is_regular_file(path)) {
        return {std::nullopt, "PRISM_TEST_RAW (" + env + ") is not a file"};
    }
    const std::string got = sha256_of_file(path);
    const std::string want = recorded_raw_sha256();
    if (got != want) {
        return {std::nullopt, "PRISM_TEST_RAW (" + env + ") has SHA-256 " + got +
                                  ", not the references' " + want + ": skipped"};
    }
    return {path, ""};
}

}  // namespace prism_golden
