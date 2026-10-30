#pragma once

#include <span>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// What kind of file is open. RAW and PNG/JPEG get different presets: a RAW file is linear and
// needs a tone curve to look like a photo, while a JPEG already has one baked in.
enum class InputKind { image, raw };

// A named look. Only the look fields of `settings` matter: exposure, saturation, contrast and
// the tone curve (with its parameter). Applying a preset sets those and turns those stages on;
// it leaves the colour matrix and the RAW options alone, since those describe the camera or a
// grade the user loaded on purpose rather than the look.
struct Preset {
    std::string_view id;           // stable, for saving and tests: "natural", "vivid", ...
    std::string_view name;         // shown in the UI
    std::string_view description;  // tooltip
    EditSettings settings;
};

// The presets offered for this kind of input, in display order. The first is the default.
[[nodiscard]] std::span<const Preset> presets_for(InputKind kind);

// What a newly opened file starts with: "natural" for RAW, "original" for PNG/JPEG.
[[nodiscard]] const Preset& default_preset(InputKind kind);

// Copies the preset's look into `s` (see Preset).
void apply_preset(EditSettings& s, const Preset& preset);

// True if a and b give the same look: the same effective exposure, saturation, contrast and
// curve. A stage that is off counts as its neutral value, and a curve parameter only counts for
// its own curve (gamma's value does not matter while filmic is chosen).
[[nodiscard]] bool same_look(const EditSettings& a, const EditSettings& b);

// The preset whose look `s` has, or nullptr when it matches none: the UI's "Custom" state.
[[nodiscard]] const Preset* matching_preset(InputKind kind, const EditSettings& s);

}  // namespace prism::gui
