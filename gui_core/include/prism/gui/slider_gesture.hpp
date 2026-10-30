#pragma once

#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"

namespace prism::gui {

// What one slider did in one UI frame. In Dear ImGui:
//
//   const bool changed = ImGui::SliderFloat(...);
//   SliderFrame f{ImGui::IsItemActivated(), changed, ImGui::IsItemDeactivated()};
//
// Several can be true in the same frame: a click that moves the value is activated + changed, a
// value typed in and confirmed with Enter is changed + deactivated, and a click released before
// the next frame is all three.
struct SliderFrame {
    bool activated = false;    // the mouse went down on the slider (or text entry began)
    bool changed = false;      // the value moved this frame
    bool deactivated = false;  // released: the drag (or text entry) is over
};

// Feeds one slider frame into the session so that a whole drag is one undo step, made when it is
// released (see History's gestures): activation begins a gesture, each change sets `edited` (the
// session's settings with the slider's new value), and deactivation ends the gesture. A drag that
// ends where it began leaves no step at all. Returns set_settings' error if `edited` was refused;
// the gesture still ends on release.
[[nodiscard]] MaybeError apply_slider_frame(Session& session, const SliderFrame& frame,
                                            const EditSettings& edited);

}  // namespace prism::gui
