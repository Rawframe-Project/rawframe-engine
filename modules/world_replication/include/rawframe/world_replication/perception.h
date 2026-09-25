#pragma once

// What moment a player saw (SPEC-0041's PerceptionContext), where the
// World's systems can read it. For a game whose input is perceived, the
// server writes it into the player's entity with each command it applies:
// the server tick of the older of the two states the client showed between,
// and 65536ths of the way to the next. A held command keeps its moment; a
// neutral one has none (tick nought). A lag-compensated query takes it as
// is and clamps it to what it keeps.

#include "rawframe/schema/stable_id.h"

#include <cstdint>
#include <string_view>

namespace rawframe::world_replication {

struct Perception {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("28ddb67b-c4b0-4d71-ac24-6379b899e29f");
    static constexpr std::string_view kComponentName = "rawframe.replication.perception";

    std::uint64_t baseTick = 0;
    std::uint16_t fraction = 0;
};

} // namespace rawframe::world_replication
