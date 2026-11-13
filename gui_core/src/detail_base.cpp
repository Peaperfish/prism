#include "prism/gui/detail_base.hpp"

#include <limits>

namespace prism::gui {

std::uint64_t full_size_bytes(PixelSize size) noexcept {
    if (size.width <= 0 || size.height <= 0) {
        return 0;
    }
    constexpr std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    const auto w = static_cast<std::uint64_t>(size.width);
    const auto h = static_cast<std::uint64_t>(size.height);
    // w and h are below 2^31, so w x h fits; only the x 12 can overflow.
    const std::uint64_t pixels = w * h;
    return pixels > max / 12 ? max : pixels * 12;
}

DetailPlan plan_detail_base(PixelSize decode_size, PixelSize preview_size,
                            bool preview_is_same_decode, std::uint64_t physical_memory) noexcept {
    if (preview_is_same_decode && preview_size.width == decode_size.width &&
        preview_size.height == decode_size.height) {
        return DetailPlan::not_needed;
    }
    if (physical_memory == 0) {
        return DetailPlan::read;
    }
    // bytes > memory / 4, exactly: 4 x bytes > memory, without overflowing 4 x bytes.
    const std::uint64_t bytes = full_size_bytes(decode_size);
    const bool too_big =
        bytes > std::numeric_limits<std::uint64_t>::max() / 4 || bytes * 4 > physical_memory;
    return too_big ? DetailPlan::too_big : DetailPlan::read;
}

DetailBaseState detail_base_state(bool enabled, DetailPlan plan, bool ready, bool failed) noexcept {
    if (!enabled) {
        return DetailBaseState::off;
    }
    switch (plan) {
        case DetailPlan::not_needed:
            return DetailBaseState::not_needed;
        case DetailPlan::too_big:
            return DetailBaseState::too_big;
        case DetailPlan::read:
            break;
    }
    if (ready) {
        return DetailBaseState::ready;
    }
    return failed ? DetailBaseState::failed : DetailBaseState::reading;
}

}  // namespace prism::gui
