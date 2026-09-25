#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::game_content {

/// Contributes `rawframe.game_content`, a Runtime-scoped participant that
/// provides `rawframe.content.game`: the game's cooked content, one store
/// for every family of the Runtime. Without `content.root` it provides a
/// store with nothing in it. Configuration keys:
///
///   content.root           the cook's output: `content.manifest` and the
///                          objects it names
///   content.reload_every   every this many Host iterations, publish a
///                          changed manifest as the next catalog (0: never)
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::game_content
