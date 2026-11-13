#include "prism/gui/detail_reader.hpp"

#include <algorithm>
#include <exception>
#include <new>
#include <optional>
#include <utility>

namespace prism::gui {

void ReadEnd::set() {
    {
        std::lock_guard lock(mutex_);
        set_ = true;
    }
    changed_.notify_all();
}

bool ReadEnd::is_set() const {
    std::lock_guard lock(mutex_);
    return set_;
}

void ReadEnd::wait() const {
    std::unique_lock lock(mutex_);
    ++waiters_;
    changed_.notify_all();
    changed_.wait(lock, [&] { return set_; });
    --waiters_;
}

bool ReadEnd::wait(std::stop_token stop) const {
    std::unique_lock lock(mutex_);
    ++waiters_;
    changed_.notify_all();
    const bool done = changed_.wait(lock, stop, [&] { return set_; });
    --waiters_;
    return done;
}

bool ReadEnd::wait_for_waiters(int count, std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return waiters_ >= count; });
}

DetailReader::DetailReader(ReadFunction read) : read_(std::move(read)) {
    if (!read_) {
        read_ = [](const std::filesystem::path& source, InputKind kind,
                   const EditSettings& settings, int threads, std::stop_token stop) {
            return decode_for_export(source, kind, settings, threads, std::move(stop));
        };
    }
}

DetailReader::~DetailReader() {
    // Every read is asked to stop before any is joined (workers_' jthreads, destroyed first), so
    // none decodes on while an earlier one is being waited for.
    stop();
}

void DetailReader::start(std::uint64_t generation, std::filesystem::path source, InputKind kind,
                         EditSettings settings, int threads) {
    stop();
    // Reads that have ended only need joining (a moment: they end right after setting `end`).
    std::erase_if(workers_, [](const Worker& w) { return w.end->is_set(); });

    std::shared_ptr<const ReadEnd> before = last_end_;
    auto end = std::make_shared<ReadEnd>();
    last_end_ = end;
    std::jthread thread([this, generation, source = std::move(source), kind,
                         settings = std::move(settings), threads, before,
                         end](std::stop_token stop) {
        run(stop, generation, source, kind, settings, threads, before);
        // Never ends before the reads started earlier: `end` then says that all of them have.
        if (before) {
            before->wait();
        }
        end->set();
        {
            // Taken so that a wait() checking whether the reads have ended either sees `end` set
            // or is already waiting for this notification.
            std::lock_guard lock(mutex_);
        }
        changed_.notify_all();
    });
    workers_.push_back(Worker{std::move(end), std::move(thread)});
}

void DetailReader::run(std::stop_token stop, std::uint64_t generation,
                       const std::filesystem::path& source, InputKind kind,
                       const EditSettings& settings, int threads,
                       const std::shared_ptr<const ReadEnd>& before) {
    // One full-size decode at a time: the reads before this one may still be decoding.
    if (before && !before->wait(stop)) {
        return;
    }
    std::optional<Result> result;
    try {
        std::variant<Photo, Error> decoded = read_(source, kind, settings, threads, stop);
        if (stop.stop_requested()) {
            return;  // what was decoded is freed here, before this read counts as ended
        }
        if (Error* e = std::get_if<Error>(&decoded)) {
            result = Result{generation, std::move(*e)};
        } else {
            result = Result{generation, std::make_shared<const Photo>(
                                            std::move(std::get<Photo>(decoded)))};
        }
    } catch (const std::bad_alloc&) {
        result = Result{generation, Error{ErrorKind::out_of_memory,
                                          "Not enough memory to read the full-size photo.", ""}};
    } catch (const std::exception& e) {
        result = Result{generation, Error{ErrorKind::unreadable,
                                          "Couldn't read the full-size photo.", e.what()}};
    }
    {
        std::lock_guard lock(mutex_);
        if (stop.stop_requested()) {
            return;  // stop() came first: post nothing (the result is freed on return)
        }
        results_.push(std::move(*result));
        posted_ = true;
    }
    changed_.notify_all();
}

void DetailReader::stop() {
    std::lock_guard lock(mutex_);
    for (Worker& w : workers_) {
        w.thread.request_stop();
    }
}

std::shared_ptr<const ReadEnd> DetailReader::ended() const {
    return last_end_;
}

std::vector<DetailReader::Result> DetailReader::take_all() {
    // Under mutex_, as a read posts: posted_ never says there is a result when there is none.
    std::lock_guard lock(mutex_);
    posted_ = false;
    return results_.take_all();
}

bool DetailReader::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [&] { return posted_ || !last_end_ || last_end_->is_set(); });
}

}  // namespace prism::gui
