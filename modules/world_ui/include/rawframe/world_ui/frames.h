#pragma once

// The UI's frames as a device draws them (D376): what the UI participant
// drew in this Host iteration's `presentation_extract`, for the device's
// half to record over the scene and the canvas in its `present`.

#include "rawframe/composition/participant.h"
#include "rawframe/ui/tree.h"

#include <cstdint>

namespace rawframe::world_ui {

class UiFrames {
public:
    UiFrames() = default;
    UiFrames(const UiFrames&) = delete;
    UiFrames& operator=(const UiFrames&) = delete;
    virtual ~UiFrames() = default;

    /// What the UI drew in this Host iteration, until the next one's
    /// `presentation_extract`; none in an iteration that drew none.
    [[nodiscard]] virtual const ui::DrawList* drawn() const noexcept = 0;
    /// The frame's size in device pixels from the next one on, as the
    /// window it is shown in has it: the views' regions follow. Sides of
    /// nought are ignored.
    virtual void resize(std::uint32_t width, std::uint32_t height) noexcept = 0;
};

inline constexpr composition::Capability<UiFrames> kUiFrames{"rawframe.world_ui.frames"};

} // namespace rawframe::world_ui
