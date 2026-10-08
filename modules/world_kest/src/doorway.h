#pragma once

// What Kest's World doors reach and the doors themselves (D125 onward):
// creating, spawning, and destroying entities, a run's random streams, and
// each component a program may insert, remove, get, or look up, all staged
// in the running system's commands. KestSystems offers them to its
// programs.

#include "rawframe/world/persistent.h"
#include "rawframe/world_kest/kest_systems.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_kest {

// An entity a system's command buffer will create has no handle yet. Until
// the barrier a program holds it as generation 0 (never live) with the
// buffer's index, plus which run made it, so one kept past its run is
// refused rather than taken for another run's.
inline constexpr std::uint32_t kPendingIndexBits = 20;
inline constexpr std::uint32_t kPendingIndexMask = (1U << kPendingIndexBits) - 1U;
inline constexpr std::uint32_t kRunMask = (1U << (32U - kPendingIndexBits)) - 1U;

inline world::EntityHandle encodePending(world::PendingEntity pending, std::uint32_t run) noexcept {
    return world::EntityHandle{.slot = ((run & kRunMask) << kPendingIndexBits) | (pending.index + 1U), .generation = 0};
}

/// What the doors reach: the system running now, and each component a
/// program may insert or remove. Its address is every door's context, so it
/// lives as long as the machine.
struct KestSystems::Doorway {
    struct Component {
        Doorway* doorway = nullptr;
        schema::ComponentTypeId id;
        std::string kestType;
        std::string insertName;
        std::string removeName;
        std::string getName;
        std::string hasName;
        std::string countName;
        std::string entityName;
        std::array<kest::Parameter, 2> insertTakes{kest::Parameter{kest::Slot::Value, kEntityType},
                                                   kest::Parameter{kest::Slot::Value, ""}};
        std::array<kest::Parameter, 1> getGives{kest::Parameter{kest::Slot::Value, ""}};
        schema::ComponentRuntimeId runtime;
        const schema::ComponentDescriptor* descriptor = nullptr;
        std::vector<std::byte> scratch;
        std::vector<std::size_t> entityFields;
        /// Every entity holding it, for a lookup's count and entity doors
        /// (D393); made with the descriptor.
        std::optional<world::ColumnQuery> holders;
        /// Offered though the game does not list it: a World without it
        /// refuses the door when called, not the systems when declared.
        bool optional = false;
    };

    /// A prefab with its components' registry entries, found with the
    /// components'.
    struct Prefab {
        KestPrefab prefab;
        std::vector<std::vector<schema::ComponentRuntimeId>> runtimes;
        std::vector<std::vector<const schema::ComponentDescriptor*>> descriptors;
    };

    world::SystemContext* context = nullptr;
    /// What the running system may look up on any entity (D391).
    std::span<const schema::ComponentRuntimeId> lookups;
    /// Journal bytes the machine's systems took in `journalTick`, against
    /// `journalLimit` (D229).
    std::uint64_t journalTick = std::numeric_limits<std::uint64_t>::max();
    std::size_t journaled = 0;
    std::size_t journalLimit = kMaximumJournalBytes;
    /// Commands recorded in `journalTick`, against `operationLimit` (D240).
    std::size_t operations = 0;
    std::size_t operationLimit = kMaximumJournalOperations;
    std::uint32_t run = 0;
    /// Where runs are timed, or nowhere (D210).
    KestTiming* timing = nullptr;
    /// Told of each run (D266).
    std::vector<KestStaging*> staging;
    std::vector<Component> components;
    std::vector<Prefab> prefabs;
    // Reused by every spawn, sized when the systems are declared, so a
    // spawn allocates nothing.
    std::vector<std::byte> prefabScratch;
    std::vector<world::PendingEntity> prefabMade;
    std::vector<std::size_t> prefabOffsets;

    /// The target an entity from the program names, or why it names none.
    [[nodiscard]] std::optional<world::CommandTarget> target(kest::DoorCall& call, std::size_t argument) const {
        world::EntityHandle entity;
        if (!call.value(argument, std::as_writable_bytes(std::span{&entity, 1}))) {
            call.fail("an entity did not cross as an entity");
            return std::nullopt;
        }
        if (!entity.isNull()) {
            return world::CommandTarget{entity};
        }
        const std::uint32_t kIndex = entity.slot & kPendingIndexMask;
        if (kIndex == 0 || (entity.slot >> kPendingIndexBits) != (run & kRunMask)) {
            call.fail("the entity is null, or was created by another system run and kept");
            return std::nullopt;
        }
        return world::CommandTarget{world::PendingEntity{kIndex - 1U}};
    }
};

using Doorway = KestSystems::Doorway;

inline constexpr std::array<kest::Parameter, 1> kEntityParameter = {kest::Parameter{kest::Slot::Value, kEntityType}};
inline constexpr std::array<kest::Parameter, 1> kPrefabParameter = {kest::Parameter{kest::Slot::U64}};

/// The doors, each given the Doorway (or, for a component's, its entry) as
/// its context.
void createDoor(kest::DoorCall& call, void* context) noexcept;
void spawnDoor(kest::DoorCall& call, void* context) noexcept;
void destroyDoor(kest::DoorCall& call, void* context) noexcept;
void belowDoor(kest::DoorCall& call, void* context) noexcept;
void unitDoor(kest::DoorCall& call, void* context) noexcept;
void insertDoor(kest::DoorCall& call, void* context) noexcept;
void removeDoor(kest::DoorCall& call, void* context) noexcept;
void getDoor(kest::DoorCall& call, void* context) noexcept;
void hasDoor(kest::DoorCall& call, void* context) noexcept;
void countDoor(kest::DoorCall& call, void* context) noexcept;
void entityDoor(kest::DoorCall& call, void* context) noexcept;

} // namespace rawframe::world_kest
