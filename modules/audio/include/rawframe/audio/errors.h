#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::audio {

/// The domain of every Error the mixer and decoders create. Documents are
/// refused in `rawframe.document`'s domain.
inline constexpr result::ErrorDomain kAudioDomain{base::parseBits128Hex("40bf646096aebc02fa89f03c54cd647f").value};

/// Codes within kAudioDomain.
enum class AudioError : std::uint32_t {
    /// Mixer settings out of range.
    BadSettings = 1,
    /// A play of no clip, onto a bus the layout lacks, or at no pitch.
    BadPlay = 2,
    /// Every voice is in use.
    NoVoice = 3,
    /// The command queue to the mix thread is full.
    QueueFull = 4,
    /// Bytes that are not a sound of an admitted form.
    BadSound = 5,
    /// A full concurrency set turned a play away.
    Concurrency = 6,
    /// No output device could be opened.
    NoDevice = 7,
    /// An effect parameter write to no such bus, effect, band, or
    /// parameter, or outside its range.
    BadParameter = 8,
};

[[nodiscard]] constexpr result::ErrorCode code(AudioError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::audio
