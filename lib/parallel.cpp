#include "prism/parallel.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace prism {

int hardware_threads() noexcept {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
}

int threads_for_pixels(std::size_t pixels, int threads) noexcept {
    if (pixels < parallel_min_pixels) {
        return 1;
    }
    return threads <= 0 ? hardware_threads() : threads;
}

RowRange chunk_rows(int height, int chunks, int chunk) noexcept {
    // Chunk i covers [i * height / chunks, (i + 1) * height / chunks). Rounding down at both ends
    // spreads the leftover rows evenly, e.g. 10 rows in 4 chunks gives sizes 2, 3, 2, 3.
    // long long because i * height overflows int for large chunk counts on tall images.
    const auto boundary = [&](int i) {
        return static_cast<int>(static_cast<long long>(i) * height / chunks);
    };
    return {boundary(chunk), boundary(chunk + 1)};
}

void parallel_for(int count, const std::function<void(int index)>& fn) {
    if (count <= 0) {
        return;
    }
    {
        // std::jthread joins (waits for its thread) in its destructor. So when this vector goes
        // out of scope, every worker has finished, even if creating a later thread threw. A plain
        // std::thread would instead call std::terminate if destroyed without join().
        std::vector<std::jthread> workers;
        workers.reserve(static_cast<std::size_t>(count - 1));
        for (int i = 0; i < count - 1; ++i) {
            workers.emplace_back(fn, i);
        }
        fn(count - 1);
    }
    // Joining is also what makes the workers' writes visible here: the C++ memory model says
    // everything a thread did "happens before" join() returns, so no extra locks are needed.
}

void parallel_for_rows(int height, int threads,
                       const std::function<void(int begin, int end)>& fn) {
    if (height <= 0) {
        return;
    }
    // More chunks than rows would give empty chunks, and threads with nothing to do.
    const int chunks = std::clamp(threads, 1, height);
    parallel_for(chunks, [&](int i) {
        const RowRange rows = chunk_rows(height, chunks, i);
        fn(rows.begin, rows.end);
    });
}

}  // namespace prism
