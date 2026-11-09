#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// Typing a value into Exposure's, Saturation's or Contrast's slider (Ctrl-click, Cmd-click on a Mac).
// The number is in the unit the row's value line shows: signed stops for Exposure ("+0.70 stops"),
// the multiplier for Saturation and Contrast ("×1.20"). All pure functions, so they are tested
// without a window.

// The number in `text`: an optional sign (+ or -), digits with at most one decimal mark ("." or
// ","), and then, after optional spaces, nothing or one of `units` (ASCII letters in any case).
// Spaces around the whole text are ignored. Anything else, including an empty number ("", "+",
// "."), a second mark ("1.000,5"), an exponent ("1e3") or an unknown word, gives nothing.
//
// Reads the digits by hand, so the result never depends on the C locale (std::from_chars for
// float needs macOS 26 and Prism supports 13.3). The value is the nearest double to the decimal
// text, converted to float; digits past the 18th significant one are dropped, and a value past
// 1e30 reads as 1e30 (any slider clamps it anyway).
[[nodiscard]] std::optional<float> parse_typed_number(std::string_view text,
                                                      std::span<const std::string_view> units);

// The unit words `control`'s row accepts after the number: "stops" for Exposure, "×" (UTF-8) and
// "x" for the other two.
[[nodiscard]] std::span<const std::string_view> typed_units(SimpleControl control) noexcept;

// What `text` means for the field behind `control`, in the row's unit (stops, or the multiplier);
// nothing if it is not a number the row accepts.
[[nodiscard]] std::optional<float> typed_row_value(SimpleControl control, std::string_view text);

// The slider position `text` stands for, converted through the settings maps and clamped to the
// slider's range (a typed ×5 Contrast is the slider's top end); nothing if the text is rejected,
// so the value stays.
[[nodiscard]] std::optional<float> typed_slider_position(SimpleControl control,
                                                         std::string_view text);

// What the field holds when it opens: the stored value as the row's value line shows it, without
// the "×": "+0.70 stops", "1.20". It reads back as the same text with typed_row_value.
[[nodiscard]] std::string typed_field_text(const EditSettings& s, SimpleControl control);

}  // namespace prism::gui
