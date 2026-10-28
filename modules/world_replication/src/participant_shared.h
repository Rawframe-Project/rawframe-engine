#pragma once

// What the replication participants share (participants.cpp,
// bots_participant.cpp): their settings read from configuration, and the
// lanes both sides declare. Their profiles are public (profiles.h).

#include "rawframe/composition/composition.h"
#include "rawframe/composition/participant.h"
#include "rawframe/network/session.h"
#include "rawframe/network/transport.h"
#include "rawframe/result/result.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_replication/profiles.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_replication {

/// Bytes as lower-case hexadecimal, for a capture's log.
[[nodiscard]] std::string hexOf(std::span<const std::byte> bytes);

/// The two-sided capture is a development capability: a
/// shipping build refuses the key that asks for it.
[[nodiscard]] result::Result<bool> captureAsked(const composition::Configuration& configuration, std::string_view key);

/// The event lanes both sides declare: the engine's, the game's when
/// it sends messages, its bound checked when the game loaded, and
/// its command lane when it has commands, bound by the largest.
[[nodiscard]] std::vector<network::EventLaneDeclaration> lanesOf(const ReplicationPlan& plan);

/// What peers must agree on: the protocol, the game, its replicated
/// components, and the package revision, which is the CompositionId when
/// the Runtime's content is a Composition
/// and zero otherwise.
[[nodiscard]] result::Result<network::Compatibility> compatibilityOf(composition::ParticipantContext& context,
                                                                     const ReplicationPlan& plan);

/// Connections a server holds beyond its players, so a hello arriving when it
/// is full can still be answered `capacity` instead of the transport closing
/// on it.
[[nodiscard]] std::size_t admissionRoom(std::size_t players) noexcept;

/// A configuration a participant cannot run with.
[[nodiscard]] std::unexpected<result::Error> missing(std::string_view why);

/// The bots participant (bots_participant.cpp): headless clients, and the
/// process's own local players.
[[nodiscard]] result::Result<composition::ParticipantOwner> makeBots(composition::ParticipantContext& context) noexcept;

} // namespace rawframe::world_replication
