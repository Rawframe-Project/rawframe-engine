#pragma once

#include "rawframe/schema/component.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/entity.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::world::detail {

/// One component's values for every row of an archetype, in one aligned
/// allocation. Tags have no column.
struct Column {
    schema::ComponentRuntimeId component;
    std::size_t size = 0;
    std::size_t alignment = 1;
    schema::ComponentOperations operations;
    std::byte* data = nullptr;

    [[nodiscard]] void* at(std::size_t row) const noexcept {
        return data + (row * size);
    }
};

/// All entities with exactly one set of component types, stored as columns.
/// Rows are dense: removing one moves the last row into its place, so row order
/// changes only in response to structural operations, which are themselves
/// deterministic.
class Archetype {
public:
    Archetype(std::vector<schema::ComponentRuntimeId> components, const schema::SchemaRegistry& registry);
    Archetype(const Archetype&) = delete;
    Archetype& operator=(const Archetype&) = delete;
    ~Archetype();

    [[nodiscard]] std::span<const schema::ComponentRuntimeId> components() const noexcept {
        return components_;
    }
    [[nodiscard]] bool has(schema::ComponentRuntimeId component) const noexcept;
    /// The column holding `component`, or -1 for a tag or an absent component.
    [[nodiscard]] int columnIndex(schema::ComponentRuntimeId component) const noexcept;
    [[nodiscard]] Column& column(int index) noexcept {
        return columns_[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entities_.size();
    }
    [[nodiscard]] EntityHandle entity(std::size_t row) const noexcept {
        return entities_[row];
    }
    [[nodiscard]] std::span<const EntityHandle> entities() const noexcept {
        return entities_;
    }

    /// Appends a row for `entity` and returns its index. Every column's value at
    /// that row is uninitialized until the caller constructs it.
    std::size_t appendRow(EntityHandle entity);

    /// Destroys the row's values and fills the hole with the last row. Returns
    /// the entity now at `row`, or the null handle if `row` was the last.
    EntityHandle removeRow(std::size_t row) noexcept;

    // Cached transitions: the archetype reached by adding or removing one
    // component. Indices into the World's archetype list.
    struct Edge {
        schema::ComponentRuntimeId component;
        std::uint32_t archetype;
    };
    std::vector<Edge> addEdges;
    std::vector<Edge> removeEdges;

private:
    void grow();

    std::vector<schema::ComponentRuntimeId> components_; // sorted
    std::vector<Column> columns_;                        // sorted by component
    std::vector<EntityHandle> entities_;
    std::size_t capacity_ = 0;
};

} // namespace rawframe::world::detail
