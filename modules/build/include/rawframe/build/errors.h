#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::build {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kBuildDomain{base::parseBits128Hex("2d618980a7c12e1cd64bb2e9568cfd8d").value};

enum class BuildError : std::uint32_t {
    /// An identity field outside its grammar.
    BadIdentity = 1,
    /// An output inside the cooked content, or the other way round.
    BadRequest = 2,
    /// No cook receipt, or one that proves nothing: not a receipt, failures,
    /// or a manifest other than the one beside it.
    NoProof = 3,
    /// The manifest and the receipt disagree about an artifact, or an
    /// artifact's bytes are not what both say.
    ArtifactMismatch = 4,
    WriteFailed = 5,
    /// A publisher key that is not one, or a signing that failed.
    BadKey = 6,
};

[[nodiscard]] constexpr result::ErrorCode code(BuildError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::build
