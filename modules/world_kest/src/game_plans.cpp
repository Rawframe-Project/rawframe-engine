#include "game_plans.h"

#include "physics_facts.h"
#include "rawframe/physics/motion.h"
#include "rawframe/world_kest/errors.h"
#include "rawframe/world_replication/perception.h"

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

result::Result<std::vector<schema::ComponentTypeId>>
replicatedAmong(const GameDescription& game, std::span<const std::string> names, std::string_view why) {
    std::vector<schema::ComponentTypeId> made;
    for (const std::string& name : names) {
        const auto kComponent = std::ranges::find(game.components, name, &GameComponent::name);
        if (!std::ranges::contains(game.replicated, name) || kComponent == game.components.end()) {
            return std::unexpected<result::Error>{
                result::fail(
                    result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::BadGameLine), why)
                    .error()
                    .withContext("name", name)};
        }
        made.push_back(kComponent->id);
    }
    return made;
}

result::Result<PredictionPlan> predictionOf(const GameDescription& game,
                                            const world_replication::ReplicationTable& table,
                                            std::span<const kest::TypeLayout> layouts,
                                            const std::vector<physics3d::BodyMesh>& meshes) {
    PredictionPlan plan;
    if (game.predicted.empty()) {
        return plan;
    }
    const auto kIn = [](const std::vector<std::string>& names, const std::string& name) {
        return std::ranges::find(names, name) != names.end();
    };
    const auto kRefuse = [](std::string_view why, std::string_view subject) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::InvalidArgument, kWorldKestDomain, code(WorldKestError::BadGameLine), why)
                .error()
                .withContext("name", subject)};
    };
    // parseGame has checked every name.
    const auto kId = [&game](const std::string& name) {
        return std::ranges::find(game.components, name, &GameComponent::name)->id;
    };
    if (game.input.empty()) {
        return kRefuse("a game that predicts needs input to predict from", "input");
    }
    // An entity a replicated value names is the mirror's on a client and
    // the predictor's own there: never the same bits to compare.
    const auto kNamesEntities = [&](const std::string& name) {
        const schema::ComponentTypeId kComponent = kId(name);
        return std::ranges::any_of(table.components, [kComponent](const world_replication::ComponentCodec& codec) {
            return codec.component == kComponent && codec.namesEntities();
        });
    };
    for (const std::string& name : game.predicted) {
        if (!kIn(game.player, name) || !kIn(game.replicated, name)) {
            return kRefuse("a predicted component is one of the player's and replicates", name);
        }
        if (kNamesEntities(name)) {
            return kRefuse("a predicted component names no entity", name);
        }
        plan.predicted.push_back(kId(name));
    }
    for (const GameSystem& system : game.systems) {
        if (!system.predicted) {
            continue;
        }
        if (!system.randomStreams.empty()) {
            return kRefuse("a predicted system draws from no World stream: a client does not have its state",
                           system.identity);
        }
        for (const GameColumn& column : system.columns) {
            // The moment a player saw is the server's to know: a client
            // stepping its own command does not have it.
            if (column.component == world_replication::Perception::kComponentName) {
                return kRefuse("a predicted system reads no Perception", system.identity);
            }
            if (column.access == world::Access::Write && kIn(game.replicated, column.component) &&
                !kIn(game.predicted, column.component)) {
                return kRefuse("a predicted system writes no replicated component that is not predicted",
                               system.identity);
            }
        }
    }
    // A predicting client steps its player's body among the level's static
    // bodies; everything that moves besides it is the server's.
    if (game.physics.has_value()) {
        if (game.physics->dimensions == 3) {
            plan.physics3d = physics3dSettingsOf(game, meshes);
        } else {
            plan.physics2d = physics2dSettingsOf(game);
        }
        const PhysicsFacts kFacts = physicsFacts(game.physics->dimensions);
        const GameScenes kScenes{game, layouts};
        for (const GameSpawn& spawn : game.spawns) {
            RAWFRAME_TRY_ASSIGN(SpawnValues values, kScenes.spawnValues(spawn));
            const auto kBody = std::ranges::find(values, kFacts.body, &SpawnValues::value_type::first);
            if (kBody == values.end() || static_cast<std::uint8_t>(kBody->second[kFacts.motion]) !=
                                             static_cast<std::uint8_t>(physics::Motion::Static)) {
                continue;
            }
            for (std::uint32_t made = 0; made < spawn.count; ++made) {
                plan.level.push_back(values);
            }
        }
    }
    for (const std::string& name : game.nearby) {
        if (!kIn(game.replicated, name)) {
            return kRefuse("a nearby component replicates", name);
        }
        if (kNamesEntities(name)) {
            return kRefuse("a nearby component names no entity", name);
        }
        plan.nearby.push_back(kId(name));
    }
    return plan;
}

} // namespace rawframe::world_kest
