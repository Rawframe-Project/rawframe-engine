#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/network/admission.h"
#include "rawframe/schema/component.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/server.h"

#include <optional>
#include <span>

namespace rawframe::world_replication {

/// What a game replicates, as the participant that loaded the game says:
/// every component (so a client World can hold the same registry), the
/// replication table, what a player starts with, where its input goes, and
/// the game's exact identity for admission.
class ReplicationPlan {
public:
    ReplicationPlan() = default;
    ReplicationPlan(const ReplicationPlan&) = delete;
    ReplicationPlan& operator=(const ReplicationPlan&) = delete;
    virtual ~ReplicationPlan() = default;

    [[nodiscard]] virtual std::span<const schema::ComponentDescriptor> components() const noexcept = 0;
    [[nodiscard]] virtual const ReplicationTable& table() const noexcept = 0;
    [[nodiscard]] virtual std::span<const schema::ComponentTypeId> playerComponents() const noexcept = 0;
    [[nodiscard]] virtual const std::optional<ComponentCodec>& input() const noexcept = 0;
    [[nodiscard]] virtual network::Fingerprint game() const noexcept = 0;
};

inline constexpr composition::Capability<ReplicationPlan> kReplicationPlan{"rawframe.replication.plan"};

/// The fingerprint of a replication table: every component's identity, size,
/// and fields in order. Both sides must replicate exactly the same way.
[[nodiscard]] network::Fingerprint tableFingerprint(const ReplicationTable& table) noexcept;

} // namespace rawframe::world_replication
