#pragma once

// Importing sounds (ADR-0058, SPEC-0036's closed ingest list): WAVE, Ogg
// Vorbis, and MP3 sources decoded into clips and cooked into the form the
// runtime reads. Import tooling only: the source decoders here are trusted
// with an author's own files and never link into a client or a server.

#include "rawframe/audio/decode.h"
#include "rawframe/audio/mixer.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::audio_import {

enum class SourceForm : std::uint8_t {
    Wave,
    Vorbis,
    Mp3,
};

[[nodiscard]] std::string_view describe(SourceForm form) noexcept;

/// The form `bytes` begin as, by their signature: `RIFF....WAVE`, `OggS`,
/// or an ID3 tag or MPEG audio frame. None for anything else.
[[nodiscard]] std::optional<SourceForm> sniff(std::span<const std::byte> bytes) noexcept;

/// Decodes a source of an admitted form into a clip at its own rate:
/// nothing is resampled or mixed down. Refuses (`BadSound`) a form not
/// admitted, a stream it cannot read, more than two channels, a rate
/// outside 8 to 192 kHz, and anything past the limits.
[[nodiscard]] result::Result<audio::Clip> importSound(std::span<const std::byte> bytes,
                                                      const audio::DecodeLimits& limits = {});

/// The clip in the runtime's short-form tier, 16-bit PCM WAVE.
[[nodiscard]] std::vector<std::byte> cook(const audio::Clip& clip);

} // namespace rawframe::audio_import
