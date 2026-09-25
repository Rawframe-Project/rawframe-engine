#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_audio {

/// Contributes `rawframe.world_audio.recorder`, a World-scoped participant
/// that hears one client's mirrored World each frame as its player would
/// (the listener bound to the player) and writes what it heard to a 48 kHz
/// stereo WAVE file when the World stops, then logs a summary. Without
/// `audio.record` it does nothing. Configuration keys:
///
///   audio.record           the WAVE file to write
///   audio.record_client    which of the process's clients to hear (0)
///   audio.record_seconds   the most it records (60)
///   kest.game, kest.library  the game whose `mixer` and `sound` lines it plays
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_audio
