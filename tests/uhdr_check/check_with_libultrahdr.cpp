// Checks Prism's Ultra HDR JPEGs with libultrahdr (PRISM_CHECK_LIBULTRAHDR, test only). Links
// libultrahdr, never prism_core: everything it knows about a file comes from the fixtures that
// write_fixtures.cpp wrote with Prism (<name>.jpg, .txt, .f32) and from the stored ICC profile.
// For each case, libultrahdr must
//   - accept the file as Ultra HDR and find a gain map of the picture's size;
//   - read back the metadata Prism wrote: each value within 1e-6 (stops, or as stored);
//   - hand back the ICC profile Prism embedded (third_party/icc/sRGB-v2-magic.icc);
//   - decode it, linear, at a display boost of 2^HDRCapacityMax (weight 1), to within the
//     tolerance below of Prism's own reader (apply_gain_map on stb's decodes).
// Tolerance, in stops of luminance |log2((Y_libultrahdr + 1/64) / (Y_prism + 1/64))| per pixel:
// at most 0.03 at the 99.9th percentile and 0.10 anywhere. The two decoders differ only in their
// JPEG IDCTs (libjpeg-turbo against stb, about one level), libultrahdr's half-float output and its
// gain lookup table. On three 2048-pixel a7RV exports (#104's probe) they agreed to 0.024 at the
// 99.9th percentile and 0.065 at worst; on the fixtures here, to 0.0066 and 0.023.
//
// usage: prism_uhdr_check <fixture_dir> <icc_file> <case>...
// Prints one line per check; exit status 1 if any fails.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <ultrahdr_api.h>

namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr double tolerance_p999_stops = 0.03;
constexpr double tolerance_max_stops = 0.10;
constexpr double metadata_tolerance = 1e-6;

Bytes read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

float half_to_float(std::uint16_t h) {
    const int sign = (h >> 15) & 1;
    const int exponent = (h >> 10) & 0x1F;
    const int mantissa = h & 0x3FF;
    float v = 0.0f;
    if (exponent == 0) {
        v = std::ldexp(float(mantissa), -24);
    } else if (exponent == 31) {
        v = mantissa != 0 ? NAN : INFINITY;
    } else {
        v = std::ldexp(float(mantissa | 0x400), exponent - 25);
    }
    return sign != 0 ? -v : v;
}

double luminance(double r, double g, double b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

int failures = 0;

void report(bool ok, const std::string& name, const char* what, const std::string& detail) {
    std::printf("%s %s: %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), what,
                detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) {
        ++failures;
    }
}

std::string number(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.9g", v);
    return buffer;
}

void check_case(const std::string& dir, const Bytes& icc, const std::string& name) {
    const std::string base = dir + "/" + name;
    Bytes file = read_file(base + ".jpg");
    std::map<std::string, double> want;
    {
        std::ifstream in(base + ".txt", std::ios::binary);
        std::string key;
        double value = 0;
        while (in >> key >> value) {
            want[key] = value;
        }
    }
    const int width = int(want["width"]);
    const int height = int(want["height"]);
    std::vector<float> prism_rgb(std::size_t(width) * height * 3);
    {
        std::ifstream in(base + ".f32", std::ios::binary);
        in.read(reinterpret_cast<char*>(prism_rgb.data()),
                std::streamsize(prism_rgb.size() * sizeof(float)));
        if (file.empty() || width <= 0 || height <= 0 || !in) {
            report(false, name, "fixtures", "missing or short (" + base + ".*)");
            return;
        }
    }

    report(is_uhdr_image(file.data(), int(file.size())) != 0, name, "is_uhdr_image", "");
    uhdr_codec_private_t* dec = uhdr_create_decoder();
    uhdr_compressed_image_t image{file.data(),         file.size(),         file.size(),
                                  UHDR_CG_UNSPECIFIED, UHDR_CT_UNSPECIFIED, UHDR_CR_UNSPECIFIED};
    uhdr_dec_set_image(dec, &image);
    uhdr_dec_set_out_img_format(dec, UHDR_IMG_FMT_64bppRGBAHalfFloat);
    uhdr_dec_set_out_color_transfer(dec, UHDR_CT_LINEAR);
    uhdr_dec_set_out_max_display_boost(dec, float(std::exp2(want["HDRCapacityMax"])));
    uhdr_error_info_t e = uhdr_dec_probe(dec);
    report(e.error_code == UHDR_CODEC_OK, name, "probe", e.has_detail ? e.detail : "");
    if (e.error_code != UHDR_CODEC_OK) {
        uhdr_release_decoder(dec);
        return;
    }
    report(uhdr_dec_get_gainmap_width(dec) == width && uhdr_dec_get_gainmap_height(dec) == height &&
               uhdr_dec_get_image_width(dec) == width && uhdr_dec_get_image_height(dec) == height,
           name, "sizes",
           std::to_string(uhdr_dec_get_gainmap_width(dec)) + "x" +
               std::to_string(uhdr_dec_get_gainmap_height(dec)));

    const uhdr_gainmap_metadata_t* m = uhdr_dec_get_gainmap_metadata(dec);
    const std::pair<const char*, double> got[] = {
        {"GainMapMin", std::log2(m->min_content_boost[0])},
        {"GainMapMax", std::log2(m->max_content_boost[0])},
        {"Gamma", m->gamma[0]},
        {"OffsetSDR", m->offset_sdr[0]},
        {"OffsetHDR", m->offset_hdr[0]},
        {"HDRCapacityMin", std::log2(m->hdr_capacity_min)},
        {"HDRCapacityMax", std::log2(m->hdr_capacity_max)},
    };
    for (const auto& [key, value] : got) {
        report(std::fabs(value - want[key]) <= metadata_tolerance, name, key,
               "read " + number(value) + ", wrote " + number(want[key]));
    }

    // libultrahdr hands back the APP2 payload: "ICC_PROFILE\0", chunk numbers, the profile.
    const uhdr_mem_block_t* got_icc = uhdr_dec_get_icc(dec);
    const std::size_t head = 14;
    const bool icc_ok = got_icc != nullptr && got_icc->data_sz == head + icc.size() &&
                        std::memcmp(static_cast<const std::uint8_t*>(got_icc->data) + head,
                                    icc.data(), icc.size()) == 0;
    report(icc_ok, name, "ICC profile",
           got_icc != nullptr ? std::to_string(got_icc->data_sz) + " bytes" : "none");

    e = uhdr_decode(dec);
    report(e.error_code == UHDR_CODEC_OK, name, "decode", e.has_detail ? e.detail : "");
    if (e.error_code == UHDR_CODEC_OK) {
        const uhdr_raw_image_t* out = uhdr_get_decoded_image(dec);
        const auto* px = static_cast<const std::uint64_t*>(out->planes[UHDR_PLANE_PACKED]);
        std::vector<double> err;
        err.reserve(std::size_t(width) * height);
        double worst = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::uint64_t p = px[std::size_t(y) * out->stride[UHDR_PLANE_PACKED] + x];
                const double yl = std::max(0.0, luminance(half_to_float(std::uint16_t(p)),
                                                          half_to_float(std::uint16_t(p >> 16)),
                                                          half_to_float(std::uint16_t(p >> 32))));
                const float* q = &prism_rgb[(std::size_t(y) * width + x) * 3];
                const double yp = std::max(0.0, luminance(q[0], q[1], q[2]));
                const double s = std::fabs(std::log2((yl + 1.0 / 64) / (yp + 1.0 / 64)));
                err.push_back(std::isfinite(s) ? s : 1e9);
                worst = std::max(worst, err.back());
            }
        }
        const std::size_t k = std::size_t(std::ceil(0.999 * double(err.size()))) - 1;
        std::nth_element(err.begin(), err.begin() + std::ptrdiff_t(k), err.end());
        report(err[k] <= tolerance_p999_stops && worst <= tolerance_max_stops, name,
               "decode against apply_gain_map",
               "p99.9 " + number(err[k]) + " stops (allowed " + number(tolerance_p999_stops) +
                   "), max " + number(worst) + " (allowed " + number(tolerance_max_stops) + ")");
    }
    uhdr_release_decoder(dec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: prism_uhdr_check <fixture_dir> <icc_file> <case>...\n");
        return 2;
    }
    const Bytes icc = read_file(argv[2]);
    report(icc.size() == 736, "icc", "stored profile", std::to_string(icc.size()) + " bytes");
    for (int i = 3; i < argc; ++i) {
        check_case(argv[1], icc, argv[i]);
    }
    std::printf("%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
