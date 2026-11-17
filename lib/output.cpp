#include "prism/output.hpp"

#include <stdexcept>
#include <string>

#include "file_ext.hpp"
#include "prism/ultra_hdr.hpp"
#include "prism/utf8_path.hpp"

namespace prism {

bool makes_hdr(std::optional<float> headroom_stops) noexcept {
    return headroom_stops && *headroom_stops > 0.0f;  // false for NaN
}

OutputFile output_file(const std::filesystem::path& path, std::optional<float> headroom_stops) {
    const std::string ext = detail::lowercase_extension(path);
    const bool hdr = makes_hdr(headroom_stops);
    if (ext == ".png") {
        return hdr ? OutputFile::hdr_needs_jpeg : OutputFile::png;
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return hdr ? OutputFile::ultra_hdr_jpeg : OutputFile::jpeg;
    }
    return OutputFile::unsupported;
}

ProcessResult process_for_output(Image& img, Image& hdr, const ProcessSettings& settings,
                                 OutputFile file, int threads, std::stop_token stop,
                                 const StageCallback& on_stage,
                                 const BeforeStageCallback& before_stage) {
    if (file == OutputFile::ultra_hdr_jpeg) {
        return process_hdr(img, hdr, settings, threads, std::move(stop), on_stage, before_stage);
    }
    return process(img, settings, threads, std::move(stop), on_stage, before_stage);
}

void save_output(const std::filesystem::path& path, const Image& img, const Image& hdr,
                 std::optional<float> headroom_stops, int threads,
                 std::span<const std::uint8_t> srgb8) {
    switch (output_file(path, headroom_stops)) {
        case OutputFile::png:
        case OutputFile::jpeg:
            if (srgb8.empty()) {
                save_image(path, img, threads);
            } else {
                save_srgb8(path, srgb8, img.width, img.height);
            }
            return;
        case OutputFile::ultra_hdr_jpeg:
            if (srgb8.empty()) {
                save_ultra_hdr_jpeg(path, encode_srgb8(img, threads), hdr, *headroom_stops,
                                    threads);
            } else {
                save_ultra_hdr_jpeg(path, srgb8, hdr, *headroom_stops, threads);
            }
            return;
        case OutputFile::hdr_needs_jpeg:
            throw std::runtime_error("cannot save image '" + utf8(path) + "': HDR files need .jpg");
        case OutputFile::unsupported: break;
    }
    throw std::runtime_error("cannot save image '" + utf8(path) +
                             "': unsupported extension (use .png, .jpg or .jpeg)");
}

}  // namespace prism
