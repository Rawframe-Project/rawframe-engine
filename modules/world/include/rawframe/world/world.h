#pragma once

#include "rawframe/base/assert.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/archetype.h"
#include "rawframe/world/command_buffer.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/random.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world {

struct WorldSettings {
    /// Entity slots this World may ever use, retired slots included. Creation
    /// past it fails with `resource_exhausted`.
    std::uint32_t maximumEntities = 1U << 20U;
    /// The generation a new slot starts at. Only tests change it, to reach
    /// slot retirement without four billion reuses. Must be at least 1.
    std::uint32_t firstGeneration = 1;
    /// The seed every random stream in this World derives from (ADR-0055).
    RootSeed rootSeed;
};

/// One authoritative ECS identity domain: entity slots and generations,
/// archetype storage, and the binding to one frozen registry (SPEC-0006).
///
/// Structural operations (create, destroy, insert, remove) run directly only
/// while the structure is unlocked. While a schedule runs systems, the
/// structure is locked and changes go through command buffers applied at a
/// barrier (SPEC-0005).
class World {
public:
    World(std::shared_ptr<const schema::SchemaRegistry> registry, WorldSettings settings = {});
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World();

    [[nodiscard]] const schema::SchemaRegistry& registry() const noexcept {
        return *registry_;
    }

    [[nodiscard]] result::Result<EntityHandle> create();
    /// Destroys the entity and every component on it. Its handle, and every
    /// copy, is stale from this moment.
    [[nodiscard]] result::Status destroy(EntityHandle entity);
    [[nodiscard]] bool alive(EntityHandle entity) const noexcept;
    [[nodiscard]] std::size_t entityCount() const noexcept {
        return liveEntities_;
    }

    /// Adds the component, or replaces its value if the entity has it.
    template <schema::Component T>
    [[nodiscard]] result::Status insert(EntityHandle entity, schema::ComponentKey<T> key, T value) {
        RAWFRAME_ASSERT(registry_->owns(key), "a component key from another registry");
        return insertErased(entity, key.id, &value);
    }

    /// Removes the component; removing one the entity lacks does nothing.
    template <schema::Component T>
    [[nodiscard]] result::Status remove(EntityHandle entity, schema::ComponentKey<T> key) {
        RAWFRAME_ASSERT(registry_->owns(key), "a component key from another registry");
        return removeErased(entity, key.id);
    }

    /// The component, or null if the handle is stale or the entity lacks it. A
    /// tag has no value; use `has`.
    template <schema::Component T> [[nodiscard]] T* get(EntityHandle entity, schema::ComponentKey<T> key) noexcept {
        RAWFRAME_ASSERT(registry_->owns(key), "a component key from another registry");
        return static_cast<T*>(getErased(entity, key.id));
    }

    template <schema::Component T>
    [[nodiscard]] bool has(EntityHandle entity, schema::ComponentKey<T> key) const noexcept {
        return hasErased(entity, key.id);
    }

    // The type-erased forms, for command buffers and generated bindings.
    // `value` is moved from; its owner still destroys it. Inserting a
    // persistent identity of nought (persistent.h) names the entity: it
    // keeps the identity it has, or draws a fresh one.
    [[nodiscard]] result::Status insertErased(EntityHandle entity, schema::ComponentRuntimeId component, void* value);
    [[nodiscard]] result::Status removeErased(EntityHandle entity, schema::ComponentRuntimeId component);
    [[nodiscard]] void* getErased(EntityHandle entity, schema::ComponentRuntimeId component) noexcept;
    [[nodiscard]] const void* getErased(EntityHandle entity, schema::ComponentRuntimeId component) const noexcept;
    [[nodiscard]] bool hasErased(EntityHandle entity, schema::ComponentRuntimeId component) const noexcept;

    [[nodiscard]] RootSeed rootSeed() const noexcept {
        return settings_.rootSeed;
    }

    /// The stream `name` of the durable owner `owner`, derived from the root
    /// seed on first use. Its state is World state: it advances only through
    /// its owner's draws.
    [[nodiscard]] Pcg32& randomStream(std::string_view owner, std::string_view name);

    /// Every stream drawn from so far, by owner then name: World state a
    /// checkpoint carries.
    [[nodiscard]] const std::map<std::pair<std::string, std::string>, Pcg32, std::less<>>&
    randomStreams() const noexcept {
        return randomStreams_;
    }
    /// Sets a stream's state, as a checkpoint restore does before the World
    /// is published.
    void restoreRandomStream(std::string owner, std::string name, Pcg32 state);

    /// Applies a command buffer in recording order and clears it. All or
    /// nothing on capacity: if the World cannot fit every entity the buffer
    /// creates, nothing is applied and the result is `resource_exhausted`.
    /// Commands on entities destroyed before they apply are skipped and counted.
    /// Requires the structure unlocked: this is the barrier.
    [[nodiscard]] result::Result<CommitReport> apply(CommandBuffer& buffer);

    /// While locked, direct structural operations fail with
    /// `failed_precondition`. The scheduler locks around system execution.
    void lockStructure() noexcept {
        structureLocked_ = true;
    }
    void unlockStructure() noexcept {
        structureLocked_ = false;
    }
    [[nodiscard]] bool structureLocked() const noexcept {
        return structureLocked_;
    }

    /// Archetypes in creation order, for queries. Only ever appended to.
    [[nodiscard]] std::span<const std::unique_ptr<detail::Archetype>> archetypes() const noexcept {
        return archetypes_;
    }

private:
    struct EntityRecord {
        std::uint32_t generation = 0;
        std::uint32_t archetype = 0;
        std::uint32_t row = 0;
        bool alive = false;
    };

    [[nodiscard]] result::Status checkStructure() const;
    /// Slots a create can still use: free ones and never-used ones.
    [[nodiscard]] std::size_t availableSlots() const noexcept;
    [[nodiscard]] result::Status checkLive(EntityHandle entity) const;
    [[nodiscard]] std::uint32_t findOrCreateArchetype(std::vector<schema::ComponentRuntimeId> components);
    [[nodiscard]] std::uint32_t transition(std::uint32_t from, schema::ComponentRuntimeId component, bool adding);
    /// Moves an entity's row to another archetype, carrying shared components,
    /// and returns its new row.
    std::size_t moveEntity(EntityHandle entity, std::uint32_t to);

    std::shared_ptr<const schema::SchemaRegistry> registry_;
    WorldSettings settings_;
    std::vector<EntityRecord> records_;
    std::vector<std::uint32_t> freeSlots_;
    std::size_t liveEntities_ = 0;
    std::vector<std::unique_ptr<detail::Archetype>> archetypes_;
    std::map<std::vector<schema::ComponentRuntimeId>, std::uint32_t> archetypeIndex_;
    bool structureLocked_ = false;
    std::map<std::pair<std::string, std::string>, Pcg32, std::less<>> randomStreams_;
    /// The persistent identity component, if the registry has it.
    std::optional<schema::ComponentRuntimeId> persistent_;
};

} // namespace rawframe::world
