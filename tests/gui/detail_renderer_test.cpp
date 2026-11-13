#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "env_var.hpp"
#include "prism/frame.hpp"
#include "prism/gui/detail_renderer.hpp"
#include "prism/gui/detail_view.hpp"
#include "prism/gui/session.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"
#include "prism/parallel.hpp"
#include "prism/raw.hpp"
#include "prism/resize.hpp"
#include "prism/stroke_field.hpp"

// DetailRenderer (#37): the stop rule, latest wins, stale frames, the budget and the base's
// lifetime with an injected render whose timing the test decides; and the real render's pixels
// against export's. As in preview_renderer_test.cpp: no sleeps; the fake render's release count
// and stop tokens decide the order of events, and the timeouts only matter if something is broken.
// Runs under ThreadSanitizer in CI.

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

using prism::FieldStroke;
using prism::Frame;
using prism::FrameBox;
using prism::Image;
using prism::Photo;
using prism::PixelSize;
using prism::StrokeField;
using prism::gui::DetailAdmit;
using prism::gui::DetailFrame;
using prism::gui::DetailJob;
using prism::gui::DetailKey;
using prism::gui::DetailRenderer;
using prism::gui::DetailRenderRequest;
using prism::gui::EditSettings;
using prism::gui::Error;
using prism::gui::FileScale;
using prism::gui::RegionBudget;
using prism::gui::Strokes;

constexpr auto timeout = 30s;

// --- Pure decisions ------------------------------------------------------------------------------

TEST(DetailRendererRules, BoxContains) {
    const FrameBox outer{100, 50, 300, 200};
    EXPECT_TRUE(prism::gui::box_contains(outer, outer));
    EXPECT_TRUE(prism::gui::box_contains(outer, {150, 60, 10, 10}));
    EXPECT_TRUE(prism::gui::box_contains(outer, {100, 50, 1, 1}));
    EXPECT_TRUE(prism::gui::box_contains(outer, {399, 249, 1, 1}));
    EXPECT_FALSE(prism::gui::box_contains(outer, {99, 60, 10, 10}));    // past the left
    EXPECT_FALSE(prism::gui::box_contains(outer, {150, 49, 10, 10}));   // past the top
    EXPECT_FALSE(prism::gui::box_contains(outer, {391, 60, 10, 10}));   // one past the right
    EXPECT_FALSE(prism::gui::box_contains(outer, {150, 241, 10, 10}));  // one past the bottom
    EXPECT_FALSE(prism::gui::box_contains(outer, {0, 0, 1000, 1000}));  // around it
    EXPECT_TRUE(prism::gui::box_contains(outer, {0, 0, 0, 0}));         // empty: inside anything
}

TEST(DetailRendererRules, StopRule) {
    const int a = 0;
    const int b = 0;
    const DetailJob running{&a, 1, {128, 128, 512, 384}};
    // The same region (a settings change) or a covered one: not stopped.
    EXPECT_FALSE(prism::gui::detail_stops_running(running, running));
    EXPECT_FALSE(prism::gui::detail_stops_running(running, {&a, 1, {200, 200, 128, 128}}));
    // A pan out, by one pixel or all the way: stopped.
    EXPECT_TRUE(prism::gui::detail_stops_running(running, {&a, 1, {129, 128, 512, 384}}));
    EXPECT_TRUE(prism::gui::detail_stops_running(running, {&a, 1, {4000, 3000, 512, 384}}));
    // Zoomed out over it: stopped.
    EXPECT_TRUE(prism::gui::detail_stops_running(running, {&a, 1, {0, 0, 1024, 768}}));
    // Another base, by pointer or by number: stopped, whatever the region.
    EXPECT_TRUE(prism::gui::detail_stops_running(running, {&b, 1, {128, 128, 512, 384}}));
    EXPECT_TRUE(prism::gui::detail_stops_running(running, {&a, 2, {128, 128, 512, 384}}));
}

TEST(DetailRendererRules, Admit) {
    using prism::gui::detail_admit;
    EXPECT_EQ(detail_admit(RegionBudget::live, false), DetailAdmit::render);
    EXPECT_EQ(detail_admit(RegionBudget::live, true), DetailAdmit::render);
    EXPECT_EQ(detail_admit(RegionBudget::on_release, false), DetailAdmit::render);
    EXPECT_EQ(detail_admit(RegionBudget::on_release, true), DetailAdmit::refused_gesture);
    EXPECT_EQ(detail_admit(RegionBudget::never, false), DetailAdmit::refused_too_big);
    EXPECT_EQ(detail_admit(RegionBudget::never, true), DetailAdmit::refused_too_big);
}

TEST(DetailRendererRules, FrameWanted) {
    using prism::gui::detail_frame_wanted;
    EXPECT_TRUE(detail_frame_wanted(3, 1, 3, 0));   // the first of this base
    EXPECT_TRUE(detail_frame_wanted(3, 8, 3, 7));   // newer than the one shown
    EXPECT_FALSE(detail_frame_wanted(3, 7, 3, 7));  // the one shown
    EXPECT_FALSE(detail_frame_wanted(3, 6, 3, 7));  // older than the one shown
    EXPECT_FALSE(detail_frame_wanted(2, 8, 3, 7));  // another base (photo)
}

// --- The worker, with a render the test controls -------------------------------------------------

// A render that records each call and then waits until the test releases it, or until it is
// stopped. Stopped, it still returns a whole image, as a stage that was under way when the stop
// came would finish: the renderer must keep that out by its own checks.
class FakeRender {
public:
    FakeRender() = default;
    // With `ignore_stop`, a call waits for its release even once stopped, as a render stuck in a
    // long stage would.
    explicit FakeRender(bool ignore_stop) : ignore_stop_(ignore_stop) {}

    struct Call {
        FrameBox region;
        float exposure = 0.0f;  // the request's settings, as the tests tag them
        std::stop_token stop;
    };

    DetailRenderer::RenderFunction function() {
        return [this](const Photo&, const EditSettings& s, const FrameBox& region,
                      std::stop_token stop) -> std::optional<Image> {
            std::size_t index = 0;
            {
                std::unique_lock lock(mutex_);
                index = calls_.size();
                calls_.push_back({region, s.exposure_stops, stop});
                changed_.notify_all();
                if (ignore_stop_) {
                    (void)changed_.wait_for(lock, timeout, [&] { return released_ > index; });
                } else {
                    (void)changed_.wait_for(lock, stop, timeout, [&] { return released_ > index; });
                }
            }
            Image img;
            img.width = region.width;
            img.height = region.height;
            img.pixels.assign(static_cast<std::size_t>(region.width) *
                                  static_cast<std::size_t>(region.height) * 3,
                              0.25f);
            return img;
        };
    }

    // Blocks until `count` calls have started; false if not within the timeout.
    bool wait_for_calls(std::size_t count) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return calls_.size() >= count; });
    }
    // Lets `count` more calls return.
    void release(std::size_t count = 1) {
        {
            std::lock_guard lock(mutex_);
            released_ += count;
        }
        changed_.notify_all();
    }
    std::vector<Call> calls() {
        std::lock_guard lock(mutex_);
        return calls_;
    }

private:
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::vector<Call> calls_;
    std::size_t released_ = 0;
    bool ignore_stop_ = false;
};

std::shared_ptr<const Photo> tiny_photo() {
    Photo p;
    p.image.width = 4;
    p.image.height = 4;
    p.image.pixels.assign(48, 0.5f);
    return std::make_shared<const Photo>(std::move(p));
}

// A request tagged by its settings generation, which the fake render sees as the exposure.
DetailRenderRequest request(std::shared_ptr<const Photo> base, std::uint64_t base_id,
                            std::uint64_t settings, const FrameBox& region,
                            RegionBudget budget = RegionBudget::live) {
    DetailRenderRequest r;
    r.base = std::move(base);
    r.settings.exposure_stops = static_cast<float>(settings) / 8.0f;
    r.request = {region, budget};
    r.output = {region.width, region.height};
    r.key = {base_id, settings};
    return r;
}

float tag(std::uint64_t settings) {
    return static_cast<float>(settings) / 8.0f;
}

const FrameBox region_a{256, 128, 640, 384};

TEST(DetailRenderer, APanOutStopsTheRunningRenderWhichPostsNothing) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    ASSERT_EQ(renderer.submit(request(base, 1, 1, region_a), false), 1u);
    ASSERT_TRUE(fake.wait_for_calls(1));

    // One cell to the right: not inside the running region.
    const FrameBox panned{region_a.x0 + 128, region_a.y0, region_a.width, region_a.height};
    ASSERT_EQ(renderer.submit(request(base, 1, 1, panned), false), 2u);
    EXPECT_TRUE(fake.calls()[0].stop.stop_requested());

    // The stopped render returned a whole image all the same; the next one has started, so the
    // stopped one is over, and it posted nothing.
    ASSERT_TRUE(fake.wait_for_calls(2));
    EXPECT_FALSE(renderer.poll(1).has_value());

    fake.release(2);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->generation, 2u);
    EXPECT_EQ(frame->region, panned);
    EXPECT_EQ(frame->error, "");
    EXPECT_EQ(frame->rgb8.size(),
              static_cast<std::size_t>(panned.width) * static_cast<std::size_t>(panned.height) * 3);
    EXPECT_FALSE(fake.calls()[1].stop.stop_requested());
}

TEST(DetailRenderer, ASettingsChangeOnTheSameRegionLetsTheRenderFinish) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    renderer.submit(request(base, 1, 2, region_a), false);
    EXPECT_FALSE(fake.calls()[0].stop.stop_requested());

    // The first finishes and is shown; then the newer settings render.
    fake.release();
    ASSERT_TRUE(fake.wait_for_calls(2));
    std::optional<DetailFrame> first = renderer.poll(1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->key, (DetailKey{1, 1}));
    EXPECT_EQ(fake.calls()[1].exposure, tag(2));

    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    std::optional<DetailFrame> second = renderer.poll(1);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->key, (DetailKey{1, 2}));
    EXPECT_FALSE(fake.calls()[0].stop.stop_requested());
}

TEST(DetailRenderer, ACoveredRegionDoesNotStopIt) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    const FrameBox inside{region_a.x0 + 128, region_a.y0 + 128, 256, 128};
    renderer.submit(request(base, 1, 1, inside), false);
    EXPECT_FALSE(fake.calls()[0].stop.stop_requested());
    fake.release(2);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    ASSERT_EQ(fake.calls().size(), 2u);
    EXPECT_FALSE(fake.calls()[0].stop.stop_requested());
    EXPECT_EQ(fake.calls()[1].region, inside);
}

TEST(DetailRenderer, ANewBaseStopsIt) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto first = tiny_photo();
    const auto second = tiny_photo();
    renderer.submit(request(first, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    renderer.submit(request(second, 2, 1, region_a), false);
    EXPECT_TRUE(fake.calls()[0].stop.stop_requested());
    ASSERT_TRUE(fake.wait_for_calls(2));
    EXPECT_FALSE(renderer.poll(2).has_value());

    // The same photo read again (a new number for the same pointer) stops it too.
    renderer.submit(request(second, 3, 1, region_a), false);
    EXPECT_TRUE(fake.calls()[1].stop.stop_requested());
    fake.release(3);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<DetailFrame> frame = renderer.poll(3);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->key.base, 3u);
}

TEST(DetailRenderer, LatestWinsOnlyTheLastWaitingRequestRuns) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    for (std::uint64_t s = 2; s <= 4; ++s) {
        renderer.submit(request(base, 1, s, region_a), false);
    }
    fake.release(4);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::vector<FakeRender::Call> calls = fake.calls();
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0].exposure, tag(1));
    EXPECT_EQ(calls[1].exposure, tag(4));
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->key.settings, 4u);
    EXPECT_EQ(frame->generation, 4u);
}

TEST(DetailRenderer, AFrameOfAnotherBaseIsDropped) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    renderer.submit(request(tiny_photo(), 1, 1, region_a), false);
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    // The UI has moved to another photo before polling.
    EXPECT_FALSE(renderer.poll(2).has_value());
    EXPECT_FALSE(renderer.poll(1).has_value());  // taken and dropped, not kept for later
}

// Requests run in order, so a frame older than the one shown can't come out of the worker; the
// rule that would drop one is FrameWanted's. Here: each frame is shown once, the newer after.
TEST(DetailRenderer, EachFrameIsShownOnceInOrder) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    renderer.submit(request(base, 1, 1, region_a), false);
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    ASSERT_TRUE(renderer.poll(1).has_value());
    renderer.submit(request(base, 1, 2, region_a), false);
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->generation, 2u);
    EXPECT_FALSE(renderer.poll(1).has_value());
}

TEST(DetailRenderer, BudgetNeverIsRefusedOnReleaseWaitsForTheGesture) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    EXPECT_EQ(renderer.submit(request(base, 1, 1, region_a, RegionBudget::never), false), 0u);
    EXPECT_EQ(renderer.submit(request(base, 1, 1, region_a, RegionBudget::never), true), 0u);
    EXPECT_EQ(renderer.submit(request(base, 1, 1, region_a, RegionBudget::on_release), true), 0u);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    EXPECT_TRUE(fake.calls().empty());
    EXPECT_FALSE(renderer.poll(1).has_value());

    // The gesture is over: the caller asks again, and it runs.
    EXPECT_EQ(renderer.submit(request(base, 1, 1, region_a, RegionBudget::on_release), false), 1u);
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    ASSERT_EQ(fake.calls().size(), 1u);
    EXPECT_TRUE(renderer.poll(1).has_value());
}

TEST(DetailRenderer, ARefusedRequestDropsTheWaitingOne) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    const auto base = tiny_photo();
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    renderer.submit(request(base, 1, 2, region_a), false);
    EXPECT_EQ(renderer.submit(request(base, 1, 3, region_a, RegionBudget::on_release), true), 0u);
    fake.release(2);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    EXPECT_EQ(fake.calls().size(), 1u);
    EXPECT_FALSE(fake.calls()[0].stop.stop_requested());  // covered: the running one finished
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->key.settings, 1u);
}

TEST(DetailRenderer, KeepsTheBaseWhileRenderingAndFreesItAfter) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    std::shared_ptr<const Photo> base = tiny_photo();
    const std::weak_ptr<const Photo> watch = base;
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    base.reset();  // the session lets go of it
    EXPECT_FALSE(watch.expired());
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    EXPECT_TRUE(watch.expired());  // the render held the last reference, and let it go
    EXPECT_TRUE(renderer.poll(1).has_value());
}

TEST(DetailRenderer, DestructionStopsTheRenderAndJoins) {
    FakeRender fake;
    std::weak_ptr<const Photo> watch;
    {
        DetailRenderer renderer(fake.function());
        std::shared_ptr<const Photo> base = tiny_photo();
        watch = base;
        renderer.submit(request(base, 1, 1, region_a), false);
        ASSERT_TRUE(fake.wait_for_calls(1));
        // The same region with newer settings: it waits rather than stopping the first.
        renderer.submit(request(std::move(base), 1, 2, region_a), false);
        EXPECT_FALSE(fake.calls()[0].stop.stop_requested());
        // Never released: only the destructor's stop ends the render.
    }
    const std::vector<FakeRender::Call> calls = fake.calls();
    ASSERT_EQ(calls.size(), 1u);  // the waiting one never started
    EXPECT_TRUE(calls[0].stop.stop_requested());
    EXPECT_TRUE(watch.expired());  // neither the render nor the waiting request kept the base
}

TEST(DetailRenderer, ClearStopsTheRenderAndDropsTheWaitingOne) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    std::shared_ptr<const Photo> base = tiny_photo();
    const std::weak_ptr<const Photo> watch = base;
    renderer.submit(request(base, 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    renderer.submit(request(base, 1, 2, region_a), false);  // waits: same region
    base.reset();                                           // the session lets go of it

    renderer.clear();
    EXPECT_TRUE(fake.calls()[0].stop.stop_requested());
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    EXPECT_EQ(fake.calls().size(), 1u);  // the waiting one never ran
    EXPECT_FALSE(renderer.poll(1).has_value());
    EXPECT_TRUE(watch.expired());
}

// Where a base was freed, and that it was.
class FreedOn {
public:
    std::shared_ptr<const Photo> photo() {
        return std::shared_ptr<const Photo>(new Photo(*tiny_photo()), [this](const Photo* p) {
            {
                std::lock_guard lock(mutex_);
                thread_ = std::this_thread::get_id();
                freed_ = true;
            }
            delete p;
        });
    }
    std::optional<std::thread::id> thread() {
        std::lock_guard lock(mutex_);
        return freed_ ? std::optional(thread_) : std::nullopt;
    }

private:
    std::mutex mutex_;
    std::thread::id thread_;
    bool freed_ = false;
};

// A waiting request replaced before it ran may hold the last reference to a base (another photo's,
// ~700 MB): it is freed on the worker thread, not on the UI thread that replaced it.
TEST(DetailRenderer, AReplacedRequestsBaseIsFreedOnTheWorker) {
    FakeRender fake(true);
    FreedOn freed;
    DetailRenderer renderer(fake.function());
    renderer.submit(request(tiny_photo(), 1, 1, region_a), false);
    ASSERT_TRUE(fake.wait_for_calls(1));
    // Another base: stops the first, which goes on until released; this one waits.
    renderer.submit(request(freed.photo(), 2, 1, region_a), false);
    EXPECT_TRUE(fake.calls()[0].stop.stop_requested());
    // Yet another base replaces it: the waiting request held the last reference.
    renderer.submit(request(tiny_photo(), 3, 1, region_a), false);
    EXPECT_FALSE(freed.thread().has_value());  // not freed here, on the UI thread
    fake.release(2);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    ASSERT_TRUE(freed.thread().has_value());
    EXPECT_NE(*freed.thread(), std::this_thread::get_id());
    ASSERT_EQ(fake.calls().size(), 2u);
    const std::optional<DetailFrame> frame = renderer.poll(3);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->key.base, 3u);
}

TEST(DetailRenderer, AFailedRenderCarriesItsMessage) {
    DetailRenderer renderer([](const Photo&, const EditSettings&, const FrameBox&,
                               std::stop_token) -> std::optional<Image> {
        throw std::runtime_error("no memory today");
    });
    renderer.submit(request(tiny_photo(), 1, 1, region_a), false);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->error, "no memory today");
    EXPECT_TRUE(frame->rgb8.empty());
}

TEST(DetailRenderer, ABadRequestIsRefusedAtOnce) {
    FakeRender fake;
    DetailRenderer renderer(fake.function());
    DetailRenderRequest r = request(tiny_photo(), 1, 1, region_a);
    r.base = nullptr;
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    r = request(tiny_photo(), 1, 1, {0, 0, 0, 10});
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    r = request(tiny_photo(), 1, 1, region_a);
    r.output = {region_a.width + 1, region_a.height};
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    r.output = {0, 1};
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    // Smaller, but not what downscale makes of the region (640 x 384 at 320 is 320 x 192).
    r.output = {320, 191};
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    r.output = {region_a.width, region_a.height - 1};
    EXPECT_THROW(renderer.submit(r, false), std::invalid_argument);
    r.output = {320, 192};
    EXPECT_EQ(renderer.submit(r, false), 1u);
    fake.release();
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    ASSERT_EQ(fake.calls().size(), 1u);
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->size, (PixelSize{320, 192}));
    EXPECT_EQ(frame->rgb8.size(), std::size_t{320} * 192 * 3);
}

TEST(DetailRenderer, ARenderThrowingSomethingElseFailsToo) {
    DetailRenderer renderer([](const Photo&, const EditSettings&, const FrameBox&,
                               std::stop_token) -> std::optional<Image> { throw 42; });
    renderer.submit(request(tiny_photo(), 1, 1, region_a), false);
    ASSERT_TRUE(renderer.wait_until_idle(timeout));
    const std::optional<DetailFrame> frame = renderer.poll(1);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->error, "The detail render failed.");
}

// --- The real render against export --------------------------------------------------------------

// As render_region_test.cpp's: a photo of varied colours, strokes with edges everywhere, every
// stage on.
Photo make_photo(int w, int h) {
    Photo photo;
    photo.image.width = w;
    photo.image.height = h;
    photo.image.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = photo.image.pixels.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(x)) *
                           3;
            const float s = 0.03f + static_cast<float>((x * 7 + y * 13) % 101) / 120.0f;
            p[0] = s;
            p[1] = 0.6f * s + 0.02f * static_cast<float>(x % 5);
            p[2] = 0.2f + static_cast<float>(y % 11) / 30.0f;
        }
    }
    photo.info.as_shot_white_xy = std::array<double, 2>{0.3457, 0.3585};
    return photo;
}

Strokes make_strokes(int w, int h) {
    auto field = std::make_shared<StrokeField>();
    field->aspect = prism::aspect_of(w, h);
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.05f, 0.2f}, {0.5f, 0.55f}, {0.95f, 0.4f}}, 0.12f, 0.8f, -1.2f}));
    field->strokes.push_back(
        std::make_shared<const FieldStroke>(FieldStroke{{{0.45f, 0.6f}}, 0.2f, 1.0f, 1.5f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.0f, 0.5f}, {1.0f, 0.52f}}, 0.04f, 0.0f, 0.7f}));
    field->strokes.push_back(std::make_shared<const FieldStroke>(
        FieldStroke{{{0.5f, 0.0f}, {0.48f, 1.0f}}, 0.03f, 0.0f, -0.6f}));
    return Strokes(std::move(field));
}

Frame turned_7() {
    Frame f;
    f.angle = 7.0f;
    f.left = 0.04f;
    f.top = 0.06f;
    f.right = 0.97f;
    f.bottom = 0.95f;
    f.aspect = prism::Aspect::free;
    return f;
}

EditSettings look(int w, int h, bool black_and_white) {
    EditSettings s;
    s.exposure_stops = 0.4f;
    s.strokes = make_strokes(w, h);
    s.white_balance = prism::WhiteBalance{5200.0, 8.0};
    s.saturation = black_and_white ? 0.0f : 1.3f;
    s.contrast = 1.15f;
    s.curve = prism::gui::CurveKind::filmic;
    s.frame = turned_7();
    return s;
}

// `part` of the w-wide 8-bit picture `whole`.
std::vector<std::uint8_t> crop8(const std::vector<std::uint8_t>& whole, int w,
                                const FrameBox& part) {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(part.width) * static_cast<std::size_t>(part.height) * 3);
    for (int y = part.y0; y < part.y0 + part.height; ++y) {
        const auto row =
            whole.begin() +
            static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                         static_cast<std::size_t>(part.x0)) *
                                        3);
        out.insert(out.end(), row, row + static_cast<std::ptrdiff_t>(part.width) * 3);
    }
    return out;
}

// The index of the first byte that differs, as a message; empty if none.
std::string first_byte_difference(const std::vector<std::uint8_t>& want,
                                  const std::vector<std::uint8_t>& got) {
    if (want.size() != got.size()) {
        return "size " + std::to_string(got.size()) + ", wanted " + std::to_string(want.size());
    }
    const auto at = std::mismatch(want.begin(), want.end(), got.begin());
    if (at.first == want.end()) {
        return {};
    }
    return "first difference at byte " + std::to_string(at.first - want.begin()) + ": got " +
           std::to_string(*at.second) + ", wanted " + std::to_string(*at.first);
}

// The renderer's frame for `region` at `output`, from `base`.
DetailFrame rendered(DetailRenderer& renderer, const std::shared_ptr<const Photo>& base,
                     const EditSettings& s, const FrameBox& region, PixelSize output) {
    DetailRenderRequest r;
    r.base = base;
    r.settings = s;
    r.request = {region, RegionBudget::live};
    r.output = output;
    r.key = {1, 1};
    if (renderer.submit(std::move(r), false) == 0 || !renderer.wait_until_idle(timeout)) {
        ADD_FAILURE() << "not rendered";
        return {};
    }
    std::optional<DetailFrame> frame = renderer.poll(1);
    if (!frame) {
        ADD_FAILURE() << "no frame";
        return {};
    }
    EXPECT_EQ(frame->error, "");
    EXPECT_EQ(frame->region, region);
    EXPECT_EQ(frame->size, output);
    return std::move(*frame);
}

// The view scale (points per preview pixel) showing `percent` of the file on a screen of
// `density`, with the preview at a quarter of the file.
constexpr FileScale quarter{4.0, 4.0, 4.0};
double view_at(double percent, double density) {
    return prism::gui::view_scale_for_file_percent(percent, density, quarter);
}

Image finished(Photo base, const EditSettings& s) {
    std::variant<Image, Error> out = prism::gui::finish_export(std::move(base), s, std::nullopt, 0);
    if (const Error* e = std::get_if<Error>(&out)) {
        ADD_FAILURE() << "finish_export failed: " << e->message;
        return {};
    }
    return std::move(std::get<Image>(out));
}

// At 100% and above the frame is export's encoded pixels of the region, bit for bit; below, it is
// the region shrunk to the output size and encoded. The frames are made first; then `base` is moved
// into finish_export, as export does, so a RAW decode is never copied.
void check_against_export(Photo base_photo, const EditSettings& s,
                          const std::vector<FrameBox>& regions) {
    struct Check {
        std::string what;
        FrameBox region;
        DetailFrame frame;
        std::vector<std::uint8_t> shrunk;  // below 100%: what the frame must be
    };
    std::vector<Check> checks;
    {
        const auto base = std::make_shared<Photo>(std::move(base_photo));
        DetailRenderer renderer(0);
        for (const FrameBox& region : regions) {
            const std::string where =
                "region " + std::to_string(region.x0) + ", " + std::to_string(region.y0) + " " +
                std::to_string(region.width) + " x " + std::to_string(region.height);
            for (const auto& [percent, density] :
                 std::vector<std::pair<double, double>>{{100.0, 1.0}, {100.0, 2.0}, {250.0, 2.0}}) {
                const PixelSize out = prism::gui::detail_output_size(
                    region, view_at(percent, density), density, quarter);
                EXPECT_EQ(out, (PixelSize{region.width, region.height}));
                checks.push_back({where + " at " + std::to_string(percent) + "%, density " +
                                      std::to_string(density),
                                  region,
                                  rendered(renderer, base, s, region, out),
                                  {}});
            }
            const PixelSize out =
                prism::gui::detail_output_size(region, view_at(60.0, 2.0), 2.0, quarter);
            EXPECT_LT(out.width, region.width);
            const Image shrunk = prism::downscale(prism::gui::render_region(*base, s, region, 0),
                                                  std::max(out.width, out.height), 0);
            checks.push_back({where + " at 60%", region, rendered(renderer, base, s, region, out),
                              prism::encode_srgb8(shrunk, 0)});
        }
        // The renderer has let go of the base: this is now its only reference.
        EXPECT_EQ(base.use_count(), 1);
        base_photo = std::move(*base);
    }
    const Image whole = finished(std::move(base_photo), s);
    const std::vector<std::uint8_t> whole8 = prism::encode_srgb8(whole, 0);
    for (const Check& c : checks) {
        SCOPED_TRACE(c.what);
        ASSERT_FALSE(c.frame.rgb8.empty());
        if (c.shrunk.empty()) {
            EXPECT_EQ(first_byte_difference(crop8(whole8, whole.width, c.region), c.frame.rgb8),
                      "");
        } else {
            EXPECT_EQ(first_byte_difference(c.shrunk, c.frame.rgb8), "");
        }
    }
}

TEST(DetailRenderer, AtAndAboveHundredPercentTheFrameIsExportsPixels) {
    const int w = 640;
    const int h = 430;
    for (const bool bw : {false, true}) {
        SCOPED_TRACE(bw ? "black and white" : "colour");
        const EditSettings s = look(w, h, bw);
        const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
        check_against_export(make_photo(w, h), s,
                             {{size.width / 4, size.height / 4, size.width / 2, size.height / 2},
                              {0, 0, 131, 97},
                              {size.width - 77, size.height - 151, 77, 151},
                              {0, 0, size.width, size.height}});
    }
}

TEST(DetailRenderer, OneAndEightThreadsGiveTheSameFrame) {
    const int w = 2000;
    const int h = 1400;
    const auto base = std::make_shared<const Photo>(make_photo(w, h));
    const EditSettings s = look(w, h, false);
    const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
    const FrameBox region{(size.width - 1501) / 2, (size.height - 1049) / 2, 1501, 1049};
    const std::size_t pixels =
        static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height);
    ASSERT_GE(region.x0, 0);
    ASSERT_GE(region.y0, 0);
    ASSERT_GE(pixels, prism::parallel_min_pixels * 3 / 2);
    ASSERT_EQ(prism::threads_for_pixels(pixels, 8), 8);

    DetailRenderer one(1);
    DetailRenderer eight(8);
    // 100%, and 80% so the shrink runs too: downscale's threads go by its input, the region, so
    // it runs on 8; its output is under 1 MP, so that pass's encode runs on one thread in both.
    for (const double percent : {100.0, 80.0}) {
        SCOPED_TRACE(std::to_string(percent) + "%");
        const PixelSize out =
            prism::gui::detail_output_size(region, view_at(percent, 1.0), 1.0, quarter);
        const DetailFrame a = rendered(one, base, s, region, out);
        const DetailFrame b = rendered(eight, base, s, region, out);
        ASSERT_FALSE(a.rgb8.empty());
        EXPECT_EQ(first_byte_difference(a.rgb8, b.rgb8), "");
    }
}

// One decode of $PRISM_TEST_RAW (~700 MB as float), framed 7 degrees with strokes.
TEST(DetailRenderer, RegionsOfTheRawFileAreExportsPixels) {
    const std::string raw = prism_test::env_var("PRISM_TEST_RAW");
    if (raw.empty()) {
        GTEST_SKIP() << "set PRISM_TEST_RAW to a RAW file to run this test";
    }
    if (!prism::raw_support_available()) {
        GTEST_SKIP() << "Prism was built without LibRaw";
    }
    std::variant<Photo, Error> decoded =
        prism::gui::decode_for_export(fs::path(raw), prism::gui::InputKind::raw, EditSettings{}, 0);
    if (const Error* e = std::get_if<Error>(&decoded)) {
        FAIL() << e->message << " " << e->details;
    }
    Photo& base = std::get<Photo>(decoded);
    const int w = base.image.width;
    const int h = base.image.height;
    const EditSettings s = look(w, h, true);
    const PixelSize size = prism::plan_export_frame(s.frame, w, h).size;
    check_against_export(
        std::move(base), s,
        {{size.width / 2 - 450, size.height / 2 - 300, 900, 600}, {size.width - 300, 0, 300, 200}});
}

}  // namespace

namespace {

TEST(DetailRenderer, AFrameSaysWhichFramedPictureItsRegionIsIn) {
    const auto base = std::make_shared<Photo>(make_photo(640, 430));
    DetailRenderer renderer(1);
    EditSettings whole;
    EditSettings turned;
    turned.frame = turned_7();
    const PixelSize turned_size = prism::plan_export_frame(turned.frame, 640, 430).size;
    ASSERT_NE(turned_size, (PixelSize{640, 430}));
    const FrameBox region{10, 20, 100, 60};
    EXPECT_EQ(rendered(renderer, base, whole, region, {100, 60}).picture, (PixelSize{640, 430}));
    EXPECT_EQ(rendered(renderer, base, turned, region, {100, 60}).picture, turned_size);
}

}  // namespace
