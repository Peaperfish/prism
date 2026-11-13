#include "prism/gui/system_memory.hpp"

#include <cstddef>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace prism::gui {

std::uint64_t physical_memory_bytes() noexcept {
#if defined(__APPLE__)
    std::uint64_t bytes = 0;
    std::size_t length = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) != 0 || length != sizeof(bytes)) {
        return 0;
    }
    return bytes;
#elif defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return static_cast<std::uint64_t>(status.ullTotalPhys);
#elif defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
#else
    return 0;
#endif
}

}  // namespace prism::gui
