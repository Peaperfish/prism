#pragma once

#include <string_view>

namespace prism {

// Returns the Prism version string, e.g. "0.3.1".
[[nodiscard]] std::string_view version() noexcept;

}  // namespace prism
