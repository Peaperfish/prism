#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid
#else
#include <unistd.h>  // getpid
#endif

#include "prism/gui/background_export.hpp"
#include "prism/gui/session.hpp"
#include "prism/image.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using prism::gui::BackgroundExport;
using prism::gui::Error;
using prism::gui::ErrorKind;
using prism::gui::ExportJob;
using prism::gui::ExportResult;
using prism::gui::MaybeError;
using prism::gui::Session;

// As in preview_renderer_test.cpp: no sleeps; Gates decide the order of events, and the timeouts
// only matter if something is broken. Runs under ThreadSanitizer in CI.

namespace {

constexpr auto timeout = 30s;

class Gate {
public:
    void open() {
        {
            std::lock_guard lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }
    bool wait() {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

// Blocks until `stop` is requested (or the timeout passes); true if it was.
bool wait_for_stop(std::stop_token stop) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock lock(m);
    (void)cv.wait_for(lock, stop, timeout, [] { return false; });
    return stop.stop_requested();
}

fs::path temp_file(const std::string& name) {
    const testing::TestInfo* test = testing::UnitTest::GetInstance()->current_test_info();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    return fs::path(testing::TempDir()) /
           ("prism_export_" + std::string(test->name()) + "_" + std::to_string(pid) + "_" + name);
}

fs::path write_png(const std::string& name, int width, int height) {
    prism::Image img;
    img.width = width;
    img.height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img.pixels.push_back(static_cast<float>(x) / width);
            img.pixels.push_back(static_cast<float>(y) / height);
            img.pixels.push_back(0.18f);
        }
    }
    const fs::path path = temp_file(name);
    prism::save_image(path, img);
    return path;
}

std::vector<char> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(BackgroundExport, WritesTheSameFileAsExportImage) {
    const fs::path source = write_png("in.png", 40, 30);
    Session session;
    ASSERT_EQ(session.open_file(source), std::nullopt);
    prism::gui::EditSettings s = session.settings();
    s.exposure_stops = 0.5f;
    s.saturation = 1.3f;
    ASSERT_EQ(session.set_settings(s), std::nullopt);

    const fs::path background = temp_file("background.png");
    const fs::path direct = temp_file("direct.png");
    BackgroundExport exporter;
    ASSERT_TRUE(exporter.start(session.export_job(), background));
    EXPECT_TRUE(exporter.busy());
    ASSERT_TRUE(exporter.wait(timeout));
    const std::optional<ExportResult> result = exporter.poll();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->output, background);
    EXPECT_EQ(result->error, std::nullopt);
    EXPECT_FALSE(exporter.busy());

    ASSERT_EQ(prism::gui::export_image(session.export_job(), direct), std::nullopt);
    EXPECT_FALSE(read_file(background).empty());
    EXPECT_EQ(read_file(background), read_file(direct));
    fs::remove(source);
    fs::remove(background);
    fs::remove(direct);
}

TEST(BackgroundExport, ReportsTheExportsError) {
    const fs::path source = write_png("in.png", 8, 8);
    Session session;
    ASSERT_EQ(session.open_file(source), std::nullopt);
    BackgroundExport exporter;
    ASSERT_TRUE(exporter.start(session.export_job(), temp_file("no_such_folder") / "out.png"));
    ASSERT_TRUE(exporter.wait(timeout));
    const std::optional<ExportResult> result = exporter.poll();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->error);
    EXPECT_EQ(result->error->kind, ErrorKind::save_failed);
    fs::remove(source);
}

TEST(BackgroundExport, OneAtATimeAndEachResultOnce) {
    Gate started;
    Gate release;
    int runs = 0;  // only touched on the export thread, read after it finished
    BackgroundExport exporter([&](const ExportJob&, const fs::path&, std::stop_token) {
        ++runs;
        started.open();
        release.wait();
        return MaybeError();
    });
    EXPECT_EQ(exporter.poll(), std::nullopt) << "nothing started yet";

    ASSERT_TRUE(exporter.start({}, "a.png"));
    ASSERT_TRUE(started.wait());
    EXPECT_FALSE(exporter.start({}, "b.png")) << "busy";
    EXPECT_EQ(exporter.poll(), std::nullopt) << "still running";
    EXPECT_TRUE(exporter.busy());
    release.open();

    ASSERT_TRUE(exporter.wait(timeout));
    const std::optional<ExportResult> result = exporter.poll();
    ASSERT_TRUE(result);
    EXPECT_EQ(result->output, fs::path("a.png"));
    EXPECT_EQ(exporter.poll(), std::nullopt) << "delivered once";
    EXPECT_FALSE(exporter.busy());

    // The next export may start now, on a new thread.
    ASSERT_TRUE(exporter.start({}, "c.png"));
    ASSERT_TRUE(exporter.wait(timeout));
    ASSERT_TRUE(exporter.poll());
    EXPECT_EQ(runs, 2);
}

TEST(BackgroundExport, CancelReachesTheExportsStopToken) {
    Gate started;
    BackgroundExport exporter([&](const ExportJob&, const fs::path&, std::stop_token stop) {
        started.open();
        return wait_for_stop(stop) ? MaybeError(Error{ErrorKind::cancelled, "Export cancelled.", ""})
                                   : MaybeError();
    });
    ASSERT_TRUE(exporter.start({}, "out.png"));
    ASSERT_TRUE(started.wait());
    exporter.cancel();
    ASSERT_TRUE(exporter.wait(timeout));
    const std::optional<ExportResult> result = exporter.poll();
    ASSERT_TRUE(result && result->error);
    EXPECT_EQ(result->error->kind, ErrorKind::cancelled);
}

TEST(BackgroundExport, DestroyingItStopsTheRunningExport) {
    Gate started;
    bool stopped = false;  // written on the export thread before the destructor's join returns
    {
        BackgroundExport exporter([&](const ExportJob&, const fs::path&, std::stop_token stop) {
            started.open();
            stopped = wait_for_stop(stop);
            return MaybeError();
        });
        ASSERT_TRUE(exporter.start({}, "out.png"));
        ASSERT_TRUE(started.wait());
    }  // must neither hang nor crash
    EXPECT_TRUE(stopped);
}

TEST(BackgroundExport, AnExceptionBecomesAnError) {
    BackgroundExport exporter([](const ExportJob&, const fs::path&, std::stop_token) -> MaybeError {
        throw std::runtime_error("disk on fire");
    });
    ASSERT_TRUE(exporter.start({}, "out.png"));
    ASSERT_TRUE(exporter.wait(timeout));
    const std::optional<ExportResult> result = exporter.poll();
    ASSERT_TRUE(result && result->error);
    EXPECT_EQ(result->error->kind, ErrorKind::save_failed);
    EXPECT_EQ(result->error->details, "disk on fire");
}
