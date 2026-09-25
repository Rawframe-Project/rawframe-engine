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
///   content.build          a Build instead (SPEC-0021): its directory
///   content.build_root     the Build's root hash, `sha256:` and 64 hex;
///                          a Build of any other identity is refused
///   content.build_keys     its publisher's key set (SPEC-0023), pinned:
///                          an unsigned Build, or one signed by a key it
///                          does not list or lists revoked, is refused
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::game_content
