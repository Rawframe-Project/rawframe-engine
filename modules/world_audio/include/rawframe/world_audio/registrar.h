#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_audio {

/// Contributes two World-scoped participants that hear one client's
/// mirrored World each frame as its player would (the listener bound to the
/// player), each doing nothing unless configured:
///
/// - `rawframe.world_audio.recorder` writes what it heard to a 48 kHz stereo
///   WAVE file when the World stops, then logs a summary.
/// - `rawframe.world_audio.player` plays it on an output device, whose own
///   thread renders the mixer at the device's rate, and logs a summary when
///   the World stops. A machine without a device logs that and runs on
///   unheard.
///
/// Configuration keys:
///
///   audio.record           the WAVE file to write
///   audio.record_seconds   the most it records (60)
///   audio.play             `device`, or `null` for a device that plays nothing
///   audio.play_period      the device's buffer in frames (256)
///   audio.client           which of the process's clients to hear (0)
///   kest.game, kest.library  the game whose `mixer` and `sound` lines it plays
///
/// Sounds are read by identity from `rawframe.content.game`, which the
/// Runtime provides (`rawframe.game_content`).
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::World);

} // namespace rawframe::world_audio
