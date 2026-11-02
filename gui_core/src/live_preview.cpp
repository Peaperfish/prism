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
    const bool covered = (options_.scopes || !options.scopes) &&
                         (options_.clip_report || !options.clip_report) &&
                         (options_.clip_mask || !options.clip_mask);
    if (base == base_ && settings == settings_ && stop_after == stop_after_ && covered) {
        // Hidden: the next time they show, ask again.
        options_.scopes = options_.scopes && options.scopes;
        options_.clip_report = options_.clip_report && options.clip_report;
        options_.clip_mask = options_.clip_mask && options.clip_mask;
        return;
    }
    base_ = base;
    settings_ = settings;
    stop_after_ = stop_after;
    options_ = options;
    submitted_ = renderer_.submit(base_, settings_, stop_after_, options);
}

std::optional<PreviewFrame> LivePreview::poll() {
    std::optional<PreviewFrame> frame = renderer_.poll();
    if (!frame) {
        return std::nullopt;
    }
    // A frame from before update(nullptr) (the file was closed) or older than one already shown.
    if (!base_ || frame->generation <= shown_) {
        return std::nullopt;
    }
    shown_ = frame->generation;
    return frame;
}

}  // namespace prism::gui
