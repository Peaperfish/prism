#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "prism/gui/preview_renderer.hpp"
#include "prism/gui/settings.hpp"
#include "prism/image.hpp"

namespace prism::gui {

// Keeps the preview in step with the Session, for a UI that redraws every frame:
//
//   every frame:  preview.update(session.preview_base(), session.settings());
//                 if (auto frame = preview.poll()) upload(*frame);
//                 if (preview.rendering()) show "Rendering…";
//
// update() submits a render only when the base image or the settings differ from the last
// request, so calling it every frame costs a comparison, and a slider drag sends one request per
// frame in which the value moved (PreviewRenderer's latest-wins keeps that from queueing up).
// Not thread-safe: it belongs to the UI thread, like the Session.
class LivePreview {
public:
    // `threads` as for the pipeline: 0 = one per hardware thread.
    explicit LivePreview(int threads = 0);
    // With PreviewRenderer's injectable render function, for tests.
    explicit LivePreview(PreviewRenderer::RenderFunction render);

    // Submits a render if `base` or `settings` changed since the last one. A null base (no file
    // open) submits nothing and forgets the last request, so the next file always renders.
    void update(const std::shared_ptr<const Image>& base, const EditSettings& settings);

    // The newest finished frame since the last poll, if any (see PreviewRenderer::poll).
    [[nodiscard]] std::optional<PreviewFrame> poll();

    // True from a submit until poll() has returned the frame of the latest request: the preview
    // on screen is not yet the current settings. A superseded request never produces a frame, but
    // the latest one always does (with `error` set if rendering failed), so this always ends.
    [[nodiscard]] bool rendering() const noexcept { return shown_ < submitted_; }

private:
    PreviewRenderer renderer_;
    std::shared_ptr<const Image> base_;
    EditSettings settings_;
    std::uint64_t submitted_ = 0;  // generation of the latest request
    std::uint64_t shown_ = 0;      // generation of the latest frame poll() returned
};

}  // namespace prism::gui
