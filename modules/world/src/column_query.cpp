#include "rawframe/world/column_query.h"

#include "rawframe/world/errors.h"

namespace rawframe::world {

namespace {

bool carriesData(Access access) noexcept {
    return access == Access::Read || access == Access::Write;
}

} // namespace

result::Result<ColumnQuery> ColumnQuery::resolve(std::span<const ColumnTerm> terms,
                                                 const schema::SchemaRegistry& registry) {
    ColumnQuery query;
    std::size_t data = 0;
    for (std::size_t index = 0; index < terms.size(); ++index) {
        const ColumnTerm& term = terms[index];
        bool repeated = false;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            repeated = repeated || terms[earlier].component.value == term.component.value;
        }
        if (term.component.value >= registry.components().size() || repeated ||
            (carriesData(term.access) && registry.descriptor(term.component).size == 0)) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kWorldDomain,
                                code(WorldError::InvalidQueryTerm),
                                "a query term names an unknown component, repeats one, or reads a tag");
        }
        data += carriesData(term.access) ? 1 : 0;
    }
    query.terms_.assign(terms.begin(), terms.end());
    query.columns_.resize(data);
    return query;
}

std::size_t ColumnQuery::count(World& world) {
    refresh(world);
    std::size_t total = 0;
    for (const Match& match : matches_) {
        total += world.archetypes()[match.archetype]->size();
    }
    return total;
}

std::vector<schema::ComponentRuntimeId> ColumnQuery::reads() const {
    std::vector<schema::ComponentRuntimeId> ids;
    for (const ColumnTerm& term : terms_) {
        if (term.access == Access::Read) {
            ids.push_back(term.component);
        }
    }
    return ids;
}

std::vector<schema::ComponentRuntimeId> ColumnQuery::writes() const {
    std::vector<schema::ComponentRuntimeId> ids;
    for (const ColumnTerm& term : terms_) {
        if (term.access == Access::Write) {
            ids.push_back(term.component);
        }
    }
    return ids;
}

void ColumnQuery::refresh(const World& world) {
    // Archetype indices belong to one World, so a query serves one World.
    RAWFRAME_ASSERT(world_ == nullptr || world_ == &world, "a column query used with a second World");
    world_ = &world;
    const auto kArchetypes = world.archetypes();
    for (; seen_ < kArchetypes.size(); ++seen_) {
        const detail::Archetype& archetype = *kArchetypes[seen_];
        bool matches = true;
        for (const ColumnTerm& term : terms_) {
            matches = matches && archetype.has(term.component) == (term.access != Access::Without);
        }
        if (!matches) {
            continue;
        }
        Match match{.archetype = seen_, .columns = {}};
        for (const ColumnTerm& term : terms_) {
            if (carriesData(term.access)) {
                match.columns.push_back(archetype.columnIndex(term.component));
            }
        }
        matches_.push_back(std::move(match));
    }
}

} // namespace rawframe::world
