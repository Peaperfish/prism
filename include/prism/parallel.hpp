#pragma once

#include <cstddef>
#include <functional>

namespace prism {

// Simple fork-join parallelism with std::thread: split the work into contiguous chunks, run one
// chunk per thread, wait for all of them. Every per-pixel stage takes an `int threads` argument
// with the same meaning:
//   0  = automatic: one thread per hardware thread (std::thread::hardware_concurrency())
//   1  = single-threaded, on the calling thread
//   n  = at most n threads
// Results are bit-identical whatever the thread count, because each pixel is computed by exactly
// the same code; only *which* thread computes it changes.

// Images smaller than this (about 1 MP) always run on one thread: starting and joining threads
// costs tens of microseconds, which is more than the whole job takes on a small image.
inline constexpr std::size_t parallel_min_pixels = 1 << 20;

// std::thread::hardware_concurrency(), or 1 if the system cannot tell (it may return 0).
[[nodiscard]] int hardware_threads() noexcept;

// The number of threads to use for an image of `pixels` pixels, given a `threads` argument as
// described above. Always at least 1.
[[nodiscard]] int threads_for_pixels(std::size_t pixels, int threads) noexcept;

// Rows [begin, end) of one chunk.
struct RowRange {
    int begin = 0;
    int end = 0;
};

// The `chunk`-th of `chunks` contiguous, near-equal pieces of [0, height). Sizes differ by at
// most one row, and together the pieces cover every row exactly once, in order.
[[nodiscard]] RowRange chunk_rows(int height, int chunks, int chunk) noexcept;

// Calls fn(0), fn(1), ..., fn(count - 1), each on its own thread, and returns once all have
// finished. fn(count - 1) runs on the calling thread, which would otherwise sit idle waiting.
// fn must not throw (an exception escaping a std::thread calls std::terminate).
void parallel_for(int count, const std::function<void(int index)>& fn);

// Splits [0, height) into min(threads, height) chunks with chunk_rows and calls
// fn(begin, end) for each chunk on its own thread. Here `threads` must already be resolved
// (>= 1), e.g. by threads_for_pixels. fn must only write data belonging to its own rows.
void parallel_for_rows(int height, int threads, const std::function<void(int begin, int end)>& fn);

}  // namespace prism
