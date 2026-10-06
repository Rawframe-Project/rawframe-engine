#pragma once

// What a client's UI shows under the mouse, for the game's present systems
// to light through `ui.hovered()` (D422). The client's UI provides it
// (`rawframe.world_ui`, from what its host lends of the pointer); the
// presentation reads it each frame. A dedicated server has none, and
// answers nought.

#include "rawframe/composition/participant.h"

#include <cstdint>

namespace rawframe::world_kest {

class UiHover {
public:
    UiHover() = default;
    UiHover(const UiHover&) = delete;
    UiHover& operator=(const UiHover&) = delete;
    virtual ~UiHover() = default;

    /// The press code of the UI node under the mouse, where a press would
    /// land; nought for none.
    [[nodiscard]] virtual std::int64_t hovered() const = 0;
};

inline constexpr composition::Capability<UiHover> kUiHover{"rawframe.world_kest.ui_hover"};

} // namespace rawframe::world_kest
