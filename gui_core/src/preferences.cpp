#include "prism/gui/preferences.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>

#include "prism/utf8_path.hpp"

namespace prism::gui {

namespace fs = std::filesystem;

namespace {

std::string_view trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// The whole of `s` as an integer, or nothing (also for trailing junk such as "320px").
std::optional<int> parse_int(std::string_view s) {
    int value = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc() || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<ThemeChoice> parse_theme(std::string_view s) {
    if (s == "system") {
        return ThemeChoice::system;
    }
    if (s == "light") {
        return ThemeChoice::light;
    }
    if (s == "dark") {
        return ThemeChoice::dark;
    }
    return std::nullopt;
}

const char* theme_name(ThemeChoice t) {
    switch (t) {
        case ThemeChoice::light:
            return "light";
        case ThemeChoice::dark:
            return "dark";
        case ThemeChoice::system:
            break;
    }
    return "system";
}

}  // namespace

Theme resolve_theme(ThemeChoice choice, SystemTheme system) {
    switch (choice) {
        case ThemeChoice::light:
            return Theme::light;
        case ThemeChoice::dark:
            return Theme::dark;
        case ThemeChoice::system:
            break;
    }
    return system == SystemTheme::light ? Theme::light : Theme::dark;
}

int clamp_interface_size(int percent) {
    if (percent == interface_size_auto) {
        return interface_size_auto;
    }
    const int clamped = std::clamp(percent, interface_size_min, interface_size_max);
    const int steps = (clamped - interface_size_min + interface_size_step / 2) / interface_size_step;
    return interface_size_min + steps * interface_size_step;
}

int clamp_panel_width(int points) {
    return std::clamp(points, panel_width_min, panel_width_max);
}

float interface_scale(const Preferences& prefs, float display_scale) {
    const int size = clamp_interface_size(prefs.interface_size);
    if (size == interface_size_auto) {
        return display_scale > 0 ? display_scale : 1.0f;
    }
    return static_cast<float>(size) / 100.0f;
}

Preferences parse_preferences(std::string_view text) {
    Preferences prefs;
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);

        const std::size_t equals = line.find('=');
        if (line.empty() || line.front() == '#' || equals == std::string_view::npos) {
            continue;  // blank, a comment, or not a key=value line: skipped
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key == "theme") {
            prefs.theme = parse_theme(value).value_or(ThemeChoice::system);
        } else if (key == "interface_size") {
            const std::optional<int> percent = value == "auto" ? interface_size_auto
                                                               : parse_int(value);
            prefs.interface_size = clamp_interface_size(percent.value_or(interface_size_auto));
        } else if (key == "panel_width") {
            prefs.panel_width = clamp_panel_width(parse_int(value).value_or(panel_width_default));
        } else if (!key.empty()) {
            prefs.unknown.emplace_back(std::string(key), std::string(value));
        }
    }
    return prefs;
}

std::string serialize_preferences(const Preferences& prefs) {
    const int size = clamp_interface_size(prefs.interface_size);
    std::string text = "# Prism preferences\n";
    text += "theme=" + std::string(theme_name(prefs.theme)) + "\n";
    text += "interface_size=" +
            (size == interface_size_auto ? std::string("auto") : std::to_string(size)) + "\n";
    text += "panel_width=" + std::to_string(clamp_panel_width(prefs.panel_width)) + "\n";
    for (const auto& [key, value] : prefs.unknown) {
        text += key + "=" + value + "\n";
    }
    return text;
}

Preferences load_preferences(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return parse_preferences(text);
}

std::optional<std::string> save_preferences(const fs::path& path, const Preferences& prefs) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        const std::string text = serialize_preferences(prefs);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            fs::remove(temp, ignored);
            return "Couldn't write " + utf8(temp) + ".";
        }
    }
    // rename replaces an existing file in one step (on Windows too: MoveFileEx with
    // MOVEFILE_REPLACE_EXISTING), so readers see either the old file or the new one.
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return "Couldn't save " + utf8(path) + ": " + ec.message();
    }
    return std::nullopt;
}

}  // namespace prism::gui
