#include "rawframe/scene/resolve.h"

#include "rawframe/scene/errors.h"

#include <algorithm>
#include <map>
#include <vector>

namespace rawframe::scene {

namespace {

std::unexpected<result::Error> unfit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kSceneDomain, code(SceneError::InstanceInvalid), why);
}

/// A field's new value, or its removal when the value is the default.
void setField(std::vector<SceneField>& fields, const SceneField& value) {
    const auto kAt = std::ranges::lower_bound(fields, value.name, {}, &SceneField::name);
    const bool kDefault = value.value.kind == FieldValue::Kind::False ||
                          (value.value.kind == FieldValue::Kind::Number && value.value.number == "0");
    if (kAt != fields.end() && kAt->name == value.name) {
        if (kDefault) {
            fields.erase(kAt);
        } else {
            kAt->value = value.value;
        }
    } else if (!kDefault) {
        fields.insert(kAt, value);
    }
}

result::Status apply(std::vector<SceneEntity>& entities, const Override& change) {
    const auto kEntity = std::ranges::find(entities, change.entity, &SceneEntity::id);
    if (kEntity == entities.end()) {
        return unfit("an override names an entity its instance does not bring");
    }
    std::vector<SceneComponent>& components = kEntity->components;
    const auto kAt = std::ranges::lower_bound(components, change.component, {}, &SceneComponent::name);
    const bool kHas = kAt != components.end() && kAt->name == change.component;
    switch (change.kind) {
    case Override::Kind::Set:
        if (!kHas) {
            return unfit("an override sets a component its entity has not");
        }
        for (const SceneField& field : change.fields) {
            setField(kAt->fields, field);
        }
        return {};
    case Override::Kind::Add:
        if (kHas) {
            return unfit("an override adds a component its entity has");
        }
        components.insert(kAt, SceneComponent{.name = change.component, .fields = change.fields});
        return {};
    case Override::Kind::Remove:
        if (!kHas) {
            return unfit("an override removes a component its entity has not");
        }
        components.erase(kAt);
        return {};
    }
    return {};
}

result::Result<Scene> resolve(const Scene& scene, const SceneSource& source, std::vector<base::Bits128>& within) {
    if (within.size() > kMaximumInstanceDepth) {
        return unfit("instances reach deeper than sixteen scenes");
    }
    // Each component's mark, from this scene and every source it brings.
    std::map<std::string, std::uint64_t> marks;
    const auto kMark = [&marks](const SchemaMark& mark) -> result::Status {
        const auto [kAt, kNew] = marks.emplace(mark.component, mark.mark);
        if (!kNew && kAt->second != mark.mark) {
            return unfit("two scenes were authored against different layouts of a component");
        }
        return {};
    };
    for (const SchemaMark& mark : scene.schema) {
        RAWFRAME_TRY(kMark(mark));
    }
    Scene made{.schema = {}, .entities = scene.entities, .instances = {}};
    for (const SceneInstance& instance : scene.instances) {
        if (std::ranges::contains(within, instance.scene)) {
            return unfit("a scene instances itself");
        }
        RAWFRAME_TRY_ASSIGN(const Scene kSource, source(instance.scene));
        within.push_back(instance.scene);
        auto resolved = resolve(kSource, source, within);
        within.pop_back();
        if (!resolved.has_value()) {
            return std::unexpected<result::Error>{std::move(resolved).error()};
        }
        for (const SchemaMark& mark : resolved->schema) {
            RAWFRAME_TRY(kMark(mark));
        }
        // The mapping is exactly the source's entities.
        std::vector<base::Bits128> sourceIds;
        for (const SceneEntity& entity : resolved->entities) {
            sourceIds.push_back(entity.id);
        }
        std::ranges::sort(sourceIds);
        if (!std::ranges::equal(sourceIds, instance.entities, {}, {}, &IdentityMapping::source)) {
            return unfit("an instance maps exactly the entities its scene has");
        }
        const auto kRenamed = [&instance](base::Bits128 id) {
            return std::ranges::lower_bound(instance.entities, id, {}, &IdentityMapping::source)->instance;
        };
        std::vector<SceneEntity> brought = std::move(resolved->entities);
        for (SceneEntity& entity : brought) {
            entity.id = kRenamed(entity.id);
            for (SceneComponent& component : entity.components) {
                for (SceneField& field : component.fields) {
                    if (field.value.kind == FieldValue::Kind::Entity) {
                        field.value.entity = kRenamed(field.value.entity);
                    }
                }
            }
        }
        for (const Override& change : instance.overrides) {
            RAWFRAME_TRY(apply(brought, change));
        }
        std::ranges::move(brought, std::back_inserter(made.entities));
    }
    if (made.entities.size() > kMaximumEntities) {
        return unfit("a scene brings more than 65,536 entities");
    }
    // The schema is what the entities now use.
    for (const SceneEntity& entity : made.entities) {
        for (const SceneComponent& component : entity.components) {
            if (!std::ranges::contains(made.schema, component.name, &SchemaMark::component)) {
                made.schema.push_back(SchemaMark{.component = component.name, .mark = marks.at(component.name)});
            }
        }
    }
    std::ranges::sort(made.schema, {}, &SchemaMark::component);
    return made;
}

} // namespace

result::Result<Scene> resolveInstances(const Scene& scene, const SceneSource& source) {
    std::vector<base::Bits128> within;
    RAWFRAME_TRY_ASSIGN(Scene made, resolve(scene, source, within));
    // Still a scene in its form: the rules hold after every rename and
    // change.
    RAWFRAME_TRY(writeScene(made));
    return made;
}

} // namespace rawframe::scene
