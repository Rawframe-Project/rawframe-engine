#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace rawframe::world {

/// A World-local runtime entity: a 32-bit slot and a 32-bit generation, valid
/// only with the World that issued it (SPEC-0006 runtime entity handle). It is
/// never persisted, replicated, or handed to scripts as a number. Generation 0
/// is never live, so the default value is the null handle.
struct EntityHandle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isNull() const noexcept {
        return generation == 0;
    }

    friend constexpr bool operator==(const EntityHandle&, const EntityHandle&) noexcept = default;
    friend constexpr std::strong_ordering operator<=>(const EntityHandle&, const EntityHandle&) noexcept = default;
};

static_assert(sizeof(EntityHandle) == 8 && std::is_trivially_copyable_v<EntityHandle>);

} // namespace rawframe::world

template <> struct std::hash<rawframe::world::EntityHandle> {
    [[nodiscard]] std::size_t operator()(const rawframe::world::EntityHandle& handle) const noexcept {
        return std::hash<std::uint64_t>{}((std::uint64_t{handle.slot} << 32U) | handle.generation);
    }
};
