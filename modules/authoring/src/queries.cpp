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

ComponentReading readingOf(const ComponentCatalog& catalog,
                           const std::string& name,
                           std::optional<scene::Override::Kind> patch,
                           const std::vector<scene::SceneField>& fields) {
    const ComponentSchema* schema = catalog.findNamed(name);
    ComponentReading made{.component = schema != nullptr ? std::optional{schema->id} : std::nullopt,
                          .name = name,
                          .patch = patch,
                          .fields = {}};
    for (const scene::SceneField& field : fields) {
        std::optional<FieldKind> kind;
        if (schema != nullptr) {
            const auto kFound = std::ranges::find(schema->fields, field.name, &FieldSchema::name);
            if (kFound != schema->fields.end()) {
                kind = kFound->kind;
            }
        }
        made.fields.push_back(FieldReading{.name = field.name, .kind = kind, .value = field.value});
    }
    return made;
}

} // namespace

result::Result<Answer> answer(const scene::Scene& scene, const Query& query, const ComponentCatalog& catalog) {
    std::vector<EntityEntry> entries = entriesOf(scene);
    if (std::holds_alternative<ListEntities>(query)) {
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
            made.components.push_back(readingOf(catalog, component.name, std::nullopt, component.fields));
        }
    } else {
        for (const scene::Override& each : scene.instances[made.entity.brought->instance].overrides) {
            if (each.entity == kEntity && !each.component.empty()) {
                made.components.push_back(readingOf(catalog, each.component, each.kind, each.fields));
            }
        }
    }
    return made;
}

} // namespace rawframe::authoring
