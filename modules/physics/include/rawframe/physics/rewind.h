#pragma once

// What a physics query cast back in time (SPEC-0041's rewind query) takes,
// in either dimension: the moment, and which bodies are taken back to it.

#include "rawframe/world/entity.h"

#include <cstdint>
#include <optional>

namespace rawframe::physics {

/// Which bodies a query cast back in time takes back, and how far (SPEC-0041's
/// victim gate); one it gives no tick is tried where it is now.
class RewindGate {
public:
    RewindGate() = default;
    RewindGate(const RewindGate&) = delete;
    RewindGate& operator=(const RewindGate&) = delete;
    virtual ~RewindGate() = default;

    /// The earliest tick `entity`'s body may be taken back to: a moment
    /// before it is taken back to that tick's pose exactly, which is what a
    /// client shows of an entity before its first two states.
    [[nodiscard]] virtual std::optional<std::uint64_t> since(world::EntityHandle entity) const noexcept = 0;
};

/// A moment to cast back to: `fraction` 65536ths of the way from the pose
/// committed at tick `base` to the next, as a client shows it between
/// states, and what is taken back to it (every body without a gate).
struct Moment {
    std::uint64_t base = 0;
    std::uint16_t fraction = 0;
    const RewindGate* gate = nullptr;
};

} // namespace rawframe::physics
