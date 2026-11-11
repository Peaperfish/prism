#pragma once

#include <array>
#include <functional>
#include <string_view>

#include "imgui.h"
#include "prism/gui/export_size.hpp"
#include "prism/resize.hpp"

namespace prism::gui_app {

// The Export sheet: what Export… shows before the save dialog. A title, the Size control (Original,
// Long side, Percent), one value row, one line saying what will be written, and Cancel and Save As….
// It is drawn from plain data and answers through its return value and its state, and holds nothing
// else, so the tests can draw it without the app or a renderer.
//
// Every part sits at a place worked out from the interface size alone (export_sheet_layout), and
// nothing that is shown has a say in it: the sheet is one height in all three modes, whatever the
// file's size, with the "not smaller" line or without it, under the mouse or not (#38). The value
// row and the result line are always there, at the same place and height; only what is in them
// changes.

// What the sheet edits. The app keeps it while the sheet is up.
struct ExportSheetState {
    gui::ExportSize size;
    // What the number field shows. Rewritten from `size` whenever the field is not being typed in,
    // so after a number is typed and the field is left it shows the number that was kept (0 -> 1).
    std::array<char, 9> text{};
    // The first frame: put the keyboard in the number field (when this mode has one).
    bool focus_field = true;
};

// What the sheet says about the photo, for its value row and result line.
struct ExportSheetContext {
    prism::PixelSize file;   // the file's own size (a percentage is of this)
    prism::PixelSize start;  // what the export decodes at (the RAW half-size decode, or `file`)
    bool file_size_exact = true;  // false: `file` is an estimate, and the sheet says "about"
};

enum class ExportSheetResult { none, cancel, save_as };

struct ExportSheetCallbacks {
    // Right after a control is submitted, it being ImGui's last item, as in scopes_panel.hpp.
    std::function<void(std::string_view id)> explain;
};

// Where the parts were drawn, in ImGui's screen units. `field_*` are empty when the mode has no
// number field (Original).
struct ExportSheetRects {
    ImVec2 title_min, title_max;
    ImVec2 label_min, label_max;
    ImVec2 tabs_min, tabs_max;
    ImVec2 row_min, row_max;
    ImVec2 field_min, field_max;
    ImVec2 result_min, result_max;
    ImVec2 cancel_min, cancel_max;
    ImVec2 save_min, save_max;
    // The bottom of what the sheet took, below its first item's top: the content height.
    float height = 0.0f;
};

// The window padding the sheet's window must have (set it with ImGuiStyleVar_WindowPadding), and the
// window's size, padding included. Both depend on the interface size only.
[[nodiscard]] float export_sheet_padding();
[[nodiscard]] ImVec2 export_sheet_size();

// Draws the sheet at the cursor, inside a window of export_sheet_size() with that padding, and leaves
// the cursor under it. Enter is Save As… and Esc is Cancel, as the buttons are. Typing in the field
// sets the size's number at once (a long side of at least 1, a percent of 1 to 99), so Save As… can
// never see an out-of-range number.
ExportSheetResult draw_export_sheet(ExportSheetState& state, const ExportSheetContext& context,
                                    const ExportSheetCallbacks& callbacks,
                                    ExportSheetRects* rects = nullptr);

// The number a field's text stands for in `mode`: kept when the text is all digits (the field takes
// no other), clamped to the mode's range; nothing for empty text. Exposed for the tests.
[[nodiscard]] int export_field_value(gui::ExportSize::Mode mode, std::string_view text, int current);

}  // namespace prism::gui_app
