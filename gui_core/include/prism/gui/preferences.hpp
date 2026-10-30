#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::gui {

// The app's own settings (not a photo's): how the window looks. Kept in a small text file of
// key=value lines in the per-user folder, e.g.
//
//   # Prism preferences
//   theme=system
//   interface_size=auto
//   panel_width=320
//
// Reading never fails: a missing or unreadable file gives the defaults, and a key whose value is
// missing, malformed or out of range falls back to its default (or is clamped) on its own, without
// affecting the other keys. Keys Prism doesn't know (e.g. from a newer version) are kept as they
// are and written back, so going back and forth between versions loses nothing.

// Appearance. `system` follows the operating system's light or dark mode, live.
enum class ThemeChoice { system, light, dark };
enum class Theme { light, dark };

// What the operating system says (SDL_GetSystemTheme). Unknown on systems without the notion.
enum class SystemTheme { unknown, light, dark };

// The interface size as a percentage, in steps of 25 from 75 to 200. 0 means auto: follow the
// display's scale (e.g. 150% on a Windows laptop set to 150%).
inline constexpr int interface_size_auto = 0;
inline constexpr int interface_size_min = 75;
inline constexpr int interface_size_max = 200;
inline constexpr int interface_size_step = 25;

// The control panel's width in points (before the interface scale). The window may still show it
// narrower or wider than this, see panel_width_pixels in layout.hpp.
inline constexpr int panel_width_default = 320;
inline constexpr int panel_width_min = 240;
inline constexpr int panel_width_max = 1200;

struct Preferences {
    ThemeChoice theme = ThemeChoice::system;
    int interface_size = interface_size_auto;
    int panel_width = panel_width_default;
    // Lines with keys this version doesn't know, as key/value pairs in file order.
    std::vector<std::pair<std::string, std::string>> unknown;

    bool operator==(const Preferences&) const = default;
};

// The choice resolved to what to draw: `system` follows the OS, and dark when the OS doesn't say
// (a photo editor's usual look).
[[nodiscard]] Theme resolve_theme(ThemeChoice choice, SystemTheme system);

// A valid interface size: auto stays auto; anything else is clamped to 75..200 and rounded to the
// nearest step of 25 (ties upwards: 87 -> 75, 88 -> 100).
[[nodiscard]] int clamp_interface_size(int percent);

// The panel width clamped to panel_width_min..panel_width_max.
[[nodiscard]] int clamp_panel_width(int points);

// The UI's scale factor: the display's scale for auto, otherwise the chosen percentage.
[[nodiscard]] float interface_scale(const Preferences& prefs, float display_scale);

// The file's text, and back. parse_preferences never fails (see above); serialize_preferences
// writes every known key, then the unknown ones.
[[nodiscard]] Preferences parse_preferences(std::string_view text);
[[nodiscard]] std::string serialize_preferences(const Preferences& prefs);

// Reads `path`; the defaults if it is missing or can't be read.
[[nodiscard]] Preferences load_preferences(const std::filesystem::path& path);

// Writes `path` atomically: to a temporary file next to it, then renamed over it, so a crash or a
// full disk never leaves a half-written file behind. Nothing on success, otherwise a message.
[[nodiscard]] std::optional<std::string> save_preferences(const std::filesystem::path& path,
                                                          const Preferences& prefs);

}  // namespace prism::gui
