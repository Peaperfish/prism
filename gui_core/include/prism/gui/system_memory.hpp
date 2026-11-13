#pragma once

#include <cstdint>

namespace prism::gui {

// The computer's physical memory (RAM) in bytes: sysctl hw.memsize on macOS,
// GlobalMemoryStatusEx on Windows, sysconf(_SC_PHYS_PAGES) x the page size on Linux. 0 if the
// system won't say, or on any other platform.
[[nodiscard]] std::uint64_t physical_memory_bytes() noexcept;

}  // namespace prism::gui
