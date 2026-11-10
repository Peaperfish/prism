#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "prism/gui/settings.hpp"
#include "prism/gui/stage_controls.hpp"

namespace prism::gui {

// Typing a value into Exposure's, Saturation's or Contrast's slider (Ctrl-click, Cmd-click on a Mac).
// The number is in the unit the row's value line shows: signed stops for Exposure ("+0.70 stops"),
// the multiplier for Saturation and Contrast ("×1.20"). All pure functions, so they are tested
// without a window.

// The number in `text`: an optional sign (+, - or the minus sign U+2212 "−" in UTF-8), digits with at most one decimal mark ("." or
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

// The unit words `control`'s row accepts after the number: "stops" and "stop" for Exposure, "×"
// (UTF-8) and "x" for the other two.
[[nodiscard]] std::span<const std::string_view> typed_units(SimpleControl control) noexcept;

// What `text` means for the field behind `control`, in the row's unit (stops, or the multiplier);
// nothing if it is not a number the row accepts. Contrast's row takes a paper grade ("3", "3,5",
// "3½", "grade 4"; clamped to 0 to 5) or, with a "×" or "x" ("×1.2", "1.2x"), the multiplier
// (clamped to contrast_range); either way the result is the multiplier.
[[nodiscard]] std::optional<float> typed_row_value(SimpleControl control, std::string_view text);

// The slider position `text` stands for, converted through the settings maps and clamped to the
// slider's range; nothing if the text is rejected, so the value stays. Not for Contrast, whose
// slider is in half grades: it always gives nothing (typed_row_value reads its text).
[[nodiscard]] std::optional<float> typed_slider_position(SimpleControl control,
                                                         std::string_view text);

// What the field holds when it opens: the stored value as the row's value line shows it, without
// the "×": "+0.70 stops", "1.20". Contrast holds its grade, "3", "3½" or "2.7", or for a value
// past grade 0 or 5 the multiplier with its "×" ("×0.60"). It reads back as the same text with typed_row_value.
[[nodiscard]] std::string typed_field_text(const EditSettings& s, SimpleControl control);

// --- White balance -------------------------------------------------------------------------------

// What `text` means typed into one of the White balance row's sliders, clamped to the slider's range
// and snapped as a drag is (snap_kelvin, snap_tint); nothing if it is not such a number.
//   Kelvin: a number above 0 with an optional "K" or "kelvin": "5200", "5200 K", "5200K". Spaces
//           between the digits are ignored ("5 200"), and so is one "," or "." that groups
//           thousands, with exactly three digits after it ("5,200").
//   Tint:   a number with an optional sign, "-" or the minus sign U+2212: "10", "+10", "−10".
[[nodiscard]] std::optional<double> typed_white_balance_value(WhiteBalanceControl control,
                                                              std::string_view text);

// What the field holds when it opens, for a slider at `shown` (slider_white_balance): "5330 K" for
// Kelvin, and for Tint "+27", "−10" or "0". It reads back with typed_white_balance_value.
[[nodiscard]] std::string typed_field_text(const WhiteBalance& shown, WhiteBalanceControl control);

}  // namespace prism::gui
