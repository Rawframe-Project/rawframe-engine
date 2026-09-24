#pragma once

#include "rawframe/base/assert.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/archetype.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::world {

/// How a ColumnQuery term uses its component, as the typed Query's terms do.
enum class Access : std::uint8_t {
    Read,
    Write,
    With,
    Without
};

struct ColumnTerm {
    schema::ComponentRuntimeId component;
    Access access = Access::Read;
};

/// One matching archetype's rows: its entities and, for each Read and Write
/// term in term order, the first byte of that component's column. Row `n` of
/// a column is at `columns[i] + n * size` for the component's size.
struct ColumnChunk {
    std::span<const EntityHandle> entities;
    std::span<std::byte* const> columns;
};

/// The Query of code that knows components only by runtime ID: a script, a
/// tool, a replication pass. Same matching and the same deterministic order as
/// Query, handed out an archetype at a time. A query serves one World.
class ColumnQuery {
public:
    /// Refuses (`invalid_argument`) a component not in the registry, a
    /// component named twice, and a Read or Write of a tag.
    [[nodiscard]] static result::Result<ColumnQuery> resolve(std::span<const ColumnTerm> terms,
                                                             const schema::SchemaRegistry& registry);

    /// Calls `function(const ColumnChunk&)` once per matching archetype that
    /// has rows, in archetype creation order.
    template <typename Function> void forEachChunk(World& world, Function&& function) {
        refresh(world);
        const auto kArchetypes = world.archetypes();
        for (const Match& match : matches_) {
            detail::Archetype& archetype = *kArchetypes[match.archetype];
            if (archetype.size() == 0) {
                continue;
            }
            for (std::size_t index = 0; index < match.columns.size(); ++index) {
                columns_[index] = archetype.column(match.columns[index]).data;
            }
            function(ColumnChunk{.entities = archetype.entities(), .columns = columns_});
        }
    }

    /// How many entities match now.
    [[nodiscard]] std::size_t count(World& world);

    [[nodiscard]] std::span<const ColumnTerm> terms() const noexcept {
        return terms_;
    }
    /// The components read and written, for a system's declaration.
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> reads() const;
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> writes() const;

private:
    struct Match {
        std::size_t archetype;
        std::vector<int> columns;
    };

    void refresh(const World& world);

    std::vector<ColumnTerm> terms_;
    const World* world_ = nullptr;
    std::size_t seen_ = 0;
    std::vector<Match> matches_;
    std::vector<std::byte*> columns_;
};

} // namespace rawframe::world
