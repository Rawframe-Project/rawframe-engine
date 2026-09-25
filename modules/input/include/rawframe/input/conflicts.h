#pragma once

// SPEC-0029's conflict query, for the rebinding surface to show before a
// change is kept: collisions, shadowing, reserved controls, and orphaned
// overrides, found by what would activate, not by comparing records.

#include "rawframe/input/actions.h"
#include "rawframe/input/overrides.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace rawframe::input {

struct Conflict {
    enum class Kind : std::uint8_t {
        /// One control activates two actions of one context.
        Collision,
        /// A binding no press reaches: a context of higher priority claims
        /// its control.
        Shadowed,
        /// A binding of a control the product keeps.
        Reserved,
        /// An override naming what the set does not have.
        Orphaned,
    };
    static constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

    Kind kind = Kind::Collision;
    /// The action and, for a collision or shadowing, the other one; kNone
    /// where there is none.
    std::size_t action = kNone;
    std::size_t other = kNone;
    /// The context it happens in, or kNone.
    std::size_t context = kNone;
    Control control;
    /// For an orphaned override, the identity it names.
    std::uint64_t identity = 0;
};

/// Every conflict in `effective` (a set with its overrides applied), and
/// the orphans of `overrides` when given, in a stable order.
[[nodiscard]] std::vector<Conflict> conflicts(const ActionSet& effective, const Overrides* overrides = nullptr);

} // namespace rawframe::input
