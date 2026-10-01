#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::particles_gpu {

/// The domain of every Error this module creates (ADR-0053's simulation).
inline constexpr result::ErrorDomain kParticlesGpuDomain{
    base::parseBits128Hex("1609b4daafab4c0485baeaf6e477067c").value};

enum class ParticlesGpuError : std::uint32_t {
    /// The device refused or failed work: a buffer, a pass, a draw.
    Device = 1,
};

[[nodiscard]] constexpr result::ErrorCode code(ParticlesGpuError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::particles_gpu
