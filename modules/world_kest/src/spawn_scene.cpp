#include "rawframe/world_kest/spawn_scene.h"

#include "rawframe/document/json.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_kest/layouts.h"

#include <algorithm>

namespace rawframe::world_kest {

namespace {

/// A spawn value as a scene writes it: none at its default.
result::Result<std::optional<scene::FieldValue>> sceneValue(std::string_view text) {
    if (text == "true") {
        return scene::FieldValue{.kind = scene::FieldValue::Kind::True};
    }
    if (text == "false") {
        return std::nullopt;
    }
    auto number = document::parse(text);
    if (!number.has_value() || number->kind() != document::Value::Kind::Number) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldKestDomain,
                            code(WorldKestError::BadGameLine),
                            "a spawn value is a number, true, or false");
    }
    std::string written = document::write(*number);
    written.pop_back();
    if (written == "0") {
        return std::nullopt;
    }
    return scene::FieldValue{.kind = scene::FieldValue::Kind::Number, .number = std::move(written)};
}

} // namespace

result::Result<scene::Scene>
spawnsAsScene(const GameDescription& game, const kest::Program& program, const std::function<base::Bits128()>& nextId) {
    scene::Scene made;
    for (const GameSpawn& spawn : game.spawns) {
        scene::SceneEntity entity;
        for (const GameSpawnComponent& part : spawn.components) {
            const auto kComponent = std::ranges::find(game.components, part.component, &GameComponent::name);
            RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kLayout, componentLayout(game, program, *kComponent));
            if (!std::ranges::contains(made.schema, part.component, &scene::SchemaMark::component)) {
                made.schema.push_back(scene::SchemaMark{.component = part.component, .mark = kLayout.mark});
            }
            scene::SceneComponent component{.name = part.component, .fields = {}};
            for (const GameFieldValue& field : part.fields) {
                // A collision class, by name, is its identity in a scene.
                std::string value = field.value;
                const auto kClass = std::ranges::find(game.collision.classes, value, &GameCollisionClass::name);
                if (field.field == "collisionClass" && kClass != game.collision.classes.end()) {
                    value = std::to_string(kClass->id);
                }
                RAWFRAME_TRY_ASSIGN(const std::optional<scene::FieldValue> kValue, sceneValue(value));
                if (kValue.has_value()) {
                    component.fields.push_back(scene::SceneField{.name = field.field, .value = *kValue});
                }
            }
            std::ranges::sort(component.fields, {}, &scene::SceneField::name);
            entity.components.push_back(std::move(component));
        }
        std::ranges::sort(entity.components, {}, &scene::SceneComponent::name);
        for (std::uint32_t count = 0; count < spawn.count; ++count) {
            entity.id = nextId();
            made.entities.push_back(entity);
        }
    }
    std::ranges::sort(made.schema, {}, &scene::SchemaMark::component);
    return made;
}

} // namespace rawframe::world_kest
