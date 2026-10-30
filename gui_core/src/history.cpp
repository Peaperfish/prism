#include "prism/gui/history.hpp"

#include <utility>

namespace prism::gui {

History::History(EditSettings initial, std::size_t capacity)
    : current_(std::move(initial)), capacity_(capacity) {}

void History::set(const EditSettings& s) {
    if (s == current_) {
        return;
    }
    if (!in_gesture_) {
        push_undo(current_);
        redo_.clear();
    }
    current_ = s;
}

void History::begin_gesture() {
    if (in_gesture_) {
        return;
    }
    in_gesture_ = true;
    gesture_start_ = current_;
}

void History::end_gesture() {
    if (in_gesture_) {
        finish_gesture();
    }
}

void History::finish_gesture() {
    in_gesture_ = false;
    if (gesture_start_ != current_) {
        push_undo(std::move(gesture_start_));
        redo_.clear();
    }
}

// The gesture in progress counts as a step that can be undone, since undo() would finish it.
bool History::can_undo() const noexcept {
    return !undo_.empty() || (in_gesture_ && gesture_start_ != current_);
}

bool History::undo() {
    end_gesture();
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    return true;
}

bool History::redo() {
    end_gesture();
    if (redo_.empty()) {
        return false;
    }
    push_undo(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    return true;
}

void History::reset(EditSettings s) {
    current_ = std::move(s);
    undo_.clear();
    redo_.clear();
    in_gesture_ = false;
}

void History::push_undo(EditSettings s) {
    if (capacity_ == 0) {
        return;
    }
    if (undo_.size() == capacity_) {
        undo_.pop_front();
    }
    undo_.push_back(std::move(s));
}

}  // namespace prism::gui
