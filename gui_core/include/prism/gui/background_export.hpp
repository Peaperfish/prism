#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "prism/gui/session.hpp"

namespace prism::gui {

// How an export ended, for the UI to report.
struct ExportResult {
    std::filesystem::path output;
    MaybeError error;  // nothing: the file was written
};

// Runs export_image() on a thread of its own, so the UI keeps drawing (a "Saving..." dialog)
// while a 60 MP photo is decoded at full size, processed and encoded, which takes seconds.
//
//   UI thread:            if (!exporter.busy()) exporter.start(session.export_job(), path);
//   UI thread, per frame: if (auto result = exporter.poll()) report(*result);
//
// The job is a copy of everything the export needs (see ExportJob), so the thread shares nothing
// with the Session. One export at a time. The destructor cancels a running export (it stops
// between stages and writes nothing) and waits for its thread.
class BackgroundExport {
public:
    // How a job becomes a file. The default is export_image; tests pass their own to control
    // timing. Runs on the export thread with the export's stop token.
    using ExportFunction = std::function<MaybeError(const ExportJob& job,
                                                    const std::filesystem::path& output,
                                                    std::stop_token stop)>;

    BackgroundExport();
    explicit BackgroundExport(ExportFunction run);
    ~BackgroundExport();

    BackgroundExport(const BackgroundExport&) = delete;
    BackgroundExport& operator=(const BackgroundExport&) = delete;

    // Starts exporting `job` to `output`. False (and nothing starts) while busy().
    bool start(ExportJob job, std::filesystem::path output);

    // True from start() until poll() has returned its result.
    [[nodiscard]] bool busy() const noexcept { return busy_; }

    // The result once the export has finished, exactly once; nothing while it runs (or if none
    // was started).
    [[nodiscard]] std::optional<ExportResult> poll();

    // Asks the running export to stop; it then finishes with ErrorKind::cancelled.
    void cancel();

    // Waits until the running export has finished, for at most `timeout`; false if it hadn't.
    // For tests and headless use; the UI just polls.
    bool wait(std::chrono::milliseconds timeout);

private:
    ExportFunction run_;
    bool busy_ = false;  // UI thread only

    std::mutex mutex_;  // guards result_
    std::condition_variable done_;
    std::optional<ExportResult> result_;

    // Declared last: destroyed (stopped and joined) first, while the members above still exist.
    std::jthread worker_;
};

}  // namespace prism::gui
