#pragma once

// The sound importer (`rawframe.audio`): a WAVE, Ogg Vorbis, or MP3 source
// cooked into the short-form tier (`rawframe.audio.wave`) or cooked Opus
// (`rawframe.audio.opus`), as its sidecar's `tier` says.

#include "rawframe/audio/decode.h"
#include "rawframe/content/identity.h"
#include "rawframe/cook/cook.h"

namespace rawframe::cook {

/// The type of every cooked sound clip, as the audio module names it.
inline constexpr content::ResourceTypeId kSoundClipType{
    base::parseBits128Hex("684215ae3a2a3f665361a5a7b3b260c6").value};

[[nodiscard]] Importer audioImporter() noexcept;

} // namespace rawframe::cook
