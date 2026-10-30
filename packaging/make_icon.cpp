// Draws Prism's placeholder app icon at build time: a faceted glass prism on a dark rounded
// square, its right face tinted with a soft spectrum. No image file is committed; the build runs
// this to get the sizes each OS wants.
//
//   prism_make_icon --iconset <dir>   PNGs named as macOS's iconutil expects (<dir>.iconset)
//   prism_make_icon --ico <file>      a Windows .ico holding PNGs from 16 to 256 px

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "stb_image_write.h"

namespace {

namespace fs = std::filesystem;

struct Colour {
    float r, g, b, a;
};

struct Point {
    float x, y;
};

// Puts `top` over `bottom` (straight alpha).
Colour over(Colour top, Colour bottom) {
    const float a = top.a + bottom.a * (1 - top.a);
    if (a <= 0) {
        return {0, 0, 0, 0};
    }
    auto mix = [&](float t, float b) { return (t * top.a + b * bottom.a * (1 - top.a)) / a; };
    return {mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), a};
}

// The icon's body, Apple's rounded square: 100/1024 margin on each side.
bool in_body(Point p) {
    constexpr float lo = 100.0f / 1024, hi = 1 - lo, radius = 0.18f;
    if (p.x < lo || p.x > hi || p.y < lo || p.y > hi) {
        return false;
    }
    const float cx = std::clamp(p.x, lo + radius, hi - radius);
    const float cy = std::clamp(p.y, lo + radius, hi - radius);
    return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= radius * radius;
}

float cross(Point o, Point a, Point b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// The prism seen slightly from the side: a triangle split by the front edge into a lit left face
// and a right face that spreads the light.
constexpr Point apex{0.50f, 0.20f};
constexpr Point base_left{0.20f, 0.76f};
constexpr Point base_right{0.80f, 0.76f};
constexpr Point front{0.44f, 0.80f};  // the front edge runs from the apex to here

bool in_triangle(Point p, Point a, Point b, Point c) {
    const float d1 = cross(a, b, p);
    const float d2 = cross(b, c, p);
    const float d3 = cross(c, a, p);
    return (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
}

// Distance from `p` to the segment a-b.
float segment_distance(Point p, Point a, Point b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.0f,
                               1.0f);
    const float ex = p.x - (a.x + t * dx), ey = p.y - (a.y + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}

// A spectrum colour for t in [0, 1], red to violet.
Colour hue(float t) {
    const float h = 6 * std::clamp(t, 0.0f, 1.0f) * 0.8f;  // stop at violet, before magenta
    auto channel = [&](float shift) {
        const float k = std::fmod(h + shift, 6.0f);
        return 1 - std::clamp(std::min(k, 4 - k), 0.0f, 1.0f);
    };
    return {channel(5), channel(3), channel(1), 1};
}

Colour sample(Point p) {
    if (!in_body(p)) {
        return {0, 0, 0, 0};
    }
    // Dark blue-grey background, a little lighter at the top.
    const float shade = 0.20f - 0.10f * p.y;
    Colour c{shade, shade + 0.02f, shade + 0.07f, 1};

    const float edge = std::min({segment_distance(p, apex, base_left),
                                 segment_distance(p, base_left, front),
                                 segment_distance(p, front, base_right),
                                 segment_distance(p, base_right, apex),
                                 segment_distance(p, apex, front)});
    if (in_triangle(p, apex, base_left, front)) {
        c = over({0.86f, 0.92f, 1.0f, 0.55f + 0.25f * (0.80f - p.y)}, c);
    } else if (in_triangle(p, apex, front, base_right)) {
        // Across the face from the front edge to the right one, red to violet.
        const float v = std::min((p.y - apex.y) / (front.y - apex.y), 1.0f);
        const float x_front = apex.x + (front.x - apex.x) * v;
        const float x_right = apex.x + (base_right.x - apex.x) *
                                           std::min((p.y - apex.y) / (base_right.y - apex.y), 1.0f);
        const float t = (p.x - x_front) / std::max(x_right - x_front, 0.001f);
        Colour h = hue(t);
        h.a = 0.6f;
        c = over(h, over({0.55f, 0.62f, 0.78f, 0.6f}, c));
    } else if (edge >= 0.012f) {
        return c;
    }
    if (edge < 0.012f) {
        c = over({0.96f, 0.98f, 1.0f, 0.9f}, c);
    }
    return c;
}

// The icon at `size` x `size` pixels, RGBA, with 4 x 4 samples per pixel for smooth edges.
std::vector<std::uint8_t> render(int size) {
    constexpr int n = 4;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < n; ++sy) {
                for (int sx = 0; sx < n; ++sx) {
                    const Colour c = sample({(x + (sx + 0.5f) / n) / size,
                                             (y + (sy + 0.5f) / n) / size});
                    r += c.r * c.a;  // premultiplied, so transparent samples add no colour
                    g += c.g * c.a;
                    b += c.b * c.a;
                    a += c.a;
                }
            }
            auto byte = [](float v) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255));
            };
            std::uint8_t* px = &rgba[(static_cast<std::size_t>(y) * size + x) * 4];
            px[0] = byte(a > 0 ? r / a : 0);
            px[1] = byte(a > 0 ? g / a : 0);
            px[2] = byte(a > 0 ? b / a : 0);
            px[3] = byte(a / (n * n));
        }
    }
    return rgba;
}

std::vector<std::uint8_t> png(int size) {
    const std::vector<std::uint8_t> rgba = render(size);
    std::vector<std::uint8_t> out;
    stbi_write_png_to_func(
        [](void* context, void* data, int length) {
            auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* d = static_cast<const std::uint8_t*>(data);
            bytes->insert(bytes->end(), d, d + length);
        },
        &out, size, size, 4, rgba.data(), size * 4);
    return out;
}

bool write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool write_iconset(const fs::path& dir) {
    fs::create_directories(dir);
    for (int size : {16, 32, 128, 256, 512}) {
        const std::string name = "icon_" + std::to_string(size) + "x" + std::to_string(size);
        if (!write_file(dir / (name + ".png"), png(size)) ||
            !write_file(dir / (name + "@2x.png"), png(2 * size))) {
            return false;
        }
    }
    return true;
}

// An .ico file: a directory of images, each stored as a PNG (Windows Vista and later).
bool write_ico(const fs::path& path) {
    constexpr std::array sizes = {16, 24, 32, 48, 64, 128, 256};
    std::vector<std::vector<std::uint8_t>> images;
    for (int size : sizes) {
        images.push_back(png(size));
    }
    std::vector<std::uint8_t> out;
    auto u16 = [&](unsigned v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto u32 = [&](std::size_t v) {
        u16(static_cast<unsigned>(v & 0xffff));
        u16(static_cast<unsigned>(v >> 16));
    };
    u16(0);  // reserved
    u16(1);  // type: icon
    u16(static_cast<unsigned>(sizes.size()));
    std::size_t offset = 6 + 16 * sizes.size();
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto side = static_cast<std::uint8_t>(sizes[i] == 256 ? 0 : sizes[i]);  // 0 = 256
        out.push_back(side);
        out.push_back(side);
        out.push_back(0);  // no palette
        out.push_back(0);  // reserved
        u16(1);            // colour planes
        u16(32);           // bits per pixel
        u32(images[i].size());
        u32(offset);
        offset += images[i].size();
    }
    for (const auto& image : images) {
        out.insert(out.end(), image.begin(), image.end());
    }
    return write_file(path, out);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s --iconset <dir> | --ico <file>\n", argv[0]);
        return 2;
    }
    const std::string_view mode = argv[1];
    const fs::path target = argv[2];
    bool ok = false;
    if (mode == "--iconset") {
        ok = write_iconset(target);
    } else if (mode == "--ico") {
        ok = write_ico(target);
    } else {
        std::fprintf(stderr, "Unknown option %s\n", argv[1]);
        return 2;
    }
    if (!ok) {
        std::fprintf(stderr, "Couldn't write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
