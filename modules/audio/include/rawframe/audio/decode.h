#pragma once

// Decoding admitted sound forms into clips (SPEC-0036's closed ingest list).
// Every decoder is bounded and refuses what it does not fully understand.

#include "rawframe/audio/mixer.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <span>

namespace rawframe::audio {

struct DecodeLimits {
    std::size_t maximumBytes = std::size_t{256} * 1024 * 1024;
    /// Ten minutes of 48 kHz.
    std::size_t maximumFrames = std::size_t{48'000} * 600;
};

/// A RIFF WAVE of PCM (plain or extensible), one or two channels, 16 or 24
/// bits, 8 to 192 kHz. Refuses (`BadSound`) anything else, and anything
/// past the limits.
[[nodiscard]] result::Result<Clip> decodeWav(std::span<const std::byte> bytes, const DecodeLimits& limits = {});

} // namespace rawframe::audio
