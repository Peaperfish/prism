// Writes Ultra HDR JPEGs with Prism's writer for check_with_libultrahdr.cpp
// (PRISM_CHECK_LIBULTRAHDR, test only). Links prism_core, never libultrahdr. For each case <name>
// it writes, into <out_dir>:
//   <name>.jpg    the file, from save_ultra_hdr_jpeg
//   <name>.txt    the metadata Prism wrote ("key value" lines) and the picture's size
//   <name>.f32    what Prism's own reader shows at weight 1: apply_gain_map on stb's decode of the
//                 primary and of the gain map (stb's decode of the codes at gain_map_jpeg_quality,
//                 which UltraHdrJpeg.GainMapDecodesToItsCodes checks is the embedded map's decode),
//                 linear float RGB, row-major
//
// usage: prism_uhdr_write_fixtures <out_dir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include "prism/gain_map.hpp"
#include "prism/image.hpp"
#include "prism/ultra_hdr.hpp"

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

namespace {

void append(void* context, void* data, int size) {
    auto* out = static_cast<Bytes*>(context);
    out->insert(out->end(), static_cast<std::uint8_t*>(data),
                static_cast<std::uint8_t*>(data) + size);
}

Bytes stb_decode(const Bytes& jpeg, int channels) {
    int w = 0;
    int h = 0;
    int stored = 0;
    stbi_uc* data = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h,
                                          &stored, channels);
    if (data == nullptr) {
        throw std::runtime_error("stb could not decode a JPEG");
    }
    Bytes out(data, data + std::size_t(w) * h * channels);
    stbi_image_free(data);
    return out;
}

// A 512 x 256 picture with light up to `peak` x white: a grey ramp (log-spaced, 1/256 to peak) on
// top, grey patches, and saturated colours at 1 and at peak below.
struct Pictures {
    Bytes sdr;
    prism::Image hdr;
};

Pictures chart(float peak) {
    constexpr int w = 512;
    constexpr int h = 256;
    Pictures p;
    p.hdr = prism::Image{w, h, std::vector<float>(std::size_t(w) * h * 3)};
    prism::Image sdr = p.hdr;
    const float colours[6][3] = {{1, 0.1f, 0.1f}, {0.1f, 1, 0.1f}, {0.1f, 0.1f, 1},
                                 {0.1f, 1, 1},    {1, 0.1f, 1},    {1, 1, 0.1f}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float rgb[3];
            const int column = std::min(x * 6 / w, 5);
            if (y < 96) {
                const float v = std::exp2(-8.0f + (8.0f + std::log2(peak)) * float(x) / (w - 1));
                rgb[0] = rgb[1] = rgb[2] = v;
            } else if (y < 176) {
                const float greys[6] = {0.05f, 0.18f, 0.5f, 1.0f, 2.0f, peak};
                rgb[0] = rgb[1] = rgb[2] = greys[column];
            } else {
                const float k = y < 216 ? 1.0f : peak;
                for (int c = 0; c < 3; ++c) {
                    rgb[c] = colours[column][c] * k;
                }
            }
            for (int c = 0; c < 3; ++c) {
                const std::size_t i = (std::size_t(y) * w + x) * 3 + c;
                p.hdr.pixels[i] = rgb[c];
                sdr.pixels[i] = std::min(rgb[c], 1.0f);
            }
        }
    }
    p.sdr = prism::encode_srgb8(sdr);
    return p;
}

void write_case(const fs::path& dir, const std::string& name, float headroom_stops) {
    const Pictures p = chart(std::exp2(headroom_stops));
    const fs::path jpg = dir / (name + ".jpg");
    prism::save_ultra_hdr_jpeg(jpg, p.sdr, p.hdr, headroom_stops);
    // The metadata written, as encode_ultra_hdr_jpeg (which save calls) made it.
    const prism::UltraHdrJpeg file = prism::encode_ultra_hdr_jpeg(p.sdr, p.hdr, headroom_stops);
    const prism::GainMapMetadata& m = file.gain_map.metadata;
    {
        std::ofstream txt(dir / (name + ".txt"), std::ios::binary);
        char line[512];
        std::snprintf(line, sizeof line,
                      "width %d\nheight %d\nGainMapMin %.9g\nGainMapMax %.9g\nGamma %.9g\n"
                      "OffsetSDR %.9g\nOffsetHDR %.9g\nHDRCapacityMin %.9g\nHDRCapacityMax %.9g\n",
                      p.hdr.width, p.hdr.height, m.min_log2, m.max_log2, m.gamma, m.offset_sdr,
                      m.offset_hdr, m.hdr_capacity_min, m.hdr_capacity_max);
        txt << line;
    }
    Bytes saved;
    {
        std::ifstream in(jpg, std::ios::binary);
        saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (saved != file.bytes) {
        throw std::runtime_error("save_ultra_hdr_jpeg and encode_ultra_hdr_jpeg disagree");
    }
    Bytes map_jpeg;
    if (stbi_write_jpg_to_func(append, &map_jpeg, p.hdr.width, p.hdr.height, 1,
                               file.gain_map.codes.data(), prism::gain_map_jpeg_quality) == 0) {
        throw std::runtime_error("stb could not encode the gain map");
    }
    prism::GainMap decoded = file.gain_map;
    decoded.codes = stb_decode(map_jpeg, 1);
    const prism::Image shown = prism::apply_gain_map(stb_decode(saved, 3), decoded, 1.0f);
    std::ofstream f32(dir / (name + ".f32"), std::ios::binary);
    f32.write(reinterpret_cast<const char*>(shown.pixels.data()),
              static_cast<std::streamsize>(shown.pixels.size() * sizeof(float)));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: prism_uhdr_write_fixtures <out_dir>\n");
        return 2;
    }
    try {
        const fs::path dir = argv[1];
        fs::create_directories(dir);
        write_case(dir, "chart-n2", 2.0f);
        write_case(dir, "chart-n3.5", 3.5f);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "prism_uhdr_write_fixtures: %s\n", e.what());
        return 1;
    }
    return 0;
}
