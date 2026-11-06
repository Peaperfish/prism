#pragma once

#include <string>

#include "prism/gui/clipping.hpp"

namespace prism::gui {

// The short clipping note at the right end of the status bar (#31): one line that is always there
// once a photo is open, so clipping is always in sight. Built from the
// same ClipReport as the Light path, so the two always agree. The window draws the dot (in the
// colour of `kind`) and `text`; the full sentence is `full`.
enum class ClipNoteKind {
    none,        // nothing is out of range and then cut off: "No clipping", no dot
    highlights,  // values above white that a stage cut off (orange dot)
    shadows,     // values below black that a stage cut off (blue dot)
    gamut,       // colours too saturated for sRGB that a stage cut off (violet dot)
};

struct ClipNote {
    ClipNoteKind kind = ClipNoteKind::none;
    // "0.2% clipped in the file", "3% highlights clipped at Output encode", "No clipping".
    // More than one kind: the most important first, then " +1 more".
    std::string text = "No clipping";
    // describe_clipping(), then describe_gamut() if there are colours outside sRGB.
    std::string full = "Nothing clips.";

    bool operator==(const ClipNote&) const = default;
};

// The note for `report`. What counts is what a stage cuts off (detail lost), the same events as
// the filled marks in the Light path. In order of importance: highlights already clipped in the
// file (nothing can bring them back), then highlights, shadows and colours outside sRGB that a
// stage clipped, each with its share of the sampled pixels and the stage that clipped it. The
// first of those that exists is shown; the rest are counted as "+N more". An empty report gives
// "No clipping".
[[nodiscard]] ClipNote make_clip_note(const ClipReport& report);

}  // namespace prism::gui
