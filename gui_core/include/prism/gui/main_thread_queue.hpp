#pragma once

#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace prism::gui {

// Hands values from any thread to the UI thread. A file dialog's callback, for example, may run
// on a thread of the operating system's choosing, but the Session and the renderer belong to the
// UI thread: the callback push()es what it got, and the UI thread take_all()s it once per frame.
//
//   any thread:  queue.push(path);
//   UI thread:   for (auto& path : queue.take_all()) open(path);
//
// Values come out in the order they went in (per pushing thread; between threads, in the order
// they got the lock). Nothing ever waits for the UI thread: push() only holds the lock long
// enough to append, and take_all() long enough to swap the contents out.
template <typename T>
class MainThreadQueue {
public:
    // Thread-safe.
    void push(T value) {
        std::lock_guard lock(mutex_);
        items_.push_back(std::move(value));
    }

    // Everything pushed since the last call, oldest first; empty if nothing was. Thread-safe,
    // but meant for the one thread that consumes.
    [[nodiscard]] std::vector<T> take_all() {
        std::deque<T> taken;
        {
            std::lock_guard lock(mutex_);
            taken.swap(items_);
        }
        return std::vector<T>(std::make_move_iterator(taken.begin()),
                              std::make_move_iterator(taken.end()));
    }

private:
    std::mutex mutex_;
    std::deque<T> items_;
};

}  // namespace prism::gui
