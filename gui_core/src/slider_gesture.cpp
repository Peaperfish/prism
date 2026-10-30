#include "prism/gui/slider_gesture.hpp"

namespace prism::gui {

MaybeError apply_slider_frame(Session& session, const SliderFrame& frame,
                              const EditSettings& edited) {
    // In this order, because one frame can carry all three (a quick click).
    if (frame.activated) {
        session.begin_gesture();
    }
    MaybeError error;
    if (frame.changed) {
        error = session.set_settings(edited);
    }
    if (frame.deactivated) {
        session.end_gesture();
    }
    return error;
}

}  // namespace prism::gui
