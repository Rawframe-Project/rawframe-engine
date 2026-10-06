#include "game_plans.h"

#include "rawframe/world_kest/errors.h"

#include <algorithm>

namespace rawframe::world_kest {

physics::CollisionDocument collisionDocumentOf(const GameDescription& game) {
    physics::CollisionDocument document;
    const auto kId = [&game](const std::string& name) {
        return std::ranges::find(game.collision.classes, name, &GameCollisionClass::name)->id;
    };
    for (const GameCollisionClass& declared : game.collision.classes) {
        document.classes.push_back({.id = declared.id, .name = declared.name});
    }
    for (const GameCollisionRule& rule : game.collision.rules) {
        document.rules.push_back({.first = kId(rule.first), .second = kId(rule.second), .rule = rule.rule});
    }
    document.fallback = game.collision.fallback;
    return document;
}

physics2d::Physics2DSettings physics2dSettingsOf(const GameDescription& game) {
    return physics2d::Physics2DSettings{.gravityX = game.physics->gravityX,
                                        .gravityY = game.physics->gravityY,
                                        .substeps = game.physics->substeps,
                                        .collision = collisionDocumentOf(game)};
}

physics3d::Physics3DSettings physics3dSettingsOf(const GameDescription& game,
                                                 const std::vector<physics3d::BodyMesh>& meshes) {
    return physics3d::Physics3DSettings{.gravityX = game.physics->gravityX,
                                        .gravityY = game.physics->gravityY,
                                        .gravityZ = game.physics->gravityZ,
                                        .substeps = game.physics->substeps,
                                        .meshes = meshes,
                                        .collision = collisionDocumentOf(game)};
}

result::Result<std::optional<world_replication::InterestSettings>>
interestOf(const GameDescription& game, std::span<const kest::TypeLayout> layouts) {
    if (!game.interest.has_value()) {
        return std::optional<world_replication::InterestSettings>{};
    }
    const GameInterest& interest = *game.interest;
    // parseGame has checked the name.
    const auto kComponent = std::ranges::find(game.components, interest.component, &GameComponent::name);
    const kest::TypeLayout& layout = layouts[static_cast<std::size_t>(kComponent - game.components.begin())];
    world_replication::InterestSettings settings{
        .position = kComponent->id, .axes = {}, .radius = interest.radius, .leaveRadius = interest.radius * 1.125};
    for (const std::string& axis : interest.axes) {
        const auto kField = std::ranges::find(layout.fields, axis, &kest::Field::name);
        if (kField == layout.fields.end() ||
            (kField->kind != kest::FieldKind::F32 && kField->kind != kest::FieldKind::F64)) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::InvalidArgument,
                             kWorldKestDomain,
                             code(WorldKestError::UnknownName),
                             "an interest line names a field that is not a floating-point number of its component")
                    .error()
                    .withContext("field", axis)};
        }
        settings.axes.push_back(world_replication::WireField{.offset = kField->offset,
                                                             .kind = kField->kind == kest::FieldKind::F32
                                                                         ? world_replication::WireKind::F32
                                                                         : world_replication::WireKind::F64});
    }
    return std::optional{std::move(settings)};
}

} // namespace rawframe::world_kest
