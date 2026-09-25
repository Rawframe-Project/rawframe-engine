#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::kest {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kKestDomain{base::parseBits128Hex("7950591838e8a750d589f83219cd1e88").value};

/// Codes within kKestDomain.
enum class KestError : std::uint32_t {
    /// The program did not compile; the report says why.
    DoesNotCompile = 1,
    /// The Kest library linked is not the one its header describes.
    AbiMismatch = 2,
    /// The program asks for a door the table does not hold.
    UnknownDoor = 3,
    /// A door's slots differ from what the program declared for it.
    DoorShapeMismatch = 4,
    /// Untrusted code asked for a door not marked safe for it.
    DoorNotForUntrusted = 5,
    /// A required limit was zero.
    MissingLimit = 7,
    /// The machine did not start; the report says why.
    DidNotStart = 8,
    /// The program defines no function of that name, or several.
    UnknownEntry = 9,
    /// A call spent all the fuel it was given.
    FuelExhausted = 10,
    /// The program refused while running; the report says why.
    ScriptFailed = 11,
    /// Two doors of one table share a name.
    DuplicateDoor = 12,
    /// A frame is narrower than the function needs.
    FrameTooSmall = 13,
    /// A call reached the heap ceiling it was given.
    HeapExhausted = 14,
    /// The program has no one type of that name.
    UnknownType = 15,
    /// A lend was refused: an unknown element type, a size that disagrees,
    /// or a type holding more than numbers.
    LendRefused = 16,
};

[[nodiscard]] constexpr result::ErrorCode code(KestError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::kest
