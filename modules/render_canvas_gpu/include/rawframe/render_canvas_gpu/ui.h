#pragma once

// The UI's draw-command list on the device (ADR-0034's one render path,
// SPEC-0032, D375): a tree's boxes drawn over the frame's picture in their
// paint order, each a rounded box by its signed distance, its border
// inside it, smoothed over a pixel, inside its clip, premultiplied and
// blended over what is behind it in linear light. A box's clip is its
// innermost one; its ancestors are not intersected yet.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/result/result.h"
#include "rawframe/ui/tree.h"

#include <cstdint>
#include <memory>

namespace rawframe::render_canvas_gpu {

struct UiStatistics {
    /// Frames the list was drawn in, and its boxes drawn in all.
    std::uint64_t frames = 0;
    std::uint64_t boxes = 0;
};

class UiRenderer final : public render::FrameRecorder {
public:
    /// On `device`, which must be ready and must outlive it.
    [[nodiscard]] static result::Result<std::unique_ptr<UiRenderer>> create(render::Device& device);

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    ~UiRenderer() override;

    /// What the next frame draws: `list`, in the picture's pixels from its
    /// top left; nothing for none. It is read until the frame is made.
    void prepare(const ui::DrawList* list) noexcept;

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
