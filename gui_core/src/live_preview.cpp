#include "prism/gui/live_preview.hpp"

#include <utility>

namespace prism::gui {

LivePreview::LivePreview(int threads) : renderer_(threads) {}

LivePreview::LivePreview(PreviewRenderer::RenderFunction render) : renderer_(std::move(render)) {}

LivePreview::LivePreview(PreviewRenderer::StageRenderFunction render)
    : renderer_(std::move(render)) {}

LivePreview::LivePreview(PreviewRenderer::ScopeRenderFunction render)
    : renderer_(std::move(render)) {}

void LivePreview::update(const std::shared_ptr<const Image>& base, const EditSettings& settings,
                         StageId stop_after, bool scopes) {
    if (!base) {
        base_.reset();
        shown_ = submitted_;  // nothing to wait for: a frame still in flight is just ignored
        return;
    }
    if (base == base_ && settings == settings_ && stop_after == stop_after_ &&
        (scopes_ || !scopes)) {
        scopes_ = scopes_ && scopes;  // hidden: the next time they show, ask again
        return;
    }
    base_ = base;
    settings_ = settings;
    stop_after_ = stop_after;
    scopes_ = scopes;
    submitted_ = renderer_.submit(base_, settings_, stop_after_, scopes);
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
