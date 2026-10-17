#pragma once

// What a replication server has sent and taken so far (D807), for a tool
// to watch while a game plays: its totals and the connections it holds,
// each counted alone too (D840), read on the Host thread between
// iterations.

#include "rawframe/composition/participant.h"
#include "rawframe/world_replication/server.h"

#include <cstddef>
#include <vector>

namespace rawframe::world_replication {

class ReplicationTraffic {
public:
    ReplicationTraffic() = default;
    ReplicationTraffic(const ReplicationTraffic&) = delete;
    ReplicationTraffic& operator=(const ReplicationTraffic&) = delete;
    virtual ~ReplicationTraffic() = default;

    /// Monotonic totals since the server started.
    [[nodiscard]] virtual ServerReplicationStatistics totals() const noexcept = 0;
    /// Connections open now, connecting, admitted, or draining.
    [[nodiscard]] virtual std::size_t connections() const noexcept = 0;
    /// Each admitted connection's replication so far (D840).
    [[nodiscard]] virtual std::vector<ConnectionTraffic> perConnection() const = 0;
};

inline constexpr composition::Capability<ReplicationTraffic> kReplicationTraffic{"rawframe.replication.traffic"};

} // namespace rawframe::world_replication
