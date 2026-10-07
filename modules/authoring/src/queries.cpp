#include "rawframe/authoring/queries.h"

#include "rawframe/authoring/errors.h"

#include <algorithm>

namespace rawframe::authoring {

namespace {

std::vector<EntityEntry> entriesOf(const scene::Scene& scene) {
    std::vector<EntityEntry> made;
    for (std::size_t at = 0; at < scene.entities.size(); ++at) {
        made.push_back(EntityEntry{.id = scene.entities[at].id,
                                   .name = scene.entities[at].name,
                                   .place = at,
                                   .brought = std::nullopt,
                                   .removed = false});
    }
    for (std::size_t at = 0; at < scene.instances.size(); ++at) {
        const scene::SceneInstance& instance = scene.instances[at];
        for (const scene::IdentityMapping& mapping : instance.entities) {
            const bool kRemoved = std::ranges::any_of(instance.overrides, [&mapping](const scene::Override& each) {
                return each.entity == mapping.instance && each.component.empty();
            });
            made.push_back(
                EntityEntry{.id = mapping.instance,
                            .name = {},
                            .place = std::nullopt,
                            .brought = Brought{.instance = at, .scene = instance.scene, .source = mapping.source},
                            .removed = kRemoved});
        }
    }
    return made;
}

ComponentReading readingOf(const scene::Scene& scene,
                           const ComponentCatalog& catalog,
                           const std::string& name,
                           std::optional<scene::Override::Kind> patch,
                           const std::vector<scene::SceneField>& fields) {
    const ComponentSchema* schema = catalog.findNamed(name);
    ComponentReading made{.component = schema != nullptr ? std::optional{schema->id} : std::nullopt,
                          .name = name,
                          .patch = patch,
                          .fields = {}};
    if (schema != nullptr) {
        const auto kRecorded = std::ranges::find(scene.schema, name, &scene::SchemaMark::component);
        made.stale = kRecorded != scene.schema.end() && kRecorded->mark != schema->mark;
    }
    for (const scene::SceneField& field : fields) {
        FieldReading reading{.name = field.name, .kind = std::nullopt, .cases = {}, .value = field.value};
        if (schema != nullptr) {
            const auto kFound = std::ranges::find(schema->fields, field.name, &FieldSchema::name);
            if (kFound != schema->fields.end()) {
                reading.kind = kFound->kind;
                reading.cases = kFound->cases;
            }
        }
        made.fields.push_back(std::move(reading));
    }
    return made;
}

/// Whether `name` holds `part`, ASCII letters in either case.
bool holds(std::string_view name, std::string_view part) noexcept {
    const auto kLower = [](char each) {
        return each >= 'A' && each <= 'Z' ? static_cast<char>(each - 'A' + 'a') : each;
    };
    return !std::ranges::search(name, part, {}, kLower, kLower).empty() || part.empty();
}

/// Whether the entity `entry` names holds the component named `component`:
/// the scene's own entity among its components, an instance's entity by
/// what its patch adds or sets (its source's components are the source
/// scene's to answer, as `scene.read_entity` has them).
bool holdsComponent(const scene::Scene& scene, const EntityEntry& entry, std::string_view component) {
    if (entry.place.has_value()) {
        return std::ranges::contains(scene.entities[*entry.place].components, component, &scene::SceneComponent::name);
    }
    return std::ranges::any_of(
        scene.instances[entry.brought->instance].overrides, [&entry, component](const scene::Override& each) {
            return each.entity == entry.id && each.component == component && each.kind != scene::Override::Kind::Remove;
        });
}

} // namespace

result::Result<Answer> answer(const scene::Scene& scene, const Query& query, const ComponentCatalog& catalog) {
    std::vector<EntityEntry> entries = entriesOf(scene);
    if (std::holds_alternative<ListEntities>(query)) {
        return EntityList{.entities = std::move(entries)};
    }
    if (const auto* find = std::get_if<FindEntities>(&query)) {
        const ComponentSchema* having = find->having.has_value() ? catalog.find(*find->having) : nullptr;
        if (find->having.has_value() && having == nullptr) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::NotFound,
                                                               kAuthoringDomain,
                                                               code(AuthoringError::TargetNotFound),
                                                               "the catalog knows no such component")
                                                      .error()
                                                      .withContext("operation", declarationOf(query).name)};
        }
        std::erase_if(entries, [&](const EntityEntry& entry) {
            return !holds(entry.name, find->named) ||
                   (having != nullptr && !holdsComponent(scene, entry, having->name));
        });
        return EntityList{.entities = std::move(entries)};
    }
    const base::Bits128 kEntity = std::get<ReadEntity>(query).entity;
    const auto kFound = std::ranges::find(entries, kEntity, &EntityEntry::id);
    if (kFound == entries.end()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::NotFound,
                                                           kAuthoringDomain,
                                                           code(AuthoringError::TargetNotFound),
                                                           "the scene holds no such entity")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    EntityReading made{.entity = std::move(*kFound), .components = {}};
    if (made.entity.place.has_value()) {
        for (const scene::SceneComponent& component : scene.entities[*made.entity.place].components) {
            made.components.push_back(readingOf(scene, catalog, component.name, std::nullopt, component.fields));
        }
    } else {
        for (const scene::Override& each : scene.instances[made.entity.brought->instance].overrides) {
            if (each.entity == kEntity && !each.component.empty()) {
                made.components.push_back(readingOf(scene, catalog, each.component, each.kind, each.fields));
            }
        }
    }
    return made;
}

} // namespace rawframe::authoring
