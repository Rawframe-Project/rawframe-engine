#pragma once

#include "rawframe/composition/registrar.h"

#include <cstdint>

namespace rawframe::world_localization {

/// Contributes `rawframe.world_localization.text`, a Runtime participant
/// that reads the string tables and translations the game's `text` lines
/// name from `rawframe.content.game`, builds their catalog (refusing to
/// start when they do not make one), provides it as `kGameText`, and logs
/// `text_catalog` with what it holds and `text_stale` for each stale
/// translation. A game that names no text, or a process without cooked
/// content, reads none; the latter logs `text_unavailable`.
///
/// Configuration keys:
///
///   localization.locale   the player's locale, as a player or platform
///                         gives it (read by intake); the game's `locale`
///                         line, else its first table's source locale
///   localization.pseudo   development builds only: `true` adds each
///                         table's pseudo-localized `en-XA` translation
void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept;

inline constexpr std::uint8_t kScopes = composition::scopeBit(composition::LifetimeScope::Runtime);

} // namespace rawframe::world_localization
