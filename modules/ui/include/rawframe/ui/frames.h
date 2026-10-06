#pragma once

// The UI's frames as a device draws them (D376): what a UI drew in this
// Host iteration's `presentation_extract` (a game's, mirrored by
// world_ui, or a tool's own), and the images it names (D378), for the
// device's half to record over the scene and the canvas in its `present`.

#include "rawframe/composition/participant.h"
#include "rawframe/texture/texture.h"
#include "rawframe/ui/tree.h"

#include <cstdint>
#include <memory>

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
    /// nought are ignored.
    virtual void resize(std::uint32_t width, std::uint32_t height) noexcept = 0;
    /// The image the list names `id`, decoded and held, for the list drawn;
    /// none while it is not ready, and for one the UI does not declare.
    [[nodiscard]] virtual std::shared_ptr<const texture::Texture> image(std::uint64_t id) const = 0;
};

inline constexpr composition::Capability<UiFrames> kUiFrames{"rawframe.ui.frames"};

} // namespace rawframe::ui
