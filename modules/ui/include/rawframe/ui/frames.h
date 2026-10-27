#pragma once

// The UI's frames as a device draws them: what a UI drew in this
// Host iteration's `presentation_extract` (a game's, mirrored by
// world_ui, or a tool's own), and the images it names, for the
// device's half to record over the scene and the canvas in its `present`.

#include "rawframe/composition/participant.h"
#include "rawframe/texture/texture.h"
#include "rawframe/ui/tree.h"

#include <cstdint>
#include <memory>
#include <span>

namespace rawframe::ui {

class UiFrames {
public:
    UiFrames() = default;
    UiFrames(const UiFrames&) = delete;
    UiFrames& operator=(const UiFrames&) = delete;
    virtual ~UiFrames() = default;

    /// What the UI drew in this Host iteration, until the next one's
    /// `presentation_extract`; none in an iteration that drew none.
    [[nodiscard]] virtual const DrawList* drawn() const noexcept = 0;
    /// The frame's size in device pixels from the next one on, as the
    /// window it is shown in has it: the views' regions follow. Sides of
    /// nought are ignored. On a headset's `panel`, the UI's own
    /// surface, a pixel is a logical pixel, and no window's safe area
    /// applies.
    virtual void resize(std::uint32_t width, std::uint32_t height, bool panel) noexcept = 0;
    /// The image the list names `id`, decoded and held, for the list drawn;
    /// none while it is not ready, and for one the UI does not declare.
    [[nodiscard]] virtual std::shared_ptr<const texture::Texture> image(std::uint64_t id) const = 0;
    /// The windows beside the one shown where an editor shows a panel apart:
    /// each one (`window::WindowId`'s value) and what the UI drew
    /// for it in this iteration. A UI with none has none.
    struct Aside {
        std::uint64_t window = 0;
        const DrawList* drawn = nullptr;
    };
    [[nodiscard]] virtual std::span<const Aside> asides() const noexcept {
        return {};
    }
    /// An aside's size in device pixels from the next iteration on, as
    /// `resize` is the window's.
    virtual void resizeAside(std::uint64_t /*window*/, std::uint32_t /*width*/, std::uint32_t /*height*/) noexcept {
    }
};

inline constexpr composition::Capability<UiFrames> kUiFrames{"rawframe.ui.frames"};

} // namespace rawframe::ui
