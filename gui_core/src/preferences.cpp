#include "prism/gui/preferences.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

#include "prism/atomic_file.hpp"
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

const char* scope_name(ScopeChoice c) {
    switch (c) {
        case ScopeChoice::parade:
            return "parade";
        case ScopeChoice::waveform:
            return "waveform";
        case ScopeChoice::vectorscope:
            return "vectorscope";
        case ScopeChoice::histogram:
            break;
    }
    return "histogram";
}

std::optional<ScopeChoice> parse_scope(std::string_view s) {
    for (const ScopeChoice c : {ScopeChoice::histogram, ScopeChoice::parade, ScopeChoice::waveform,
                                ScopeChoice::vectorscope}) {
        if (s == scope_name(c)) {
            return c;
        }
    }
    return std::nullopt;
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

int step_interface_size(int current, int direction, float display_scale) {
    if (direction == 0) {
        return current;
    }
    current = clamp_interface_size(current);
    if (current != interface_size_auto) {
        return clamp_interface_size(current + (direction > 0 ? interface_size_step
                                                              : -interface_size_step));
    }
    // Auto: where the display's scale falls on the preset ladder, in steps (a fraction, since it
    // may lie between two presets). A small tolerance keeps 100.0000001% from counting as above.
    const double percent = (display_scale > 0 ? display_scale : 1.0f) * 100.0;
    const double position = (percent - interface_size_min) / interface_size_step;
    const double last = (interface_size_max - interface_size_min) / interface_size_step;
    constexpr double tolerance = 1e-3;
    if (direction > 0) {
        const double next = std::floor(position + tolerance) + 1;
        return next > last ? interface_size_auto
                           : interface_size_min + static_cast<int>(std::max(next, 0.0)) *
                                                      interface_size_step;
    }
    const double previous = std::ceil(position - tolerance) - 1;
    return previous < 0 ? interface_size_auto
                        : interface_size_min + static_cast<int>(std::min(previous, last)) *
                                                   interface_size_step;
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
        } else if (key == "scope") {
            prefs.scope = parse_scope(value).value_or(ScopeChoice::histogram);
        } else if (key == "clip_overlay") {
            prefs.clip_overlay = value == "on";
        } else if (key == "show_explanations") {
            prefs.show_explanations = value != "off";  // on unless it says off
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
    text += "scope=" + std::string(scope_name(prefs.scope)) + "\n";
    text += std::string("clip_overlay=") + (prefs.clip_overlay ? "on" : "off") + "\n";
    text += std::string("show_explanations=") + (prefs.show_explanations ? "on" : "off") + "\n";
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
    const std::optional<AtomicWriteError> error =
        write_file_atomically(path, serialize_preferences(prefs));
    if (!error) {
        return std::nullopt;
    }
    if (error->step == AtomicWriteError::Step::write) {
        return "Couldn't write " + utf8(error->file) + ".";
    }
    return "Couldn't save " + utf8(path) + ": " + error->reason;
}

}  // namespace prism::gui
