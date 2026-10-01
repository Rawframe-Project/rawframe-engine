#pragma once

// The UI's draw-command list on the device (ADR-0034's one render path,
// SPEC-0032, D375): a tree's boxes drawn over the frame's picture in their
// paint order, each a rounded box by its signed distance, its border
// inside it, smoothed over a pixel, inside its clip, premultiplied and
// blended over what is behind it in linear light, inside its clip and
// every clip that one is inside (D377); and its images in their place in
// the order, each its texture's part stretched or in nine slices (D378);
// its shadows, as CSS's box-shadow, blurred by a Gaussian (D381); and its
// boxes' gradients over their fills, moving through premultiplied Oklab
// (D382).

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"
#include "rawframe/ui/tree.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace rawframe::render_canvas_gpu {

/// The image a list names, decoded; none while it is not ready.
using ImageSource = std::function<std::shared_ptr<const texture::Texture>(std::uint64_t id)>;

struct UiStatistics {
    /// Frames the list was drawn in, and its boxes drawn in all.
    std::uint64_t frames = 0;
    std::uint64_t boxes = 0;
    /// Images drawn in all, and those left out while their texture was not
    /// ready or past the textures held (D378).
    std::uint64_t images = 0;
    std::uint64_t imagesWaiting = 0;
    /// Shadows drawn in all (D381).
    std::uint64_t shadows = 0;
};

class UiRenderer final : public render::FrameRecorder {
public:
    /// On `device`, which must be ready and must outlive it.
    [[nodiscard]] static result::Result<std::unique_ptr<UiRenderer>> create(render::Device& device);

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    ~UiRenderer() override;

    /// What the next frame draws: `list`, from the picture's top left, its
    /// logical pixels `list.scale` of the picture's each, its images from
    /// `images`, uploaded once and held; nothing for none. It is read until
    /// the frame is made.
    void prepare(const ui::DrawList* list, ImageSource images = {}) noexcept;

    [[nodiscard]] result::Status declare(render::Frame& frame) override;
    [[nodiscard]] result::Status record(render::Frame& frame) override;
    void ended(bool submitted) noexcept override;

    [[nodiscard]] const UiStatistics& statistics() const noexcept;

    struct State;

private:
    explicit UiRenderer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_canvas_gpu
