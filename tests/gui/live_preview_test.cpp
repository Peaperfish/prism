#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>

#include "prism/gui/live_preview.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

using namespace std::chrono_literals;
using prism::gui::EditSettings;
using prism::gui::LivePreview;
using prism::gui::PreviewFrame;

// As in preview_renderer_test.cpp: no sleeps; the test decides the order of events with a Gate,
// and the timeouts only matter if something is broken. Runs under ThreadSanitizer in CI.

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

std::shared_ptr<const prism::Image> make_base() {
    auto img = std::make_shared<prism::Image>();
    img->width = 2;
    img->height = 1;
    img->pixels = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    return img;
}

EditSettings with_exposure(float stops) {
    EditSettings s;
    s.exposure_stops = stops;
    return s;
}

// A render function that counts its calls and tags each frame with the request's exposure.
struct CountingRender {
    std::atomic<int> calls{0};

    prism::gui::PreviewRenderer::RenderFunction function() {
        return [this](const prism::Image&, const EditSettings& s, std::stop_token) {
            ++calls;
            PreviewFrame f;
            f.width = 1;
            f.height = 1;
            f.rgb8 = {static_cast<std::uint8_t>(s.exposure_stops), 0, 0};
            return std::optional<PreviewFrame>(f);
        };
    }
};

// Polls until a frame arrives (the worker is quick, but on another thread).
std::optional<PreviewFrame> next_frame(LivePreview& preview) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::optional<PreviewFrame> f = preview.poll()) {
            return f;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

}  // namespace

TEST(LivePreview, RendersOnceForUnchangedInput) {
    CountingRender render;
    LivePreview preview(render.function());
    EXPECT_FALSE(preview.rendering());

    const auto base = make_base();
    preview.update(base, with_exposure(1));
    EXPECT_TRUE(preview.rendering());
    for (int frame = 0; frame < 10; ++frame) {
        preview.update(base, with_exposure(1));  // every UI frame, nothing changed
    }
    const std::optional<PreviewFrame> f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->rgb8[0], 1);
    EXPECT_FALSE(preview.rendering());
    EXPECT_EQ(render.calls.load(), 1);
}

TEST(LivePreview, ChangedSettingsOrBaseRenderAgain) {
    CountingRender render;
    LivePreview preview(render.function());
    const auto base = make_base();

    preview.update(base, with_exposure(1));
    ASSERT_TRUE(next_frame(preview));
    preview.update(base, with_exposure(2));
    EXPECT_TRUE(preview.rendering());
    std::optional<PreviewFrame> f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->rgb8[0], 2);

    preview.update(make_base(), with_exposure(2));  // another file, same settings
    f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(render.calls.load(), 3);
    EXPECT_FALSE(preview.rendering());
}

TEST(LivePreview, RenderingStaysTrueUntilTheLatestFrameArrives) {
    // The first render waits until the test releases it; a second request arrives meanwhile.
    Gate first_started;
    Gate release;
    std::atomic<int> calls{0};
    LivePreview preview([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        if (calls++ == 0) {
            first_started.open();
            release.wait();
        }
        PreviewFrame f;
        f.rgb8 = {static_cast<std::uint8_t>(s.exposure_stops)};
        return std::optional<PreviewFrame>(f);
    });
    const auto base = make_base();
    preview.update(base, with_exposure(1));
    ASSERT_TRUE(first_started.wait());
    preview.update(base, with_exposure(2));
    EXPECT_TRUE(preview.rendering());
    release.open();

    const std::optional<PreviewFrame> f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->rgb8[0], 2) << "only the latest request's frame is shown";
    EXPECT_FALSE(preview.rendering());
}

TEST(LivePreview, AnErrorFrameStillEndsRendering) {
    LivePreview preview([](const prism::Image&, const EditSettings&,
                           std::stop_token) -> std::optional<PreviewFrame> {
        throw std::runtime_error("broken");
    });
    preview.update(make_base(), with_exposure(1));
    const std::optional<PreviewFrame> f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->error, "broken");
    EXPECT_FALSE(preview.rendering());
}

TEST(LivePreview, ClosingTheFileDropsAFrameInFlight) {
    Gate started;
    Gate release;
    std::atomic<int> calls{0};
    LivePreview preview([&](const prism::Image&, const EditSettings& s, std::stop_token) {
        if (calls++ == 0) {
            started.open();
            release.wait();
        }
        PreviewFrame f;
        f.rgb8 = {static_cast<std::uint8_t>(s.exposure_stops)};
        return std::optional<PreviewFrame>(f);
    });
    preview.update(make_base(), with_exposure(1));
    ASSERT_TRUE(started.wait());
    preview.update(nullptr, with_exposure(1));  // no file any more
    EXPECT_FALSE(preview.rendering());
    release.open();

    // The next file renders even with the same settings, and the stale frame never shows.
    preview.update(make_base(), with_exposure(1));
    EXPECT_TRUE(preview.rendering());
    const std::optional<PreviewFrame> f = next_frame(preview);
    ASSERT_TRUE(f);
    EXPECT_EQ(f->generation, 2u);
    EXPECT_FALSE(preview.rendering());
}

TEST(LivePreview, ChangingViewedStageTriggersANewRender) {
    using prism::gui::StageId;

    LivePreview preview(1);
    const auto base = make_base();
    const EditSettings s = with_exposure(1.5f);

    preview.update(base, s, StageId::encode);
    const std::optional<PreviewFrame> final_frame = next_frame(preview);
    ASSERT_TRUE(final_frame);
    EXPECT_EQ(final_frame->stage, StageId::encode);

    preview.update(base, s, StageId::decode);
    EXPECT_TRUE(preview.rendering());
    const std::optional<PreviewFrame> decode_frame = next_frame(preview);
    ASSERT_TRUE(decode_frame);
    EXPECT_EQ(decode_frame->stage, StageId::decode);
    EXPECT_NE(decode_frame->rgb8, final_frame->rgb8);
}

TEST(LivePreview, ShowingScopesRendersOnceMoreAndHidingThemDoesNot) {
    std::atomic<int> calls{0};
    std::atomic<bool> last_scopes{false};
    LivePreview preview(prism::gui::PreviewRenderer::ScopeRenderFunction(
        [&](const prism::Image&, const EditSettings&, prism::gui::StageId, bool scopes,
            std::stop_token) {
            ++calls;
            last_scopes = scopes;
            PreviewFrame f;
            f.width = 1;
            f.height = 1;
            f.rgb8 = {0, 0, 0};
            return std::optional<PreviewFrame>(f);
        }));
    const auto base = make_base();
    const EditSettings s = with_exposure(1);

    preview.update(base, s, prism::gui::final_stage, false);
    ASSERT_TRUE(next_frame(preview));
    EXPECT_EQ(calls.load(), 1);
    EXPECT_FALSE(last_scopes.load());

    preview.update(base, s, prism::gui::final_stage, true);  // scopes turned on
    EXPECT_TRUE(preview.rendering());
    ASSERT_TRUE(next_frame(preview));
    EXPECT_EQ(calls.load(), 2);
    EXPECT_TRUE(last_scopes.load());

    preview.update(base, s, prism::gui::final_stage, true);  // still on: nothing new
    preview.update(base, s, prism::gui::final_stage, false);  // hidden: the picture is still right
    EXPECT_FALSE(preview.rendering());
    EXPECT_EQ(calls.load(), 2);

    preview.update(base, s, prism::gui::final_stage, true);  // shown again: asks again
    ASSERT_TRUE(next_frame(preview));
    EXPECT_EQ(calls.load(), 3);
}

TEST(LivePreview, ComparingRendersOnceMoreAndEachNewCompareStageRendersAgain) {
    using prism::gui::StageId;
    LivePreview preview(1);
    const auto base = make_base();
    const EditSettings s = with_exposure(1.5f);
    prism::gui::PreviewOptions options;

    preview.update(base, s, StageId::encode, options);
    const auto plain = next_frame(preview);
    ASSERT_TRUE(plain);
    EXPECT_FALSE(plain->compare);

    options.compare_stage = StageId::decode;  // compare turned on: a new render with the picture
    preview.update(base, s, StageId::encode, options);
    EXPECT_TRUE(preview.rendering());
    const auto with_original = next_frame(preview);
    ASSERT_TRUE(with_original && with_original->compare);
    EXPECT_EQ(with_original->compare->stage, StageId::decode);
    EXPECT_EQ(with_original->rgb8, plain->rgb8);

    preview.update(base, s, StageId::encode, options);  // unchanged: nothing
    EXPECT_FALSE(preview.rendering());

    options.compare_stage = StageId::exposure;  // another compare stage
    preview.update(base, s, StageId::encode, options);
    EXPECT_TRUE(preview.rendering());
    const auto with_exposure_stage = next_frame(preview);
    ASSERT_TRUE(with_exposure_stage && with_exposure_stage->compare);
    EXPECT_EQ(with_exposure_stage->compare->stage, StageId::exposure);

    options.compare_stage.reset();  // off: the frame on screen is still right
    preview.update(base, s, StageId::encode, options);
    EXPECT_FALSE(preview.rendering());

    options.compare_stage = StageId::exposure;  // on again: asks again
    preview.update(base, s, StageId::encode, options);
    EXPECT_TRUE(preview.rendering());
    EXPECT_TRUE(next_frame(preview));
}

TEST(LivePreview, TheViewedStageAsCompareStageIsTheSameAsOff) {
    using prism::gui::StageId;
    LivePreview preview(1);
    const auto base = make_base();
    const EditSettings s = with_exposure(1.5f);
    prism::gui::PreviewOptions options;

    preview.update(base, s, StageId::exposure, options);
    ASSERT_TRUE(next_frame(preview));
    options.compare_stage = StageId::exposure;
    preview.update(base, s, StageId::exposure, options);
    EXPECT_FALSE(preview.rendering());  // nothing to ask for

    options.compare_stage = StageId::decode;
    preview.update(base, s, StageId::exposure, options);
    ASSERT_TRUE(next_frame(preview));
    // Viewing the compare stage moves the viewed stage there: that is a new render anyway.
    preview.update(base, s, StageId::decode, options);
    const auto frame = next_frame(preview);
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->stage, StageId::decode);
    EXPECT_FALSE(frame->compare);
}
