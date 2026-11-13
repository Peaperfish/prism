#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <variant>
#include <vector>

#include "prism/gui/main_thread_queue.hpp"
#include "prism/gui/session.hpp"

namespace prism::gui {

// Set once a background read (DetailReader) has ended and freed what it read, and every read
// started before it has too. An export waits for it before decoding (ExportJob::read_ended), so a
// full-size photo is never decoded twice at once. Thread-safe.
class ReadEnd {
public:
    void set();
    [[nodiscard]] bool is_set() const;
    // Blocks until set.
    void wait() const;
    // Blocks until set (true) or until `stop` is requested (false, if it wasn't set by then).
    bool wait(std::stop_token stop) const;
    // For tests: blocks until `count` threads are waiting in wait(), for at most `timeout`; false
    // if they weren't by then.
    bool wait_for_waiters(int count, std::chrono::milliseconds timeout) const;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable_any changed_;
    bool set_ = false;
    mutable int waiters_ = 0;
};

// Reads a photo's detail base, decode_for_export's full-size photo, on threads of its own, and
// hands each result to the UI thread through a MainThreadQueue (take_all). It only reads: the
// Session decides when, and which result is still wanted (by its generation).
//
// One full-size decode at a time: a read waits for every read started before it to end before
// it decodes, and a read that is stopped throws away what it decoded rather than posting it.
// decode_for_export only checks its stop token before decoding, so a read stopped while LibRaw
// decodes ends once that decode is done (about a second for 60 MP). The destructor stops every
// read and waits for all of them.
class DetailReader {
public:
    // How a file becomes a detail base (SessionOptions::read_detail_base). The default is
    // decode_for_export; tests pass their own to control timing. Runs on the read's thread with
    // its stop token.
    using ReadFunction = DetailReadFunction;

    struct Result {
        std::uint64_t generation = 0;  // what start() was given
        std::variant<std::shared_ptr<const Photo>, Error> base;
    };

    explicit DetailReader(ReadFunction read = {});
    ~DetailReader();

    DetailReader(const DetailReader&) = delete;
    DetailReader& operator=(const DetailReader&) = delete;

    // UI thread. Stops the reads still running and starts another of `source` as decode_for_export
    // reads it with `settings` (only its RAW options matter), tagged `generation`. It decodes once
    // the earlier reads have ended.
    void start(std::uint64_t generation, std::filesystem::path source, InputKind kind,
               EditSettings settings, int threads);
    // UI thread. Asks every read to stop. Results posted before this stay in take_all().
    void stop();
    // What is set once every read started so far has ended; null if none was started.
    [[nodiscard]] std::shared_ptr<const ReadEnd> ended() const;

    // UI thread: the results posted since the last call, oldest first.
    [[nodiscard]] std::vector<Result> take_all();
    // Waits until a result is waiting in take_all(), or every read has ended, for at most
    // `timeout`; false if neither happened by then. For tests and headless use.
    bool wait(std::chrono::milliseconds timeout);

private:
    struct Worker {
        std::shared_ptr<ReadEnd> end;
        std::jthread thread;
    };

    // The body of one read: decode (after `before` has ended), then post unless stopped.
    void run(std::stop_token stop, std::uint64_t generation, const std::filesystem::path& source,
             InputKind kind, const EditSettings& settings, int threads,
             const std::shared_ptr<const ReadEnd>& before);

    ReadFunction read_;
    MainThreadQueue<Result> results_;

    // Guards posted_, and orders a read's last stop check and its post against stop(), so that a
    // read either posts before stop() or sees it and posts nothing.
    std::mutex mutex_;
    std::condition_variable changed_;
    bool posted_ = false;  // a result is waiting in results_

    std::shared_ptr<ReadEnd> last_end_;  // UI thread only

    // Declared last: destroyed (stopped and joined) first, while the members above still exist.
    std::vector<Worker> workers_;
};

}  // namespace prism::gui
