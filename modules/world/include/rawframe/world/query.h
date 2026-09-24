#pragma once

#include "rawframe/base/assert.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/archetype.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace rawframe::world {

// Query terms. Read and Write hand the component to the function; With and
// Without only filter. A tag has no value, so it is only ever filtered on.

template <schema::Component T> struct Read {
    static_assert(!std::is_empty_v<T>, "a tag has no value to read; use With");
    using Component = T;
    static constexpr bool kData = true;
    static constexpr bool kWrites = false;
    static constexpr bool kRequired = true;
};

template <schema::Component T> struct Write {
    static_assert(!std::is_empty_v<T>, "a tag has no value to write; use With");
    using Component = T;
    static constexpr bool kData = true;
    static constexpr bool kWrites = true;
    static constexpr bool kRequired = true;
};

template <schema::Component T> struct With {
    using Component = T;
    static constexpr bool kData = false;
    static constexpr bool kWrites = false;
    static constexpr bool kRequired = true;
};

template <schema::Component T> struct Without {
    using Component = T;
    static constexpr bool kData = false;
    static constexpr bool kWrites = false;
    static constexpr bool kRequired = false;
};

/// Every entity whose archetype has all the required terms and none of the
/// excluded ones. Iteration is in archetype creation order, then row order,
/// both of which follow only from the World's operation history, so the same
/// history always iterates the same way. Matching archetypes are cached and
/// only archetypes created since the last run are examined. A query serves the
/// one World it first runs against.
template <typename... Terms> class Query {
public:
    /// Resolves every term against the registry. `not_found` if one is not in
    /// it.
    [[nodiscard]] static result::Result<Query> resolve(const schema::SchemaRegistry& registry) {
        Query query;
        std::size_t index = 0;
        result::Status resolved;
        (
            [&] {
                if (!resolved.has_value()) {
                    return;
                }
                auto id = registry.find(Terms::Component::kComponentTypeId);
                if (!id.has_value()) {
                    resolved = std::unexpected<result::Error>{std::move(id).error()};
                    return;
                }
                query.ids_[index++] = *id;
            }(),
            ...);
        if (!resolved.has_value()) {
            return std::unexpected<result::Error>{std::move(resolved).error()};
        }
        return query;
    }

    /// Calls `function(EntityHandle, value...)` for each match, with `const T&`
    /// for each Read and `T&` for each Write, in term order.
    template <typename Function> void forEach(World& world, Function&& function) {
        refresh(world);
        const auto kArchetypes = world.archetypes();
        for (const Match& match : matches_) {
            detail::Archetype& archetype = *kArchetypes[match.archetype];
            const std::size_t kRows = archetype.size();
            for (std::size_t row = 0; row < kRows; ++row) {
                std::apply(function,
                           std::tuple_cat(std::make_tuple(archetype.entity(row)),
                                          fetchAll(archetype, match, row, std::index_sequence_for<Terms...>{})));
            }
        }
    }

    /// How many entities match now.
    [[nodiscard]] std::size_t count(World& world) {
        refresh(world);
        std::size_t total = 0;
        for (const Match& match : matches_) {
            total += world.archetypes()[match.archetype]->size();
        }
        return total;
    }

    /// The components this query reads and writes, for a system's declaration.
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> reads() const {
        return idsWhere<false>();
    }
    [[nodiscard]] std::vector<schema::ComponentRuntimeId> writes() const {
        return idsWhere<true>();
    }

private:
    struct Match {
        std::size_t archetype;
        std::array<int, sizeof...(Terms)> columns;
    };

    template <bool Writes> [[nodiscard]] std::vector<schema::ComponentRuntimeId> idsWhere() const {
        std::vector<schema::ComponentRuntimeId> ids;
        std::size_t index = 0;
        ((Terms::kData && Terms::kWrites == Writes ? ids.push_back(ids_[index++]) : static_cast<void>(index++)), ...);
        return ids;
    }

    void refresh(const World& world) {
        // A query caches archetype indices of one World, so it serves one World.
        RAWFRAME_ASSERT(world_ == nullptr || world_ == &world, "a query used with a second World");
        world_ = &world;
        const auto kArchetypes = world.archetypes();
        for (; seen_ < kArchetypes.size(); ++seen_) {
            const detail::Archetype& archetype = *kArchetypes[seen_];
            bool matches = true;
            std::size_t index = 0;
            // Every term is examined, so `index` advances once per term.
            ((matches = (archetype.has(ids_[index++]) == Terms::kRequired) && matches), ...);
            if (!matches) {
                continue;
            }
            Match match{.archetype = seen_, .columns = {}};
            for (std::size_t term = 0; term < sizeof...(Terms); ++term) {
                match.columns[term] = archetype.columnIndex(ids_[term]);
            }
            matches_.push_back(match);
        }
    }

    template <std::size_t... Index>
    static auto
    fetchAll(detail::Archetype& archetype, const Match& match, std::size_t row, std::index_sequence<Index...>) {
        return std::tuple_cat(fetch<Terms, Index>(archetype, match, row)...);
    }

    template <typename Term, std::size_t Index>
    static auto fetch(detail::Archetype& archetype, const Match& match, std::size_t row) {
        if constexpr (!Term::kData) {
            return std::tuple<>{};
        } else {
            using Value = typename Term::Component;
            auto* value = static_cast<Value*>(archetype.column(match.columns[Index]).at(row));
            if constexpr (Term::kWrites) {
                return std::tuple<Value&>{*value};
            } else {
                return std::tuple<const Value&>{*value};
            }
        }
    }

    std::array<schema::ComponentRuntimeId, sizeof...(Terms)> ids_{};
    const World* world_ = nullptr;
    std::size_t seen_ = 0;
    std::vector<Match> matches_;
};

} // namespace rawframe::world
