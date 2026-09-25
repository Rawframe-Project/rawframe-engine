#pragma once

// A collision document as Maul's category and mask bits, which Maul2D and
// Maul3D filter by alike: bit `i` is class `i`'s solid shapes (class nought
// is bodies of none), bit 31 the character controller's queries, and bit
// `32 + i` class `i`'s sensors. A solid meets another class's solid where the
// rule is collide, and its sensors where the rule is not ignore; a sensor
// twin, made for a class that triggers with another, meets the solids of the
// classes it triggers with; a body that is a sensor meets every solid its
// class does not ignore. Every solid meets character queries except a
// character's own, which drops the bit.

#include "rawframe/physics/collision.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::physics {

/// How one class's shapes filter.
struct ClassFilter {
    std::uint64_t solidMask = 0;
    std::uint64_t triggerMask = 0;
    std::uint64_t sensorMask = 0;
};

inline constexpr std::uint64_t kSensorBits = 32;
inline constexpr std::uint64_t kCharacterQuery = std::uint64_t{1} << 31U;
/// Every class's solid bit, and none of the rest.
inline constexpr std::uint64_t kSolids = kCharacterQuery - 1;

class CollisionFilters {
public:
    /// Checks the document (SPEC-0037 §15, 2 and 3): identities not nought
    /// and names not empty, neither declared twice, rules naming declared
    /// classes, no pair ruled twice, at most kMaximumCollisionClasses.
    [[nodiscard]] static result::Result<CollisionFilters> make(const CollisionDocument& document);

    /// A class's index by its identity: nought for bodies of none, none for
    /// an identity the document does not declare.
    [[nodiscard]] std::optional<std::size_t> classIndex(std::uint64_t id) const noexcept;
    [[nodiscard]] const ClassFilter& filter(std::size_t index) const noexcept {
        return filters_[index];
    }
    [[nodiscard]] static constexpr std::uint64_t solidBit(std::size_t index) noexcept {
        return std::uint64_t{1} << index;
    }
    [[nodiscard]] static constexpr std::uint64_t sensorBit(std::size_t index) noexcept {
        return std::uint64_t{1} << (kSensorBits + index);
    }

private:
    /// By class index.
    std::vector<ClassFilter> filters_;
    /// Identities and their indices, by identity.
    std::vector<std::pair<std::uint64_t, std::size_t>> classes_;
};

} // namespace rawframe::physics
