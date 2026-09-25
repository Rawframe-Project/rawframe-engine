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

/// The clip at `rate`, for cooking: a windowed-sinc filter at the ratio of
/// the two rates exactly, passing up to 95% of the lower rate's Nyquist and
/// holding what lies above it near -90 dB. Only import tooling resamples
/// this way; playback's one resampling stage is the mixer's. Refuses rates
/// outside 8 to 192 kHz and ratios needing more than 48,000 phases.
[[nodiscard]] result::Result<audio::Clip> resample(const audio::Clip& clip, std::uint32_t rate);

struct OpusSettings {
    /// Bits a second; nought lets the encoder choose for the channels.
    std::uint32_t bitrate = 0;
    /// The encoder's effort, 0 to 10.
    std::uint32_t complexity = 10;
};

/// The clip in the lossy tier, cooked Opus (`audio::decodeCookedOpus` reads
/// it), in packets of 20 ms, resampled first to 48 kHz, the one rate cooked
/// Opus holds, if it is not there already.
[[nodiscard]] result::Result<std::vector<std::byte>> cookOpus(const audio::Clip& clip,
                                                              const OpusSettings& settings = {});

} // namespace rawframe::audio_import
