#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"

using namespace std::chrono_literals;
using prism::gui::EditSettings;
using prism::gui::PreviewFrame;
using prism::gui::PreviewRenderer;

// These tests never sleep to wait for the worker thread. They decide the order of events with
// Gate (a flag behind a mutex and condition variable), and wait with generous timeouts that only
// matter if something is broken: then the test fails instead of hanging. They run under
// ThreadSanitizer in CI, which reports any unsynchronised access between the threads.

namespace {

constexpr auto timeout = 30s;  // TSan builds are 5-15x slower; this is never reached normally

// A one-way door: wait() blocks until open() is called (or the timeout passes).
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
    // Nothing ever notifies cv: the wait ends when `stop` is requested, or at the timeout.
    (void)cv.wait_for(lock, stop, timeout, [] { return false; });
    return stop.stop_requested();
}

std::shared_ptr<const prism::Image> make_base(int width, int height) {
    auto img = std::make_shared<prism::Image>();
    img->width = width;
    img->height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img->pixels.push_back(static_cast<float>(x % 97) / 60.0f);
            img->pixels.push_back(static_cast<float>(y % 89) / 70.0f);
            img->pixels.push_back(0.18f);
        }
    }
    return img;
}

EditSettings every_stage() {
    EditSettings s;
    s.exposure_stops = 0.5f;
    s.matrix = prism::gui::MatrixFile{"grade.txt",
                                      {1.05f, 0.03f, -0.01f, 0.01f, 1, 0, -0.02f, -0.02f, 0.94f}};
    s.saturation = 1.25f;
    s.contrast = 1.15f;
    s.curve = prism::gui::CurveKind::filmic;
    return s;
}

EditSettings with_exposure(float stops) {
    EditSettings s;
    s.exposure_stops = stops;
    return s;
}

// The bytes a render of `base` with `s` must give.
std::vector<std::uint8_t> expected_bytes(const prism::Image& base, const EditSettings& s) {
    prism::Image img = base;
    EXPECT_EQ(prism::process(img, prism::gui::to_process_settings(s), 1),
              prism::ProcessResult::completed);
    return prism::encode_srgb8(img, 1);
}

// A frame that says which request it came from (its exposure), without rendering anything.
PreviewFrame tagged_frame(const EditSettings& s) {
    PreviewFrame f;
    f.width = 1;
    f.height = 1;
    f.rgb8 = {static_cast<std::uint8_t>(s.exposure_stops), 0, 0};
    return f;
}

}  // namespace

TEST(PreviewRenderer, DeliversTheRenderedFrame) {
    const auto base = make_base(64, 48);
    PreviewRenderer renderer(1);
    EXPECT_EQ(renderer.poll(), std::nullopt) << "nothing before anything was asked for";

    EXPECT_EQ(renderer.submit(base, every_stage()), 1u);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 1u);
    EXPECT_EQ(frame->width, 64);
    EXPECT_EQ(frame->height, 48);
    EXPECT_TRUE(frame->error.empty());
    EXPECT_EQ(frame->rgb8, expected_bytes(*base, every_stage()));
    EXPECT_EQ(frame->stages.size(), 5u);
    EXPECT_EQ(renderer.poll(), std::nullopt) << "a frame is delivered once";
}

TEST(PreviewRenderer, OnlyTheNewestOfManyFastRequestsIsDelivered) {
    // The first render blocks until the test lets it go; meanwhile 99 more requests arrive.
    Gate first_started;
    Gate release;
    std::mutex m;
    std::vector<float> rendered;  // the exposure of every request the worker started
    PreviewRenderer renderer([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        bool first = false;
        {
            std::lock_guard lock(m);
            rendered.push_back(s.exposure_stops);
            first = rendered.size() == 1;
        }
        if (first) {
            first_started.open();
            release.wait();
        }
        return std::optional<PreviewFrame>(tagged_frame(s));
    });

    const auto base = make_base(4, 4);
    renderer.submit(base, with_exposure(1));
    ASSERT_TRUE(first_started.wait());
    std::uint64_t last = 0;
    for (int i = 2; i <= 100; ++i) {
        last = renderer.submit(base, with_exposure(static_cast<float>(i)));  // never blocks
    }
    EXPECT_EQ(last, 100u);
    release.open();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));

    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 100u);
    EXPECT_EQ(frame->rgb8[0], 100);
    EXPECT_EQ(renderer.poll(), std::nullopt);
    std::lock_guard lock(m);
    EXPECT_EQ(rendered, (std::vector<float>{1, 100})) << "requests 2 to 99 were never started";
}

TEST(PreviewRenderer, ANewRequestCancelsTheRunningRenderBetweenStages) {
    // The real render_preview; its stage callback holds up the first render after its first
    // stage until the renderer stops it. process() must then give up before the next stage.
    Gate first_stage_done;
    std::mutex m;
    std::vector<std::string> first_render_stages;
    std::vector<bool> finished;  // per render: did it produce a frame?
    std::atomic<int> renders{0};
    PreviewRenderer renderer([&](const prism::Image& base, const EditSettings& s,
                                 std::stop_token stop) {
        const bool first = renders++ == 0;
        std::optional<PreviewFrame> frame =
            prism::gui::render_preview(base, s, 1, stop, [&](const prism::StageTime& t) {
                if (first) {
                    {
                        std::lock_guard lock(m);
                        first_render_stages.emplace_back(t.name);
                    }
                    first_stage_done.open();
                    EXPECT_TRUE(wait_for_stop(stop)) << "the second submit must stop this render";
                }
            });
        std::lock_guard lock(m);
        finished.push_back(frame.has_value());
        return frame;
    });

    const auto base = make_base(32, 32);
    renderer.submit(base, every_stage());
    ASSERT_TRUE(first_stage_done.wait());
    renderer.submit(base, with_exposure(2));
    ASSERT_TRUE(renderer.wait_until_idle(timeout));

    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 2u);
    EXPECT_EQ(frame->rgb8, expected_bytes(*base, with_exposure(2)));
    std::lock_guard lock(m);
    EXPECT_EQ(first_render_stages, std::vector<std::string>{"exposure"});
    EXPECT_EQ(finished, (std::vector<bool>{false, true}));
}

TEST(PreviewRenderer, ASupersededRenderNeverDeliversAFrame) {
    // Even a render that ignores its stop token and finishes anyway: its frame is dropped.
    Gate first_started;
    Gate release;
    std::atomic<int> renders{0};
    PreviewRenderer renderer([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        if (renders++ == 0) {
            first_started.open();
            release.wait();
        }
        return std::optional<PreviewFrame>(tagged_frame(s));
    });

    const auto base = make_base(4, 4);
    renderer.submit(base, with_exposure(1));
    ASSERT_TRUE(first_started.wait());
    renderer.submit(base, with_exposure(2));
    release.open();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));

    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 2u);
    EXPECT_EQ(frame->rgb8[0], 2);
    EXPECT_EQ(renderer.poll(), std::nullopt) << "generation 1 must not turn up later either";
    EXPECT_EQ(renders, 2);
}

TEST(PreviewRenderer, DestroyingItWhileRenderingStopsAndJoinsTheWorker) {
    Gate started;
    std::atomic<bool> saw_stop{false};
    {
        PreviewRenderer renderer(
            [&](const prism::Image&, const EditSettings& s, std::stop_token stop) {
                started.open();
                saw_stop = wait_for_stop(stop);
                return std::optional<PreviewFrame>(tagged_frame(s));
            });
        renderer.submit(make_base(4, 4), EditSettings{});
        ASSERT_TRUE(started.wait());
    }  // the destructor runs here, with the render still waiting for its stop token
    EXPECT_TRUE(saw_stop) << "the destructor must stop the render before joining";
}

TEST(PreviewRenderer, DestroyingItWithARequestWaitingIsFine) {
    // Requests that are never rendered, and a worker that is busy, when the renderer goes away.
    // Under AddressSanitizer this also checks nothing touches the renderer after it is gone.
    for (int i = 0; i < 20; ++i) {
        PreviewRenderer renderer(1);
        const auto base = make_base(200, 100);
        for (int j = 0; j < 5; ++j) {
            renderer.submit(base, every_stage());
        }
    }
}

TEST(PreviewRenderer, TheDeliveredBytesDoNotDependOnTheThreadCount) {
    // Over ~1 MP (parallel_min_pixels), so several threads really split each stage.
    const auto base = make_base(1100, 1000);
    std::vector<std::vector<std::uint8_t>> results;
    for (const int threads : {1, 4, 0}) {
        PreviewRenderer renderer(threads);
        renderer.submit(base, every_stage());
        ASSERT_TRUE(renderer.wait_until_idle(timeout));
        std::optional<PreviewFrame> frame = renderer.poll();
        ASSERT_TRUE(frame);
        results.push_back(std::move(frame->rgb8));
    }
    EXPECT_EQ(results[0], results[1]);
    EXPECT_EQ(results[0], results[2]);
}

TEST(PreviewRenderer, ARenderErrorArrivesAsAFrameWithAMessage) {
    PreviewRenderer renderer(1);
    EditSettings bad;
    bad.contrast = 50.0f;
    renderer.submit(make_base(8, 8), bad);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_TRUE(frame->rgb8.empty());
    EXPECT_NE(frame->error.find("Contrast must be from 0.1 to 10"), std::string::npos)
        << frame->error;

    EXPECT_THROW(renderer.submit(nullptr, EditSettings{}), std::invalid_argument);
}
