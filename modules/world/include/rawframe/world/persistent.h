#pragma once

// Persistent entity identity (SPEC-0006): a StableId128 an entity carries
// only if it opts in, as ordinary component data, so the hot entity record
// stays as it is. It means something only within a persistence namespace,
// which the save or checkpoint that records it names; nothing here looks one
// up process-wide.
//
// An identity comes from one of two places, both deterministic:
//   - an entity an authored scene brings is named from that scene's identity
//     and the entity's own id there, so the same level spawned into a new
//     World gives its entities the same identities, and a save can find them
//     again;
//   - an entity made while the World runs draws its identity from the World's
//     random stream `rawframe.world`/`persistent`, which checkpoints carry.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/component.h"
#include "rawframe/world/world.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rawframe::world {

struct PersistentEntityId {
    base::Bits128 value;

    friend constexpr bool operator==(const PersistentEntityId&, const PersistentEntityId&) noexcept = default;
    friend constexpr auto operator<=>(const PersistentEntityId&, const PersistentEntityId&) noexcept = default;
};

/// The component an entity opts in with. Nought is no identity: a value of
/// nought asks the engine to name the entity where it is made.
struct Persistent {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("7c2f5a90-3e18-4d6b-9a47-e1b08c6d2f35");
    static constexpr std::string_view kComponentName = "rawframe.world.persistent";

    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] constexpr PersistentEntityId id() const noexcept {
        return PersistentEntityId{base::Bits128{.high = high, .low = low}};
    }
    [[nodiscard]] constexpr bool named() const noexcept {
        return high != 0 || low != 0;
    }
};

inline constexpr std::string_view kPersistentStreamOwner = "rawframe.world";
inline constexpr std::string_view kPersistentStreamName = "persistent";

/// The identity of `entity` of the authored scene `scene`, by the entity's
/// id there: SHA-256 of a versioned label, the scene, and the entity, in its
/// first sixteen bytes. Never nought.
[[nodiscard]] PersistentEntityId persistentFromSource(base::Bits128 scene, base::Bits128 entity) noexcept;

/// A fresh identity for an entity made while the World runs, drawn from its
/// persistent stream. Never nought. The World's structure need not be
/// unlocked; the caller applies it where structural changes are made.
[[nodiscard]] PersistentEntityId newPersistentId(World& world);

/// Every identity two or more of `world`'s entities hold, each once, in
/// order; empty when each is unique. A World without the component in its
/// registry has none. An entity whose identity is nought is not counted.
[[nodiscard]] std::vector<PersistentEntityId> persistentDuplicates(World& world);

} // namespace rawframe::world
