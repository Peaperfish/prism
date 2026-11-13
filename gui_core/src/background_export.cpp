#include "prism/gui/background_export.hpp"

#include <exception>
#include <new>
#include <utility>

namespace prism::gui {

BackgroundExport::BackgroundExport()
    : BackgroundExport([](const ExportJob& job, const std::filesystem::path& output,
                          std::stop_token stop) { return export_image(job, output, stop); }) {}

BackgroundExport::BackgroundExport(ExportFunction run) : run_(std::move(run)) {}

BackgroundExport::~BackgroundExport() = default;  // worker_'s jthread: request_stop + join

bool BackgroundExport::start(ExportJob job, std::filesystem::path output) {
    if (busy_) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();  // the previous export, already finished (poll() returned its result)
    }
    busy_ = true;
    worker_ = std::jthread([this, job = std::move(job),
                            output = std::move(output)](std::stop_token stop) {
        ExportResult result{output, std::nullopt};
        try {
            result.error = run_(job, output, stop);
        } catch (const std::bad_alloc&) {
            result.error = Error{ErrorKind::out_of_memory, "Not enough memory to save this photo.",
                                 ""};
        } catch (const std::exception& e) {
            // export_image reports what it expects as an Error; anything else must still reach
            // the UI rather than end the program.
            result.error = Error{ErrorKind::save_failed, "Couldn't save the photo.", e.what()};
        }
        {
            std::lock_guard lock(mutex_);
            result_ = std::move(result);
        }
        done_.notify_all();
    });
    return true;
}

std::optional<ExportResult> BackgroundExport::poll() {
    std::optional<ExportResult> result;
    {
        std::lock_guard lock(mutex_);
        result.swap(result_);
    }
    if (result) {
        busy_ = false;
    }
    return result;
}

void BackgroundExport::cancel() {
    worker_.request_stop();
}

bool BackgroundExport::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return done_.wait_for(lock, timeout, [&] { return result_.has_value() || !busy_; });
}

SessionExport::SessionExport(BackgroundExport& exporter, Begin begin, End end)
    : exporter_(exporter), begin_(std::move(begin)), end_(std::move(end)) {}

SessionExport::~SessionExport() {
    if (!begun_) {
        return;
    }
    // Closing mid-export: stop it, wait for its thread to write nothing, and end it here, so the
    // session hears of every export it began.
    exporter_.cancel();
    while (!exporter_.wait(std::chrono::seconds(1))) {
    }
    (void)poll();
}

bool SessionExport::start(std::filesystem::path output) {
    // Begun first even when an export is running, so a refused start is ended like any other: the
    // app never starts one then (it checks busy()), and if it did the session's read would only
    // stop and start again.
    ExportJob job = begin_();
    if (!exporter_.start(std::move(job), std::move(output))) {
        end_();  // the one running stays begun, and is ended with its result
        return false;
    }
    begun_ = true;
    return true;
}

std::optional<ExportResult> SessionExport::poll() {
    std::optional<ExportResult> result = exporter_.poll();
    if (result && begun_) {
        begun_ = false;
        end_();
    }
    return result;
}

void SessionExport::cancel() {
    exporter_.cancel();
}

}  // namespace prism::gui
