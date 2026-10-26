#pragma once

// The layer an entity is put on: a number
// the authored scene holds as an ordinary component, so a scene's
// entities are grouped where an editor shows, hides, locks, and chooses
// them together, and a program may read it as any other value. A game
// names its layers with a `field rawframe.world.layer layer labels ...`
// line; nought is no layer.

#include "rawframe/schema/component.h"

#include <cstdint>
#include <string_view>

namespace rawframe::world {

struct Layer {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("a0617a59-11cd-4f92-a12f-ead27bae2974");
    static constexpr std::string_view kComponentName = "rawframe.world.layer";

    std::uint32_t layer = 0;
};

} // namespace rawframe::world
