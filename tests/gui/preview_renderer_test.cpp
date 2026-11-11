#include <gtest/gtest.h>

#include <algorithm>
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
#include <thread>
#include <vector>

#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/process.hpp"
#include "prism/white_balance.hpp"

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

TEST(PreviewRenderer, ANewBaseImageCancelsTheRunningRenderBetweenStages) {
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
                    EXPECT_TRUE(wait_for_stop(stop))
                        << "a request for another image must stop this render";
                }
            });
        std::lock_guard lock(m);
        finished.push_back(frame.has_value());
        return frame;
    });

    const auto base = make_base(32, 32);
    const auto other = make_base(16, 16);  // another photo
    renderer.submit(base, every_stage());
    ASSERT_TRUE(first_stage_done.wait());
    renderer.submit(other, with_exposure(2));
    ASSERT_TRUE(renderer.wait_until_idle(timeout));

    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, 2u);
    EXPECT_EQ(frame->rgb8, expected_bytes(*other, with_exposure(2)));
    EXPECT_EQ(renderer.poll(), std::nullopt) << "the stopped render gives no frame";
    std::lock_guard lock(m);
    EXPECT_EQ(first_render_stages, std::vector<std::string>{"exposure"});
    EXPECT_EQ(finished, (std::vector<bool>{false, true}));
}

namespace {

// Lets the test step a fake render function: render n announces that it started, then waits
// until the test releases it. No sleeps, so no timing assumptions.
class Stepper {
public:
    void started() {
        {
            std::lock_guard lock(mutex_);
            ++started_;
        }
        cv_.notify_all();
    }
    bool wait_started(int n) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return started_ >= n; });
    }
    void release(int n) {
        {
            std::lock_guard lock(mutex_);
            released_ = n;
        }
        cv_.notify_all();
    }
    bool wait_released(int n) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return released_ >= n; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int started_ = 0;
    int released_ = 0;
};

// Polls until a frame arrives, or the timeout passes.
std::optional<PreviewFrame> next_frame(PreviewRenderer& renderer) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::optional<PreviewFrame> f = renderer.poll()) {
            return f;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

}  // namespace

TEST(PreviewRenderer, ASupersededRenderOfTheSameImageStillDeliversItsFrame) {
    Stepper step;
    PreviewRenderer renderer([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        step.started();
        const int n = static_cast<int>(s.exposure_stops);
        EXPECT_TRUE(step.wait_released(n));
        return std::optional<PreviewFrame>(tagged_frame(s));
    });
    const auto base = make_base(4, 4);
    renderer.submit(base, with_exposure(1));
    ASSERT_TRUE(step.wait_started(1));
    renderer.submit(base, with_exposure(2));  // arrives while render 1 runs
    step.release(1);

    // Render 2 is held up, so the next frame can only be render 1's: older than the latest request.
    const std::optional<PreviewFrame> first = next_frame(renderer);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->generation, 1u);
    EXPECT_EQ(first->rgb8[0], 1);
    step.release(2);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> second = renderer.poll();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->generation, 2u);
    EXPECT_EQ(second->rgb8[0], 2);
}

TEST(PreviewRenderer, SubmitsFasterThanARenderStillPublishFramesWithRisingGenerations) {
    // A drag: a new request arrives during every render, so every render is superseded before it
    // ends. Each must still be published, then the latest request rendered after the last.
    constexpr int renders = 25;
    Stepper step;
    PreviewRenderer renderer([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        step.started();
        EXPECT_TRUE(step.wait_released(static_cast<int>(s.exposure_stops)));
        return std::optional<PreviewFrame>(tagged_frame(s));
    });
    const auto base = make_base(4, 4);
    renderer.submit(base, with_exposure(1));
    std::uint64_t previous = 0;
    for (int i = 1; i <= renders; ++i) {
        ASSERT_TRUE(step.wait_started(i));
        renderer.submit(base, with_exposure(static_cast<float>(i + 1)));
        step.release(i);
        const std::optional<PreviewFrame> frame = next_frame(renderer);
        ASSERT_TRUE(frame) << "render " << i << " was never published";
        EXPECT_EQ(frame->generation, static_cast<std::uint64_t>(i));
        EXPECT_GT(frame->generation, previous);
        previous = frame->generation;
    }
    // Submits stop: the render of the latest settings runs and is the last frame.
    step.release(renders + 1);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> last = renderer.poll();
    ASSERT_TRUE(last);
    EXPECT_EQ(last->generation, static_cast<std::uint64_t>(renders + 1));
    EXPECT_EQ(last->rgb8[0], renders + 1);
}

TEST(PreviewRenderer, AfterAFastBurstOfSubmitsTheLastFrameMatchesTheLatestSettings) {
    // The real render on a small image, with 40 requests in a row.
    PreviewRenderer renderer(1);
    const auto base = make_base(64, 48);
    std::uint64_t last = 0;
    std::uint64_t previous = 0;
    for (int i = 1; i <= 40; ++i) {
        last = renderer.submit(base, with_exposure(static_cast<float>(i) / 10));
        if (const std::optional<PreviewFrame> f = renderer.poll()) {
            EXPECT_GT(f->generation, previous);
            previous = f->generation;
        }
    }
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->generation, last);
    EXPECT_GT(frame->generation, previous);
    EXPECT_EQ(frame->rgb8, expected_bytes(*base, with_exposure(4.0f)));
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

TEST(PreviewRenderer, DestroyingItWithARenderInFlightAndARequestPendingIsClean) {
    Gate started;
    std::atomic<int> renders{0};
    std::atomic<bool> saw_stop{false};
    {
        PreviewRenderer renderer(
            [&](const prism::Image&, const EditSettings& s, std::stop_token stop) {
                ++renders;
                started.open();
                saw_stop = wait_for_stop(stop);
                return std::optional<PreviewFrame>(tagged_frame(s));
            });
        const auto base = make_base(4, 4);
        renderer.submit(base, with_exposure(1));
        ASSERT_TRUE(started.wait());
        renderer.submit(base, with_exposure(2));  // same image: does not stop the render
    }  // the destructor stops the render and does not start the pending request
    EXPECT_TRUE(saw_stop);
    EXPECT_EQ(renders, 1);
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

TEST(PreviewRenderer, StageTapStopsAfterChosenStageAndEncodesForDisplay) {
    using prism::gui::StageId;

    const auto base = make_base(32, 24);
    const EditSettings s = every_stage();

    // Decode tap: no pipeline stages run; output is just the display encode of `base`.
    const std::optional<PreviewFrame> at_decode =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::decode);
    ASSERT_TRUE(at_decode);
    EXPECT_EQ(at_decode->stage, StageId::decode);
    EXPECT_TRUE(at_decode->stages.empty());
    EXPECT_EQ(at_decode->rgb8, prism::encode_srgb8(*base, 1));

    // Exposure tap: only exposure runs, then display encode.
    prism::Image exp_img = *base;
    prism::exposure(exp_img, s.exposure_stops, 1);
    const std::optional<PreviewFrame> at_exposure =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::exposure);
    ASSERT_TRUE(at_exposure);
    EXPECT_EQ(at_exposure->stage, StageId::exposure);
    ASSERT_EQ(at_exposure->stages.size(), 1u);
    EXPECT_EQ(at_exposure->stages[0].name, "exposure");
    EXPECT_EQ(at_exposure->rgb8, prism::encode_srgb8(exp_img, 1));

    // Matrix tap: exposure + matrix run, later stages do not.
    prism::Image mat_img = exp_img;
    prism::apply_matrix(mat_img, s.matrix->values, 1);
    const std::optional<PreviewFrame> at_matrix =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::matrix);
    ASSERT_TRUE(at_matrix);
    ASSERT_EQ(at_matrix->stages.size(), 2u);
    EXPECT_EQ(at_matrix->stages[1].name, "matrix");
    EXPECT_EQ(at_matrix->rgb8, prism::encode_srgb8(mat_img, 1));

    // Saturation tap: exposure + matrix + saturation.
    prism::Image sat_img = mat_img;
    prism::saturation(sat_img, s.saturation, 1);
    const std::optional<PreviewFrame> at_saturation =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::saturation);
    ASSERT_TRUE(at_saturation);
    ASSERT_EQ(at_saturation->stages.size(), 3u);
    EXPECT_EQ(at_saturation->stages[2].name, "saturation");
    EXPECT_EQ(at_saturation->rgb8, prism::encode_srgb8(sat_img, 1));

    // Contrast tap: exposure + matrix + saturation + contrast.
    prism::Image con_img = sat_img;
    prism::contrast(con_img, s.contrast, 1);
    const std::optional<PreviewFrame> at_contrast =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::contrast);
    ASSERT_TRUE(at_contrast);
    ASSERT_EQ(at_contrast->stages.size(), 4u);
    EXPECT_EQ(at_contrast->stages[3].name, "contrast");
    EXPECT_EQ(at_contrast->rgb8, prism::encode_srgb8(con_img, 1));
}

TEST(PreviewRenderer, TappingLastStageEqualsFinalPreviewExactly) {
    using prism::gui::StageId;

    const auto base = make_base(64, 48);
    const EditSettings s = every_stage();

    const std::optional<PreviewFrame> untapped = prism::gui::render_preview(*base, s, 1, {});
    const std::optional<PreviewFrame> at_curve =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::curve);
    const std::optional<PreviewFrame> at_encode =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::encode);
    ASSERT_TRUE(untapped && at_curve && at_encode);
    EXPECT_EQ(at_encode->stage, prism::gui::final_stage);
    EXPECT_EQ(at_encode->rgb8, untapped->rgb8);
    EXPECT_EQ(at_curve->rgb8, untapped->rgb8);
    EXPECT_EQ(at_encode->stages.size(), untapped->stages.size());

    // Also through the worker thread.
    PreviewRenderer renderer(1);
    renderer.submit(base, s, StageId::encode);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<PreviewFrame> worker_frame = renderer.poll();
    ASSERT_TRUE(worker_frame);
    EXPECT_EQ(worker_frame->stage, StageId::encode);
    EXPECT_EQ(worker_frame->rgb8, untapped->rgb8);
}

TEST(PreviewRenderer, TappingAnOffStageShowsItPassingImageThroughUnchanged) {
    using prism::gui::StageId;

    const auto base = make_base(16, 16);
    EditSettings s = every_stage();
    s.saturation_on = false;

    const std::optional<PreviewFrame> at_matrix =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::matrix);
    const std::optional<PreviewFrame> at_saturation =
        prism::gui::render_preview(*base, s, 1, {}, {}, StageId::saturation);
    ASSERT_TRUE(at_matrix && at_saturation);
    EXPECT_EQ(at_saturation->rgb8, at_matrix->rgb8);
    EXPECT_EQ(at_saturation->stages.size(), 2u);
}

// --- Compare stage ---------------------------------------------------------------------------

namespace {

constexpr prism::gui::StageId all_stages[] = {
    prism::gui::StageId::decode,   prism::gui::StageId::exposure,   prism::gui::StageId::white_balance,
    prism::gui::StageId::matrix,   prism::gui::StageId::saturation, prism::gui::StageId::contrast,
    prism::gui::StageId::curve,    prism::gui::StageId::encode};

std::vector<std::uint8_t> stage_bytes(const prism::Image& base, const EditSettings& s,
                                      prism::gui::StageId stage) {
    const std::optional<prism::Image> img = prism::gui::render_stage_image(base, s, stage, 1);
    EXPECT_TRUE(img);
    return img ? prism::encode_srgb8(*img, 1) : std::vector<std::uint8_t>{};
}

prism::gui::PreviewOptions everything_on() {
    prism::gui::PreviewOptions o;
    o.scopes = true;
    o.clip_report = true;
    o.clip_mask = true;
    return o;
}

}  // namespace

TEST(PreviewCompare, TheComparePictureIsExactForEveryPairOfStages) {
    const auto base = make_base(40, 30);
    const EditSettings s = every_stage();
    for (const auto viewed : all_stages) {
        for (const auto compare : all_stages) {
            prism::gui::PreviewOptions options;
            options.compare_stage = compare;
            const auto frame = prism::gui::render_preview(*base, s, 1, {}, {}, viewed, options);
            ASSERT_TRUE(frame);
            EXPECT_EQ(frame->rgb8, stage_bytes(*base, s, viewed));
            if (compare == viewed) {
                EXPECT_FALSE(frame->compare);  // the viewed stage as compare is no comparison
                continue;
            }
            ASSERT_TRUE(frame->compare) << int(viewed) << " vs " << int(compare);
            EXPECT_EQ(frame->compare->stage, compare);
            EXPECT_EQ(frame->compare->width, 40);
            EXPECT_EQ(frame->compare->height, 30);
            EXPECT_EQ(frame->compare->rgb8, stage_bytes(*base, s, compare))
                << int(viewed) << " vs " << int(compare);
        }
    }
}

TEST(PreviewCompare, WithAWhiteBalanceTheComparePictureIsExactForEveryPairOfStages) {
    const auto base = make_base(40, 30);
    EditSettings s = every_stage();
    s.white_balance = prism::WhiteBalance{4300, 12};
    const prism::Chromaticity as_shot = prism::white_of(prism::WhiteBalance{5600, -8});
    const auto bytes = [&](prism::gui::StageId stage) {
        const std::optional<prism::Image> img =
            prism::gui::render_stage_image(*base, s, stage, 1, {}, {}, as_shot);
        EXPECT_TRUE(img);
        return img ? prism::encode_srgb8(*img, 1) : std::vector<std::uint8_t>{};
    };
    // The stage changes the picture, and against the photo's light, not the default one.
    EXPECT_NE(bytes(prism::gui::StageId::white_balance), bytes(prism::gui::StageId::exposure));
    EXPECT_NE(bytes(prism::gui::StageId::white_balance),
              stage_bytes(*base, s, prism::gui::StageId::white_balance));
    for (const auto viewed : all_stages) {
        for (const auto compare : all_stages) {
            prism::gui::PreviewOptions options;
            options.compare_stage = compare;
            options.as_shot_white = as_shot;
            const auto frame = prism::gui::render_preview(*base, s, 1, {}, {}, viewed, options);
            ASSERT_TRUE(frame);
            EXPECT_EQ(frame->rgb8, bytes(viewed)) << int(viewed);
            if (compare == viewed) {
                EXPECT_FALSE(frame->compare);
                continue;
            }
            ASSERT_TRUE(frame->compare) << int(viewed) << " vs " << int(compare);
            // Going on from the viewed stage to a later compare stage runs each stage once.
            EXPECT_EQ(frame->compare->rgb8, bytes(compare))
                << int(viewed) << " vs " << int(compare);
        }
    }
}

TEST(PreviewCompare, ASwitchedOffStageIsComparedAsThePictureBeforeIt) {
    using prism::gui::StageId;
    const auto base = make_base(24, 18);
    EditSettings s = every_stage();
    s.saturation_on = false;
    s.contrast_on = false;
    for (const StageId viewed : {StageId::decode, StageId::exposure, StageId::encode}) {
        for (const StageId compare : {StageId::saturation, StageId::contrast}) {
            prism::gui::PreviewOptions options;
            options.compare_stage = compare;
            const auto frame = prism::gui::render_preview(*base, s, 1, {}, {}, viewed, options);
            ASSERT_TRUE(frame && frame->compare);
            EXPECT_EQ(frame->compare->rgb8, stage_bytes(*base, s, StageId::matrix));
            EXPECT_EQ(frame->compare->rgb8, stage_bytes(*base, s, compare));
        }
    }
}

TEST(PreviewCompare, TheViewedFrameIsEqualWithCompareOffOrOn) {
    const auto base = make_base(48, 36);
    const EditSettings s = every_stage();
    for (const auto viewed : all_stages) {
        const auto off = prism::gui::render_preview(*base, s, 1, {}, {}, viewed, everything_on());
        ASSERT_TRUE(off);
        EXPECT_FALSE(off->compare);
        for (const auto compare : all_stages) {
            prism::gui::PreviewOptions options = everything_on();
            options.compare_stage = compare;
            const auto on = prism::gui::render_preview(*base, s, 1, {}, {}, viewed, options);
            ASSERT_TRUE(on);
            EXPECT_EQ(on->stage, off->stage);
            EXPECT_EQ(on->width, off->width);
            EXPECT_EQ(on->height, off->height);
            EXPECT_EQ(on->rgb8, off->rgb8);
            EXPECT_EQ(on->histogram, off->histogram);
            EXPECT_EQ(on->scopes, off->scopes);
            EXPECT_EQ(on->clips, off->clips) << int(viewed) << " vs " << int(compare);
            EXPECT_EQ(on->clip_mask, off->clip_mask) << int(viewed) << " vs " << int(compare);
            ASSERT_EQ(on->stages.size(), off->stages.size());
            EXPECT_EQ(on->error, off->error);
        }
    }
}

TEST(PreviewCompare, OffCostsNothingAndLeavesNoCompare) {
    const auto base = make_base(16, 16);
    const EditSettings s = every_stage();
    const auto plain = prism::gui::render_preview(*base, s, 1, {});
    prism::gui::PreviewOptions same;  // compare_stage = the viewed stage: also off
    same.compare_stage = prism::gui::final_stage;
    const auto as_off =
        prism::gui::render_preview(*base, s, 1, {}, {}, prism::gui::final_stage, same);
    ASSERT_TRUE(plain && as_off);
    EXPECT_FALSE(plain->compare);
    EXPECT_FALSE(as_off->compare);
    EXPECT_EQ(as_off->rgb8, plain->rgb8);
    EXPECT_EQ(as_off->stages.size(), plain->stages.size());
}

TEST(PreviewCompare, BothHalvesComeFromTheSameSettingsThroughTheWorker) {
    using prism::gui::StageId;
    const auto base = make_base(32, 32);
    PreviewRenderer renderer(1);
    prism::gui::PreviewOptions options;
    options.compare_stage = StageId::decode;
    const EditSettings a = with_exposure(1.0f);
    const EditSettings b = with_exposure(-1.0f);
    renderer.submit(base, a, StageId::exposure, options);
    renderer.submit(base, b, StageId::exposure, options);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const auto frame = renderer.poll();
    ASSERT_TRUE(frame && frame->compare);
    EXPECT_EQ(frame->rgb8, stage_bytes(*base, b, StageId::exposure));
    EXPECT_EQ(frame->compare->rgb8, stage_bytes(*base, b, StageId::decode));
    EXPECT_NE(frame->rgb8, frame->compare->rgb8);
}

TEST(PreviewCompare, ACancelledRenderGivesNoFrame) {
    using prism::gui::StageId;
    const auto base = make_base(16, 16);
    std::stop_source source;
    source.request_stop();
    prism::gui::PreviewOptions options;
    options.compare_stage = StageId::encode;
    EXPECT_FALSE(prism::gui::render_preview(*base, every_stage(), 1, source.get_token(), {},
                                            StageId::exposure, options));
}

// --- Test strip ---------------------------------------------------------------------------------

namespace {

using prism::gui::StageId;
using prism::gui::StripRect;
using prism::gui::TestStrip;

prism::Image crop_of(const prism::Image& base, const StripRect& r) {
    prism::Image out;
    out.width = r.width;
    out.height = r.height;
    for (int y = r.y; y < r.y + r.height; ++y) {
        for (int x = r.x; x < r.x + r.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                out.pixels.push_back(base.pixels[(static_cast<std::size_t>(y) * base.width + x) * 3 + c]);
            }
        }
    }
    return out;
}

// The bytes of `r` taken out of a whole frame of `frame_width` pixels, 3 bytes (or `per`) per pixel.
std::vector<std::uint8_t> bytes_of(const std::vector<std::uint8_t>& whole, int frame_width,
                                   const StripRect& r, int per) {
    std::vector<std::uint8_t> out;
    for (int y = r.y; y < r.y + r.height; ++y) {
        for (int x = r.x; x < r.x + r.width; ++x) {
            for (int c = 0; c < per; ++c) {
                out.push_back(whole[(static_cast<std::size_t>(y) * frame_width + x) * per + c]);
            }
        }
    }
    return out;
}

prism::gui::PreviewOptions strip_options(prism::gui::StripStep step, bool scopes, bool mask) {
    prism::gui::PreviewOptions o;
    o.scopes = scopes;
    o.clip_mask = mask;
    o.test_strip = TestStrip{step, 5};
    return o;
}

}  // namespace

TEST(PreviewStrip, EachBandIsItsCropRenderedAloneAtItsOwnExposure) {
    for (const auto& size : {std::pair{47, 31}, std::pair{31, 47}, std::pair{64, 40}}) {
        const auto base = make_base(size.first, size.second);
        EditSettings s = every_stage();
        s.exposure_stops = 0.3f;
        for (const auto step : {prism::gui::StripStep::third, prism::gui::StripStep::half,
                                prism::gui::StripStep::whole}) {
            for (const StageId stage : {StageId::exposure, StageId::contrast, StageId::encode}) {
                const auto opts = strip_options(step, true, true);
                const auto frame = prism::gui::render_preview(*base, s, 1, {}, {}, stage, opts);
                ASSERT_TRUE(frame && frame->error.empty());
                EXPECT_EQ(frame->width, size.first);
                EXPECT_EQ(frame->height, size.second);
                EXPECT_EQ(frame->rgb8.size(), static_cast<std::size_t>(size.first) * size.second * 3);
                EXPECT_EQ(frame->clip_mask.size(), static_cast<std::size_t>(size.first) * size.second);
                EXPECT_FALSE(frame->clips) << "no clip report while the strip is on";
                ASSERT_EQ(frame->strip.size(), 5u);
                const auto bands = prism::gui::strip_bands(size.first, size.second, 5);
                for (int i = 0; i < 5; ++i) {
                    const StripRect& r = bands[static_cast<std::size_t>(i)];
                    EditSettings band = s;
                    band.exposure_stops = 0.3f + prism::gui::strip_offset(step, 5, i);
                    prism::gui::PreviewOptions alone;
                    alone.scopes = true;
                    alone.clip_mask = true;
                    const auto expected = prism::gui::render_preview(crop_of(*base, r), band, 1, {},
                                                                     {}, stage, alone);
                    ASSERT_TRUE(expected);
                    const auto& got = frame->strip[static_cast<std::size_t>(i)];
                    EXPECT_EQ(got.rect, r);
                    EXPECT_EQ(bytes_of(frame->rgb8, size.first, r, 3), expected->rgb8)
                        << "band " << i << " stage " << int(stage);
                    EXPECT_EQ(bytes_of(frame->clip_mask, size.first, r, 1), expected->clip_mask)
                        << "band " << i << " stage " << int(stage);
                    EXPECT_EQ(got.histogram, expected->histogram) << "band " << i;
                    EXPECT_EQ(got.scopes, expected->scopes) << "band " << i;
                }
            }
        }
    }
}

TEST(PreviewStrip, TheMiddleBandIsTheSameCropOfANormalRender) {
    const auto base = make_base(53, 29);
    EditSettings s = every_stage();
    const auto opts = strip_options(prism::gui::StripStep::half, true, true);
    for (const StageId stage : {StageId::exposure, StageId::saturation, StageId::encode}) {
        const auto strip = prism::gui::render_preview(*base, s, 1, {}, {}, stage, opts);
        prism::gui::PreviewOptions plain;
        plain.clip_mask = true;
        plain.scopes = true;
        const auto normal = prism::gui::render_preview(*base, s, 1, {}, {}, stage, plain);
        ASSERT_TRUE(strip && normal);
        const StripRect r = strip->strip[2].rect;
        EXPECT_EQ(bytes_of(strip->rgb8, 53, r, 3), bytes_of(normal->rgb8, 53, r, 3));
        EXPECT_EQ(bytes_of(strip->clip_mask, 53, r, 1), bytes_of(normal->clip_mask, 53, r, 1));
        EXPECT_EQ(strip->stages.size(), normal->stages.size());
    }
}

TEST(PreviewStrip, TheMiddleBandIsACropOfTheWholeNormalRenderInEitherOrientation) {
    // A normal render of the whole picture at the current exposure, then cropped to the middle
    // band: landscape and portrait (odd sizes, so the bands are uneven), at Contrast and the other
    // stages, for every step, single- and multi-threaded.
    for (const auto& size : {std::pair{67, 41}, std::pair{41, 67}, std::pair{2, 7}}) {
        const auto base = make_base(size.first, size.second);
        EditSettings s = every_stage();
        s.exposure_stops = -0.35f;
        for (const StageId stage : {StageId::contrast, StageId::exposure, StageId::curve}) {
            prism::gui::PreviewOptions plain;
            plain.clip_mask = true;
            const auto normal = prism::gui::render_preview(*base, s, 1, {}, {}, stage, plain);
            ASSERT_TRUE(normal);
            for (const auto step : {prism::gui::StripStep::third, prism::gui::StripStep::half,
                                    prism::gui::StripStep::whole}) {
                for (const int threads : {1, 0}) {
                    const auto strip = prism::gui::render_preview(
                        *base, s, threads, {}, {}, stage, strip_options(step, false, true));
                    ASSERT_TRUE(strip);
                    const StripRect r = strip->strip[2].rect;
                    ASSERT_GT(r.width * r.height, 0);
                    EXPECT_EQ(bytes_of(strip->rgb8, size.first, r, 3),
                              bytes_of(normal->rgb8, size.first, r, 3))
                        << size.first << "x" << size.second << " stage " << int(stage);
                    EXPECT_EQ(bytes_of(strip->clip_mask, size.first, r, 1),
                              bytes_of(normal->clip_mask, size.first, r, 1))
                        << size.first << "x" << size.second << " stage " << int(stage);
                }
            }
        }
    }
}

TEST(PreviewStrip, WithExposureOffTheMiddleBandIsUnedited) {
    const auto base = make_base(40, 20);
    EditSettings s = every_stage();
    s.exposure_on = false;
    s.exposure_stops = 3.0f;  // ignored while off
    const auto frame = prism::gui::render_preview(*base, s, 1, {}, {}, StageId::exposure,
                                                  strip_options(prism::gui::StripStep::half, false, false));
    ASSERT_TRUE(frame);
    const StripRect r = frame->strip[2].rect;
    EditSettings neutral = s;
    neutral.exposure_stops = 0.0f;
    EXPECT_EQ(bytes_of(frame->rgb8, 40, r, 3),
              bytes_of(stage_bytes(*base, neutral, StageId::exposure), 40, r, 3));
    // Neighbouring bands differ from it.
    EXPECT_NE(bytes_of(frame->rgb8, 40, frame->strip[1].rect, 3),
              bytes_of(stage_bytes(*base, neutral, StageId::exposure), 40, frame->strip[1].rect, 3));
}

TEST(PreviewStrip, TheMaskIsPerBand) {
    // Uniform grey 0.3: clipped only in the bands bright enough (+1 stop is still 0.6; +2 is 1.2).
    auto img = std::make_shared<prism::Image>();
    img->width = 50;
    img->height = 10;
    img->pixels.assign(50 * 10 * 3, 0.3f);
    EditSettings s;
    const auto frame = prism::gui::render_preview(
        *img, s, 1, {}, {}, StageId::exposure, strip_options(prism::gui::StripStep::whole, false, true));
    ASSERT_TRUE(frame);
    for (int i = 0; i < 5; ++i) {
        const auto m = bytes_of(frame->clip_mask, 50, frame->strip[static_cast<std::size_t>(i)].rect, 1);
        const bool any_high = std::any_of(m.begin(), m.end(), [](std::uint8_t b) { return b != 0; });
        EXPECT_EQ(any_high, i == 4) << "band " << i;  // only +2 stops (1.2) passes the ceiling
        if (i == 4) {
            EXPECT_TRUE(std::all_of(m.begin(), m.end(), [&](std::uint8_t b) { return b == m[0] && b != 0; }));
        }
    }
}

TEST(PreviewStrip, ScopesAndMaskAreOnlyMadeIfAsked) {
    const auto base = make_base(30, 30);
    const auto frame = prism::gui::render_preview(*base, every_stage(), 1, {}, {}, StageId::encode,
                                                  strip_options(prism::gui::StripStep::half, false, false));
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->scopes);
    EXPECT_TRUE(frame->clip_mask.empty());
    for (const auto& b : frame->strip) {
        EXPECT_FALSE(b.scopes);
        EXPECT_GT(b.histogram.pixel_count, 0u);
    }
    // The frame's own histogram and scopes are the middle band's.
    EXPECT_EQ(frame->histogram, frame->strip[2].histogram);
}

TEST(PreviewStrip, ReportAndCompareAreIgnoredWhileItIsOn) {
    const auto base = make_base(30, 30);
    auto opts = strip_options(prism::gui::StripStep::half, false, false);
    opts.clip_report = true;
    opts.compare_stage = StageId::decode;
    const auto frame = prism::gui::render_preview(*base, every_stage(), 1, {}, {}, StageId::exposure, opts);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->clips);
    EXPECT_FALSE(frame->compare);
}

TEST(PreviewStrip, DecodeAndOddImagesAreHandled) {
    const auto base = make_base(30, 20);
    const auto decode = prism::gui::render_preview(*base, every_stage(), 1, {}, {}, StageId::decode,
                                                   strip_options(prism::gui::StripStep::half, true, true));
    ASSERT_TRUE(decode);
    EXPECT_TRUE(decode->strip.empty());  // a strip of the Decode stage is ignored
    EXPECT_EQ(decode->rgb8, stage_bytes(*base, every_stage(), StageId::decode));

    // Fewer pixels than bands: some bands are empty, the picture is still complete.
    const auto tiny = make_base(3, 2);
    const auto frame = prism::gui::render_preview(*tiny, every_stage(), 1, {}, {}, StageId::encode,
                                                  strip_options(prism::gui::StripStep::half, true, true));
    ASSERT_TRUE(frame);
    ASSERT_EQ(frame->strip.size(), 5u);
    int empty = 0;
    for (const auto& b : frame->strip) {
        empty += b.rect.width == 0 ? 1 : 0;
    }
    EXPECT_EQ(empty, 2);
    EXPECT_EQ(frame->rgb8.size(), 3u * 2 * 3);
}

TEST(PreviewStrip, StripOffGivesExactlyTheOldFrame) {
    const auto base = make_base(32, 24);
    prism::gui::PreviewOptions none;
    const auto a = prism::gui::render_preview(*base, every_stage(), 1, {});
    const auto b = prism::gui::render_preview(*base, every_stage(), 1, {}, {}, prism::gui::final_stage, none);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->rgb8, b->rgb8);
    EXPECT_TRUE(b->strip.empty());
}

TEST(PreviewStrip, ThreadsDoNotChangeTheResult) {
    const auto base = make_base(61, 37);
    const auto opts = strip_options(prism::gui::StripStep::third, true, true);
    const auto one = prism::gui::render_preview(*base, every_stage(), 1, {}, {}, StageId::encode, opts);
    const auto four = prism::gui::render_preview(*base, every_stage(), 4, {}, {}, StageId::encode, opts);
    ASSERT_TRUE(one && four);
    EXPECT_EQ(one->rgb8, four->rgb8);
    EXPECT_EQ(one->clip_mask, four->clip_mask);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(one->strip[i].histogram, four->strip[i].histogram);
        EXPECT_EQ(one->strip[i].scopes, four->strip[i].scopes);
    }
}

TEST(PreviewStrip, ACancelledRenderGivesNoFrame) {
    const auto base = make_base(16, 16);
    std::stop_source source;
    source.request_stop();
    EXPECT_FALSE(prism::gui::render_preview(*base, every_stage(), 1, source.get_token(), {},
                                            StageId::exposure,
                                            strip_options(prism::gui::StripStep::half, false, false)));
}

TEST(PreviewStrip, WorksThroughTheWorker) {
    const auto base = make_base(40, 30);
    PreviewRenderer renderer(2);
    renderer.submit(base, every_stage(), StageId::encode, strip_options(prism::gui::StripStep::half, true, true));
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const auto frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_TRUE(frame->error.empty());
    EXPECT_EQ(frame->strip.size(), 5u);
}

// --- Test strip: bands rendered at the same time --------------------------------------------------

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <dirent.h>
#endif

namespace {

// The threads this process has now, where the platform can tell (-1 otherwise).
int thread_count() {
#if defined(__APPLE__)
    thread_act_array_t list = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) {
        return -1;
    }
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        mach_port_deallocate(mach_task_self(), list[i]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), count * sizeof(*list));
    return static_cast<int>(count);
#elif defined(__linux__)
    DIR* dir = opendir("/proc/self/task");
    if (!dir) {
        return -1;
    }
    int count = 0;
    while (const dirent* e = readdir(dir)) {
        count += e->d_name[0] != '.' ? 1 : 0;
    }
    closedir(dir);
    return count;
#else
    return -1;
#endif
}

// The threads this process has before a strip render under test. A sanitizer or the runtime may
// start a thread of its own lazily the first time the process creates one (ThreadSanitizer on
// Linux spawns its background thread then), which would count as a leak if the baseline were taken
// before it. So a strip render that starts workers runs once first, and the baseline comes after.
int baseline_thread_count(const EditSettings& settings, const prism::Image& base) {
    static const bool warmed = [&] {
        (void)prism::gui::render_preview(base, settings, 4, {}, {}, StageId::encode,
                                         strip_options(prism::gui::StripStep::half, false, false));
        return true;
    }();
    (void)warmed;
    return thread_count();
}

// No thread started by the render is still there (where the platform can count them). A joined
// thread has finished running the moment join() returns, but the kernel may list it for a short
// while longer as it tears it down, so this waits briefly for the count to drop. A real leak fails
// after the few seconds, not the generous `timeout` the blocking waits use.
void expect_no_thread_left(int before) {
    if (before < 0) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    int now = thread_count();
    while (now > before && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        now = thread_count();
    }
    EXPECT_LE(now, before);
}

}  // namespace

TEST(PreviewStripConcurrent, BandsAreBitIdenticalForEveryThreadCount) {
    // As EachBandIsItsCropRenderedAloneAtItsOwnExposure and the middle-band tests, at 1, 4 and
    // max threads.
    for (const auto& size : {std::pair{67, 41}, std::pair{41, 67}}) {
        const auto base = make_base(size.first, size.second);
        const EditSettings s = every_stage();
        prism::gui::PreviewOptions plain;
        plain.clip_mask = true;
        const auto normal = prism::gui::render_preview(*base, s, 1, {}, {}, StageId::contrast, plain);
        ASSERT_TRUE(normal);
        const auto bands = prism::gui::strip_bands(size.first, size.second, 5);
        for (const int threads : {1, 4, 0}) {
            const auto frame = prism::gui::render_preview(
                *base, s, threads, {}, {}, StageId::contrast,
                strip_options(prism::gui::StripStep::half, true, true));
            ASSERT_TRUE(frame);
            for (int i = 0; i < 5; ++i) {
                const StripRect& r = bands[static_cast<std::size_t>(i)];
                prism::gui::PreviewOptions alone;
                alone.scopes = true;
                alone.clip_mask = true;
                const auto expected = prism::gui::render_preview(
                    crop_of(*base, r), prism::gui::strip_band_settings(s, prism::gui::StripStep::half, 5, i),
                    1, {}, {}, StageId::contrast, alone);
                ASSERT_TRUE(expected);
                EXPECT_EQ(bytes_of(frame->rgb8, size.first, r, 3), expected->rgb8) << threads;
                EXPECT_EQ(bytes_of(frame->clip_mask, size.first, r, 1), expected->clip_mask) << threads;
                EXPECT_EQ(frame->strip[static_cast<std::size_t>(i)].histogram, expected->histogram);
                EXPECT_EQ(frame->strip[static_cast<std::size_t>(i)].scopes, expected->scopes);
            }
            const StripRect m = frame->strip[2].rect;
            EXPECT_EQ(bytes_of(frame->rgb8, size.first, m, 3), bytes_of(normal->rgb8, size.first, m, 3));
        }
    }
}

TEST(PreviewStripConcurrent, BandsBigEnoughForThreadsGiveTheSameResult) {
    // Bands of 1060x1000 pixels, above parallel_min_pixels: each band also splits its own work.
    auto base = std::make_shared<prism::Image>();
    base->width = 5300;
    base->height = 1000;
    base->pixels.resize(static_cast<std::size_t>(base->width) * base->height * 3);
    for (std::size_t i = 0; i < base->pixels.size(); ++i) {
        base->pixels[i] = static_cast<float>(i % 1013) / 700.0f;
    }
    EditSettings s;
    s.exposure_stops = 0.25f;
    s.contrast = 1.3f;
    const auto opts = strip_options(prism::gui::StripStep::third, false, true);
    const auto one = prism::gui::render_preview(*base, s, 1, {}, {}, StageId::contrast, opts);
    ASSERT_TRUE(one);
    for (const int threads : {4, 0, 7}) {
        const auto many = prism::gui::render_preview(*base, s, threads, {}, {}, StageId::contrast, opts);
        ASSERT_TRUE(many);
        EXPECT_EQ(many->rgb8, one->rgb8) << threads;
        EXPECT_EQ(many->clip_mask, one->clip_mask) << threads;
        for (std::size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(many->strip[i].histogram, one->strip[i].histogram) << threads;
        }
    }
}

TEST(PreviewStripConcurrent, TheStageCallbackRunsOnTheCallingThreadForTheMiddleBand) {
    const auto base = make_base(80, 40);
    for (const int threads : {1, 4, 0}) {
        const std::thread::id caller = std::this_thread::get_id();
        std::vector<std::string> names;
        bool elsewhere = false;
        const auto frame = prism::gui::render_preview(
            *base, every_stage(), threads, {},
            [&](const prism::StageTime& t) {
                elsewhere = elsewhere || std::this_thread::get_id() != caller;
                names.emplace_back(t.name);
            },
            StageId::encode, strip_options(prism::gui::StripStep::half, false, false));
        ASSERT_TRUE(frame);
        EXPECT_FALSE(elsewhere) << threads;
        EXPECT_EQ(names.size(), 5u) << "once per stage of the middle band only";
        ASSERT_EQ(frame->stages.size(), names.size());
        for (std::size_t i = 0; i < names.size(); ++i) {
            EXPECT_EQ(std::string(frame->stages[i].name), names[i]);
        }
    }
}

TEST(PreviewStripConcurrent, AStopBeforeTheRenderGivesNoFrame) {
    const auto base = make_base(64, 32);
    for (const int threads : {1, 4, 0}) {
        const int before = baseline_thread_count(every_stage(), *base);
        std::stop_source source;
        source.request_stop();
        EXPECT_FALSE(prism::gui::render_preview(*base, every_stage(), threads, source.get_token(), {},
                                                StageId::encode,
                                                strip_options(prism::gui::StripStep::half, true, true)));
        expect_no_thread_left(before);
    }
}

TEST(PreviewStripConcurrent, AStopDuringTheRenderStopsEveryBand) {
    // The stop arrives while the middle band runs (from its first stage), with the other bands
    // running or waiting on the workers: no frame, and every worker is gone when the call returns.
    const auto base = make_base(400, 200);
    for (const int threads : {1, 4, 0}) {
        const int before = baseline_thread_count(every_stage(), *base);
        std::stop_source source;
        int stages_after_stop = 0;
        const auto frame = prism::gui::render_preview(
            *base, every_stage(), threads, source.get_token(),
            [&](const prism::StageTime&) {
                if (source.stop_requested()) {
                    ++stages_after_stop;
                }
                source.request_stop();
            },
            StageId::encode, strip_options(prism::gui::StripStep::half, true, true));
        EXPECT_FALSE(frame) << threads;
        EXPECT_EQ(stages_after_stop, 0) << "the middle band stops at its next stage";
        expect_no_thread_left(before);
    }
}

TEST(PreviewStripConcurrent, AnExceptionInEveryBandReachesTheCaller) {
    // Invalid settings: every band throws, on the workers and on the calling thread.
    const auto base = make_base(64, 32);
    EditSettings bad = every_stage();
    bad.saturation = 12.0f;
    for (const int threads : {1, 4, 0}) {
        const int before = baseline_thread_count(every_stage(), *base);
        EXPECT_THROW((void)prism::gui::render_preview(*base, bad, threads, {}, {}, StageId::encode,
                                                      strip_options(prism::gui::StripStep::half, true, true)),
                     std::invalid_argument)
            << threads;
        expect_no_thread_left(before);
    }
}

TEST(PreviewStripConcurrent, AnExceptionInOneBandStopsTheOthersAndReachesTheCaller) {
    // The middle band throws (from its stage callback) while the other bands run on the workers.
    const auto base = make_base(400, 200);
    for (const int threads : {1, 4, 0}) {
        const int before = baseline_thread_count(every_stage(), *base);
        EXPECT_THROW((void)prism::gui::render_preview(
                         *base, every_stage(), threads, {},
                         [](const prism::StageTime&) { throw std::runtime_error("band failed"); },
                         StageId::encode, strip_options(prism::gui::StripStep::half, true, true)),
                     std::runtime_error)
            << threads;
        expect_no_thread_left(before);
        // The renderer is still usable afterwards.
        EXPECT_TRUE(prism::gui::render_preview(*base, every_stage(), threads, {}, {}, StageId::encode,
                                               strip_options(prism::gui::StripStep::half, false, false)));
    }
}

TEST(PreviewStripConcurrent, AnExceptionThroughTheWorkerBecomesTheFrameError) {
    const auto base = make_base(64, 32);
    EditSettings bad = every_stage();
    bad.saturation = 12.0f;
    PreviewRenderer renderer(0);
    renderer.submit(base, bad, StageId::encode, strip_options(prism::gui::StripStep::half, false, false));
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const auto frame = renderer.poll();
    ASSERT_TRUE(frame);
    EXPECT_FALSE(frame->error.empty());
    EXPECT_TRUE(frame->rgb8.empty());
}

// The worker keeps a field cache for the strokes: a stroke drawn point by point, a second stroke,
// undo and a weight change, each submitted and rendered in turn, give exactly the frames of a
// render without a cache. Two renderers share nothing (each has its own cache).
TEST(PreviewRenderer, ItsFieldCacheGivesTheFramesOfARenderWithoutOne) {
    const auto base = make_base(640, 400);
    const float aspect = prism::aspect_of(640, 400);
    const auto field_of = [aspect](std::vector<std::shared_ptr<const prism::FieldStroke>> strokes) {
        auto f = std::make_shared<prism::StrokeField>();
        f->aspect = aspect;
        f->strokes = std::move(strokes);
        return prism::gui::Strokes(std::move(f));
    };
    const auto stroke = [](std::vector<prism::FieldPoint> points, float weight) {
        return std::make_shared<const prism::FieldStroke>(
            prism::FieldStroke{std::move(points), 0.06f, 0.5f, weight});
    };
    std::vector<prism::gui::Strokes> steps;
    std::vector<prism::FieldPoint> drawn;
    std::shared_ptr<const prism::FieldStroke> first;
    for (const prism::FieldPoint p : {prism::FieldPoint{0.2f, 0.2f}, {0.4f, 0.3f}, {0.6f, 0.5f},
                                      {0.4f, 0.6f}, {0.3f, 0.25f}}) {
        drawn.push_back(p);
        first = stroke(drawn, 1.0f);
        steps.push_back(field_of({first}));
    }
    const auto second = stroke({{0.7f, 0.2f}, {0.8f, 0.8f}}, -0.7f);
    steps.push_back(field_of({first, second}));
    steps.push_back(field_of({first}));  // undo
    steps.push_back(field_of({stroke(drawn, 0.5f)}));
    steps.emplace_back();  // cleared

    PreviewRenderer renderer(2);
    PreviewRenderer other(2);
    for (std::size_t k = 0; k < steps.size(); ++k) {
        EditSettings s = every_stage();
        s.strokes = steps[k];
        prism::gui::PreviewOptions options;
        options.clip_report = k % 2 == 1;
        renderer.submit(base, s, prism::gui::final_stage, options);
        other.submit(base, s, prism::gui::final_stage, options);
        ASSERT_TRUE(renderer.wait_until_idle(timeout));
        ASSERT_TRUE(other.wait_until_idle(timeout));
        const std::optional<PreviewFrame> got = renderer.poll();
        const std::optional<PreviewFrame> got_other = other.poll();
        const std::optional<PreviewFrame> want = prism::gui::render_preview(
            *base, s, 1, {}, {}, prism::gui::final_stage, options, nullptr);
        ASSERT_TRUE(got && got_other && want) << k;
        ASSERT_TRUE(got->error.empty()) << got->error;
        EXPECT_TRUE(got->rgb8 == want->rgb8) << "step " << k;
        EXPECT_TRUE(got->histogram == want->histogram) << "step " << k;
        EXPECT_TRUE(got->clips == want->clips) << "step " << k;
        EXPECT_TRUE(got_other->rgb8 == want->rgb8) << "step " << k;
    }
}

// A render with the field cache hands the cache's field to the Dodge & burn stage instead of
// rasterising the strokes again; the pictures are the same either way, so the cache counts the
// reads. Warm (the same strokes again) each kind of render reads it, and takes the reuse path.
TEST(PreviewRenderer, RendersReadTheCachedStrokeFieldPlainStripAndClipReport) {
    const auto base = make_base(320, 200);
    auto field = std::make_shared<prism::StrokeField>();
    field->aspect = prism::aspect_of(320, 200);
    field->strokes.push_back(std::make_shared<const prism::FieldStroke>(
        prism::FieldStroke{{{0.3f, 0.3f}, {0.6f, 0.5f}}, 0.1f, 0.5f, 1.0f}));
    EditSettings s = every_stage();
    s.strokes = prism::gui::Strokes(field);

    prism::gui::PreviewOptions plain;
    prism::gui::PreviewOptions clip;
    clip.clip_report = true;
    const prism::gui::PreviewOptions strip =
        strip_options(prism::gui::StripStep::half, false, false);
    for (const prism::gui::PreviewOptions& options : {plain, clip, strip}) {
        prism::gui::FieldCache cache;
        ASSERT_EQ(cache.field_reads(), 0u);
        const auto cold = prism::gui::render_preview(*base, s, 1, {}, {}, prism::gui::final_stage,
                                                     options, &cache);
        ASSERT_TRUE(cold && cold->error.empty());
        EXPECT_GT(cache.field_reads(), 0u);
        const std::size_t after_cold = cache.field_reads();
        const auto warm = prism::gui::render_preview(*base, s, 1, {}, {}, prism::gui::final_stage,
                                                     options, &cache);
        ASSERT_TRUE(warm && warm->error.empty());
        EXPECT_EQ(cache.last_path(), prism::gui::FieldPath::reuse);
        EXPECT_GT(cache.field_reads(), after_cold);
        EXPECT_TRUE(warm->rgb8 == cold->rgb8);
    }
}
