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
    prism::gui::StageId::decode,     prism::gui::StageId::exposure, prism::gui::StageId::matrix,
    prism::gui::StageId::saturation, prism::gui::StageId::contrast, prism::gui::StageId::curve,
    prism::gui::StageId::encode};

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
