#pragma once

#include <string_view>

namespace prism {

// Returns the Prism version string, e.g. "0.2.0".
[[nodiscard]] std::string_view version() noexcept;

}  // namespace prism
