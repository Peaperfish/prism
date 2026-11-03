#include "prism/gui/live_preview.hpp"

#include <utility>

namespace prism::gui {

LivePreview::LivePreview(int threads) : renderer_(threads) {}

LivePreview::LivePreview(PreviewRenderer::RenderFunction render) : renderer_(std::move(render)) {}

LivePreview::LivePreview(PreviewRenderer::StageRenderFunction render)
    : renderer_(std::move(render)) {}

LivePreview::LivePreview(PreviewRenderer::ScopeRenderFunction render)
    : renderer_(std::move(render)) {}

LivePreview::LivePreview(PreviewRenderer::OptionsRenderFunction render)
    : renderer_(std::move(render)) {}

void LivePreview::update(const std::shared_ptr<const Image>& base, const EditSettings& settings,
                         StageId stop_after, PreviewOptions options) {
    if (!base) {
        base_.reset();
        shown_ = submitted_;  // nothing to wait for: a frame still in flight is just ignored
        return;
    }
    // Turning an extra on needs a new frame; turning one off does not (the frame is still right).
    // The viewed stage as compare is no comparison: the same request as with none.
    if (options.compare_stage == stop_after) {
        options.compare_stage.reset();
    }
    // A strip of the Decode stage is no strip. While a strip is on there is no clip report and no
    // comparison (the render ignores them), so they must not count as different requests.
    if (stop_after == StageId::decode) {
        options.test_strip.reset();
    }
    if (options.test_strip) {
        options.clip_report = false;
        options.compare_stage.reset();
    }
    // A compare stage is wanted exactly: another stage than the frame has needs a new render.
    const bool covered = (options_.scopes || !options.scopes) &&
                         (options_.clip_report || !options.clip_report) &&
                         (options_.clip_mask || !options.clip_mask) &&
                         (!options.compare_stage || options.compare_stage == options_.compare_stage) &&
                         options.test_strip == options_.test_strip;  // exactly: on, off or step
    if (base == base_ && settings == settings_ && stop_after == stop_after_ && covered) {
        // Hidden: the next time they show, ask again.
        options_.scopes = options_.scopes && options.scopes;
        options_.clip_report = options_.clip_report && options.clip_report;
        options_.clip_mask = options_.clip_mask && options.clip_mask;
        if (!options.compare_stage) {
            options_.compare_stage.reset();
        }
        return;
    }
    const bool same_view = base == base_ && stop_after == stop_after_;
    base_ = base;
    settings_ = settings;
    stop_after_ = stop_after;
    options_ = options;
    submitted_ = renderer_.submit(base_, settings_, stop_after_, options);
    if (!same_view) {
        epoch_ = submitted_;  // frames of earlier requests show another image or stage
    }
}

std::optional<PreviewFrame> LivePreview::poll() {
    std::optional<PreviewFrame> frame = renderer_.poll();
    if (!frame) {
        return std::nullopt;
    }
    // A frame from before update(nullptr) (the file was closed), from before the base or the viewed
    // stage changed, or not newer than the one already shown. One merely older than the latest
    // request is fine.
    if (!base_ || frame->generation < epoch_ || frame->generation <= shown_) {
        return std::nullopt;
    }
    shown_ = frame->generation;
    return frame;
}

}  // namespace prism::gui
