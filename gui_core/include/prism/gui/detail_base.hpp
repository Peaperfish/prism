#pragma once

#include <cstdint>

#include "prism/resize.hpp"

// The detail base (#37): the full-size photo that real-pixel zoom renders from, the export's own
// decode (decode_for_export) kept in memory. These are the pure decisions about it; Session owns
// the base itself and the read that makes it.

namespace prism::gui {

// What a photo needs for its detail base.
enum class DetailPlan {
    not_needed,  // the preview base already holds the export's decode, pixel for pixel: render
                 // from it, nothing is read
    read,        // read the file again at the export's size, in the background
    too_big,     // the full-size photo would take more than a quarter of the computer's memory:
                 // nothing is read, so there are no real pixels to show
};

// The bytes a w x h float image takes: w x h x 3 channels x 4 bytes. Saturates at the largest
// std::uint64_t rather than wrapping.
[[nodiscard]] std::uint64_t full_size_bytes(PixelSize size) noexcept;

// What to do for a photo whose export decode (decode_for_export: the file's own size, or the
// half-size decode for a RAW file with raw_half_size on) is `decode_size`, given the preview
// base's size, whether the preview base was made by that same decode with the same options (so
// it holds the same pixels when it is not reduced), and the computer's physical memory in bytes.
//
//   not_needed  when `preview_is_same_decode` and the preview is `decode_size` (not reduced);
//   too_big     otherwise, when full_size_bytes(decode_size) is more than a quarter of
//               `physical_memory`, decided here before anything is read;
//   read        otherwise, and also when `physical_memory` is 0 (unknown: nothing to compare with,
//               so the read goes ahead and running out of memory is caught as a failed read).
//
// A RAW preview is a half-size decode: it is the export's decode only with raw_half_size on and
// the same camera matrix, never for a full-size export.
[[nodiscard]] DetailPlan plan_detail_base(PixelSize decode_size, PixelSize preview_size,
                                          bool preview_is_same_decode,
                                          std::uint64_t physical_memory) noexcept;

// Where the detail base stands, for the UI.
enum class DetailBaseState {
    off,         // no photo open, or the session doesn't keep a detail base
    not_needed,  // the preview base is the full-size photo (DetailPlan::not_needed)
    reading,     // the full-size photo is being read, or will be once an export has finished
    ready,       // the full-size photo is in memory
    too_big,     // DetailPlan::too_big: nothing was read
    failed,      // the read failed (out of memory, or the file couldn't be read again)
};

// The state for a session that keeps a detail base (`enabled`, with a photo open) whose plan is
// `plan`, given whether the read has delivered the base (`ready`) or failed (`failed`). `ready` and
// `failed` only count when the plan is to read.
[[nodiscard]] DetailBaseState detail_base_state(bool enabled, DetailPlan plan, bool ready,
                                                bool failed) noexcept;

}  // namespace prism::gui
