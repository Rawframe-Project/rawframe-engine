#pragma once

// Snapshot interpolation (ADR-0054, SPEC-0041): a client shows remote
// entities as they were a little in the past, between two states it has
// received, rather than jumping to each state as it arrives. The moment shown
// is a fixed number of ticks behind the newest tick the server is estimated
// to have run; the estimate follows the least delayed state datagrams of the
// last two seconds, so jitter moves it little.

#include "rawframe/execution/time.h"
#include "rawframe/schema/stable_id.h"

#include <cstdint>
#include <vector>

namespace rawframe::world_replication {

struct InterpolationSettings {
    /// Components interpolated on every entity but the client's own. Their
    /// floating-point fields are interpolated; every other field shows the
    /// earlier state's value.
    std::vector<schema::ComponentTypeId> interpolated;
    /// Where "now" comes from; it must outlive the client.
    const execution::MonotonicSource* clock = nullptr;
    /// Ticks the moment shown stays behind the server's estimated tick.
    std::uint32_t delay = 6;
    /// Two states further apart than this are a jump, not movement: the
    /// earlier is shown until this many ticks before the later, and only
    /// then do the two blend.
    std::uint32_t maximumSpan = 12;
    /// States kept per entity and component.
    std::uint32_t samples = 32;
};

struct InterpolationStatistics {
    /// Values shown between two states.
    std::uint64_t blended = 0;
    /// Values shown as the newest state, the moment being past it.
    std::uint64_t newest = 0;
};

} // namespace rawframe::world_replication
