#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::world_audio {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kWorldAudioDomain{base::parseBits128Hex("5def32bcb7d7a6636147d59681203c0c").value};

/// Codes within kWorldAudioDomain.
enum class WorldAudioError : std::uint32_t {
    /// The game declares no mixer, or a file it names cannot be read.
    NoAudio = 1,
    /// Its emitter and listener components are missing or not laid out as
    /// `rawframe.sound` lays them out.
    BadComponents = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(WorldAudioError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::world_audio
