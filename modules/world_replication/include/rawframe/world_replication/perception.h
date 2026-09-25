#pragma once

// What moment a player saw (SPEC-0041's PerceptionContext), where the
// World's systems can read it. For a game whose input is perceived, the
// server writes it into the player's entity with each command it applies:
// the server tick of the older of the two states the client showed between,
// and 65536ths of the way to the next. A held command keeps its moment; a
// neutral one has none (tick nought). The server clamps each claim to within
// a skew of the lag it measures for the connection, and names the connection
// seeing, so a lag-compensated query rewinds only what that connection was
// sent (SPEC-0041's victim gate).

#include "rawframe/schema/stable_id.h"
#include "rawframe/world/entity.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::world_replication {

struct Perception {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("28ddb67b-c4b0-4d71-ac24-6379b899e29f");
    static constexpr std::string_view kComponentName = "rawframe.replication.perception";

    std::uint64_t baseTick = 0;
    std::uint16_t fraction = 0;
    /// The connection that saw, as InterestHistory knows it; nought for a
    /// moment no connection claimed, which a query rewinds everything for.
    std::uint32_t viewer = 0;
};

/// Which entities each connection was sent, back in time: the victim gate's
/// record. Kept for 1024 ticks, the most a physics history holds.
class InterestHistory {
public:
    InterestHistory() = default;
    InterestHistory(const InterestHistory&) = delete;
    InterestHistory& operator=(const InterestHistory&) = delete;
    virtual ~InterestHistory() = default;

    /// The tick the connection `viewer` was first sent `entity`'s state, of
    /// the mapping it has now or of the one it had at tick `tick`; none if
    /// neither. Its own player, tick nought. A mapping first sent after
    /// `tick` still counts: until two states arrive, a client shows the
    /// first as it is.
    [[nodiscard]] virtual std::optional<std::uint64_t>
    sentSince(std::uint32_t viewer, world::EntityHandle entity, std::uint64_t tick) const noexcept = 0;
};

} // namespace rawframe::world_replication
