#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "prism/gui/settings.hpp"

namespace prism::gui {

// Undo and redo for EditSettings, by keeping whole snapshots: a snapshot is about 100 bytes plus
// two file paths, so 100 of them cost nothing worth optimising, and a snapshot can't get out of
// step with the settings the way a list of "changes" could.
//
// A slider drag sends a new value every frame, but should be one undo step. The UI says when a
// drag starts and ends (in ImGui: IsItemActivated / IsItemDeactivated), and every set() in
// between only updates the current settings:
//
//   history.begin_gesture();        // mouse down on a slider
//   history.set(s);  ... set(s);    // every frame of the drag
//   history.end_gesture();          // mouse up: one step, back to before begin_gesture()
//
// No timer is involved, so the result does not depend on how fast anyone drags. Not thread-safe:
// it belongs to the UI thread.
class History {
public:
    static constexpr std::size_t default_capacity = 100;

    // `capacity` = how many undo steps are kept; the oldest is dropped beyond that.
    explicit History(EditSettings initial = {}, std::size_t capacity = default_capacity);

    [[nodiscard]] const EditSettings& current() const noexcept { return current_; }

    // Makes `s` the current settings. Outside a gesture that is one undo step and clears the redo
    // steps; inside a gesture it is not a step of its own. Setting the current value again does
    // nothing at all.
    void set(const EditSettings& s);

    // Brackets a run of set() calls that undo as one step. A gesture that ends where it began
    // adds no step. begin_gesture() during a gesture, and end_gesture() outside one, do nothing.
    void begin_gesture();
    void end_gesture();
    [[nodiscard]] bool in_gesture() const noexcept { return in_gesture_; }

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    // Finished steps only: a gesture in progress becomes one when it ends.
    [[nodiscard]] std::size_t undo_steps() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redo_steps() const noexcept { return redo_.size(); }

    // Step back or forward; false if there is nothing to undo or redo. During a gesture, undo
    // ends the gesture first and then undoes it as a whole (redo has nothing to redo then).
    bool undo();
    bool redo();

    // Starts over from `s` with no undo or redo steps, e.g. when another file is opened.
    void reset(EditSettings s);

private:
    // Finishes the gesture: if the settings changed during it, one step from where it began.
    void finish_gesture();
    void push_undo(EditSettings s);

    EditSettings current_;
    std::deque<EditSettings> undo_;  // oldest first; deque, because the oldest is dropped
    std::vector<EditSettings> redo_;
    std::size_t capacity_;
    bool in_gesture_ = false;
    EditSettings gesture_start_;
};

}  // namespace prism::gui
