#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/network/admission.h"
#include "rawframe/schema/component.h"
#include "rawframe/world_replication/codec.h"
#include "rawframe/world_replication/perception.h"
#include "rawframe/world_replication/prediction.h"
#include "rawframe/world_replication/server.h"

#include <memory>
#include <optional>
#include <span>

namespace rawframe::world_replication {

/// What a game replicates, as the participant that loaded the game says:
/// every component (so a client World can hold the same registry), the
/// replication table, what a player starts with, where its input goes, its
/// interest, and the game's exact identity for admission.
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
    /// The player's components a client predicts; empty for a game that
    /// predicts nothing.
    [[nodiscard]] virtual std::span<const schema::ComponentTypeId> predictedComponents() const noexcept = 0;
    /// Components of other entities a predicting client steps its player
    /// among (D39); empty for none.
    [[nodiscard]] virtual std::span<const schema::ComponentTypeId> nearbyComponents() const noexcept = 0;
    /// A predictor for one client, running the game's predicted systems;
    /// `unsupported` for a game that predicts nothing.
    [[nodiscard]] virtual result::Result<std::unique_ptr<Predictor>> predictor() const = 0;
    /// The components a client shows remote entities' values of between
    /// states; empty for a game that shows every state as it arrives.
    [[nodiscard]] virtual std::span<const schema::ComponentTypeId> interpolatedComponents() const noexcept = 0;
    /// Whether each command carries the moment its client saw.
    [[nodiscard]] virtual bool perceivedInput() const noexcept = 0;
    /// Who is sent what: none for a game whose every entity every
    /// connection sees.
    [[nodiscard]] virtual const std::optional<InterestSettings>& interest() const noexcept = 0;
    /// What the game's compensated queries gate by, from the server made
    /// for it until that server goes (null).
    virtual void attach(const InterestHistory* history) noexcept = 0;
};

inline constexpr composition::Capability<ReplicationPlan> kReplicationPlan{"rawframe.replication.plan"};

/// The fingerprint of a replication table: every component's identity, size,
/// and fields in order. Both sides must replicate exactly the same way.
[[nodiscard]] network::Fingerprint tableFingerprint(const ReplicationTable& table) noexcept;

} // namespace rawframe::world_replication
