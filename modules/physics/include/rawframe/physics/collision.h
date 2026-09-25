#pragma once

// SPEC-0037's collision document (§7), one per game and the same for 2D and
// 3D physics: collision classes, each a durable identity and a name, the
// rule between each pair of them, and the rule for every pair not listed.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rawframe::physics {

/// What happens where two collision classes meet (SPEC-0037's closed rule
/// vocabulary): they push each other, overlap and are told, or pass through
/// unaware.
enum class CollisionRule : std::uint8_t {
    Collide,
    Trigger,
    Ignore,
};

/// A collision class: a durable identity (never nought) and its name.
struct CollisionClass {
    std::uint64_t id = 0;
    std::string name;
};

struct CollisionPair {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    CollisionRule rule = CollisionRule::Collide;
};

/// The classes, the rules between pairs of them (either order), and the rule
/// for every pair not listed, bodies of no class among them. A body naming a
/// class not here is not made. A body that is a sensor overlaps what its
/// class does not ignore.
struct CollisionDocument {
    std::vector<CollisionClass> classes;
    std::vector<CollisionPair> rules;
    CollisionRule fallback = CollisionRule::Collide;
};

/// A query's `among` that meets bodies of every class, and of none.
inline constexpr std::uint64_t kEveryClass = 0;

/// Classes a document may declare.
inline constexpr std::size_t kMaximumCollisionClasses = 30;

} // namespace rawframe::physics
