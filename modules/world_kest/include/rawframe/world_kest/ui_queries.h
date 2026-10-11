#pragma once

// What a client's UI answers its present systems (D422, D621): the node
// under the mouse, for a present system to light through `ui.hovered()`,
// and the value a range holds, which the player may have moved, through
// `ui.value(press)`. The client's UI provides it (`rawframe.world_ui`,
// from what its host lends of the pointer); the presentation reads it
// each frame. A dedicated server has none, and answers nought.

#include "rawframe/composition/participant.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace rawframe::world_kest {

class UiQueries {
public:
    UiQueries() = default;
    UiQueries(const UiQueries&) = delete;
    UiQueries& operator=(const UiQueries&) = delete;
    virtual ~UiQueries() = default;

    /// The press code of the UI node under the mouse, where a press would
    /// land; nought for none.
    [[nodiscard]] virtual std::int64_t hovered() const = 0;
    /// The value of the range whose press code is `press` in local player
    /// `player`'s view; none for no such range.
    [[nodiscard]] virtual std::optional<float> valueOf(std::size_t player, std::int64_t press) const = 0;
};

inline constexpr composition::Capability<UiQueries> kUiQueries{"rawframe.world_kest.ui_queries"};

} // namespace rawframe::world_kest
