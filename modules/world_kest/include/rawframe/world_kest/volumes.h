#pragma once

// The player's volumes, by the role of the mixer's bus each binds to, as
// a client's present systems set them with `sound.volume(role, decibels)`:
// in decibels on top of each
// bus's own, the roles in `audio::Role`'s order, nought for none. The
// presentation provides it; what plays the client's sound reads it each
// frame. A dedicated server has none.

#include "rawframe/composition/participant.h"

#include <array>
#include <cstdint>

namespace rawframe::world_kest {

struct PresentedVolumes {
    std::array<float, 7> decibels{};
    /// Moved on by each change, so a reader applies only what changed.
    std::uint64_t revision = 0;
};

inline constexpr composition::Capability<PresentedVolumes> kPresentedVolumes{"rawframe.world_kest.presented_volumes"};

} // namespace rawframe::world_kest
