#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>

#include "prism/image.hpp"
#include "prism/process.hpp"

namespace prism {

// --- Writing a processed photo -------------------------------------------------------------------
//
// The one way the command line and the app turn settings into an output file (#105): the file
// name and the headroom choose what is written, process_for_output makes the picture(s) that needs
// and save_output writes them. With no headroom (or 0) this is exactly what Prism did before
// 0.8.0: process(), then save_image (or save_srgb8 for bytes already encoded).

// What an output file holds, from its extension (case-insensitive) and the headroom.
enum class OutputFile {
    png,             // .png with no headroom: an SDR PNG, as before
    jpeg,            // .jpg or .jpeg with no headroom: an SDR JPEG, as before
    ultra_hdr_jpeg,  // .jpg or .jpeg with a headroom above 0 (prism/ultra_hdr.hpp)
    hdr_needs_jpeg,  // .png with a headroom above 0: refused, since a PNG stays SDR (#96, 6)
    unsupported,     // any other extension
};

// True if `headroom_stops` asks for an HDR picture: above 0. Empty, 0, a negative value and NaN
// don't (process_hdr treats empty and 0 as SDR only; the readers never give the others).
[[nodiscard]] bool makes_hdr(std::optional<float> headroom_stops) noexcept;

// What writing `path` with `headroom_stops` gives (see OutputFile).
[[nodiscard]] OutputFile output_file(const std::filesystem::path& path,
                                     std::optional<float> headroom_stops);

// Runs the stages as `file` needs them: process_hdr for an Ultra HDR JPEG, which also fills `hdr`;
// for anything else process(), which ignores the headroom, and `hdr` is left as it is. Arguments,
// results and exceptions as process() and process_hdr().
[[nodiscard]] ProcessResult process_for_output(Image& img, Image& hdr,
                                               const ProcessSettings& settings, OutputFile file,
                                               int threads = 0, std::stop_token stop = {},
                                               const StageCallback& on_stage = {},
                                               const BeforeStageCallback& before_stage = {});

// Writes `path` as output_file(path, headroom_stops) says:
// - png, jpeg: `img` as save_image writes it, or when `srgb8` is given (encode_srgb8 of `img`,
//   made already for something else) those bytes as save_srgb8 writes them; `hdr` is not used;
// - ultra_hdr_jpeg: save_ultra_hdr_jpeg of `img` (as `srgb8`, or encode_srgb8 of it) and `hdr`,
//   process_for_output's two pictures, at the headroom;
// - hdr_needs_jpeg, unsupported: throws std::runtime_error naming the file, writing nothing.
// Throws as save_image and save_ultra_hdr_jpeg do.
void save_output(const std::filesystem::path& path, const Image& img, const Image& hdr,
                 std::optional<float> headroom_stops, int threads = 0,
                 std::span<const std::uint8_t> srgb8 = {});

}  // namespace prism
