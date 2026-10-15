#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/world/layer.h"
#include "scene_state.h"

#include <algorithm>
#include <cstring>

namespace rawframe::render_scene {

using particles::BeamInstance;
using particles::EmitterInstance;
using particles::ParticleEmitter;
using particles::RibbonInstance;
using particles::TrailInstance;

void Scene::State::posePalette(const world_animation::AnimationQueries* poses, ModelInstance& instance) {
    if (poses == nullptr) {
        return;
    }
    const auto kMesh = meshes.find(instance.model.mesh);
    if (kMesh == meshes.end() || kMesh->second.mesh->skin.joints.empty()) {
        return;
    }
    const animation::Pose* kPose = poses->pose(instance.entity);
    if (kPose == nullptr) {
        return;
    }
    const std::size_t kFirst = posed.size();
    if (skinning.pose(instance.model.mesh, kMesh->second.mesh->skin, poses->bones(instance.entity), *kPose, posed)) {
        instance.palette = static_cast<std::uint32_t>(kFirst);
        instance.joints = static_cast<std::uint32_t>(posed.size() - kFirst);
    }
}

namespace {

/// Each of the game's trail or beam components copied out of `world`,
/// where its entity's pose puts it (D354).
template <typename Ribbon>
void extractRibbons(world::World& world,
                    std::vector<world::ColumnQuery>& queries,
                    const std::optional<schema::ComponentRuntimeId>& pose,
                    std::vector<RibbonInstance<Ribbon>>& made) {
    made.clear();
    for (std::size_t component = 0; component < queries.size(); ++component) {
        queries[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                RibbonInstance<Ribbon> instance{.entity = chunk.entities[row],
                                                .component = static_cast<std::uint32_t>(component)};
                std::memcpy(&instance.ribbon, chunk.columns[0] + (row * sizeof(Ribbon)), sizeof(Ribbon));
                if (pose) {
                    if (const auto* placed =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *pose))) {
                        instance.position = {placed->x, placed->y, placed->z};
                    }
                }
                made.push_back(instance);
            }
        });
    }
}

} // namespace

void Scene::hideLayers(const std::vector<std::uint32_t>& layers) {
    state_->hiddenLayers = layers;
}

std::uint64_t Scene::modelsHidden() const noexcept {
    return state_->modelsHidden;
}

void Scene::extract(world::World& world, const world_animation::AnimationQueries* poses, const HandsInWorld* hands) {
    State& state = *state_;
    state.extracted.clear();
    state.posed.clear();
    for (std::size_t component = 0; component < state.models.size(); ++component) {
        state.models[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                ModelInstance instance{.entity = chunk.entities[row],
                                       .component = static_cast<std::uint32_t>(component)};
                // Left out where an editor's preview hides its layer (D791).
                if (!state.hiddenLayers.empty() && state.layer.has_value()) {
                    if (const auto* layer =
                            static_cast<const world::Layer*>(world.getErased(instance.entity, *state.layer));
                        layer != nullptr && std::ranges::contains(state.hiddenLayers, layer->layer)) {
                        ++state.modelsHidden;
                        continue;
                    }
                }
                std::memcpy(&instance.model, chunk.columns[0] + (row * sizeof(Model)), sizeof(Model));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                // Drawn where it is placed, if it is (D665).
                for (const schema::ComponentRuntimeId kPlacement : state.placements) {
                    if (const auto* placed = static_cast<const Placed*>(world.getErased(instance.entity, kPlacement));
                        placed != nullptr && placed->placed) {
                        instance.position = {placed->x, placed->y, placed->z};
                        instance.placed = true;
                        break;
                    }
                }
                // Drawn where its hand is this frame, if it is (D676).
                for (const schema::ComponentRuntimeId kTracking : state.tracking) {
                    const auto* tracked = static_cast<const Tracked*>(world.getErased(instance.entity, kTracking));
                    if (tracked == nullptr || hands == nullptr || tracked->hand == 0 || tracked->hand > hands->size()) {
                        continue;
                    }
                    if (const auto& kHand = (*hands)[tracked->hand - 1]; kHand.has_value()) {
                        instance.position = {
                            (*kHand)[0] + tracked->x, (*kHand)[1] + tracked->y, (*kHand)[2] + tracked->z};
                        instance.placed = false;
                        instance.tracked = true;
                        break;
                    }
                }
                // Drawn `lift` along the pose's own up (D514).
                const auto [kX, kY, kZ, kW] = instance.rotation;
                const double kLift = instance.model.lift;
                instance.position[0] += kLift * 2.0 * ((kX * kY) - (kW * kZ));
                instance.position[1] += kLift * (1.0 - (2.0 * ((kX * kX) + (kZ * kZ))));
                instance.position[2] += kLift * 2.0 * ((kY * kZ) + (kW * kX));
                state.posePalette(poses, instance);
                state.extracted.push_back(instance);
            }
        });
    }
    // The first set, if any: a game declares at most one of each.
    const auto kFirstOf = [&world]<typename T>(std::optional<world::ColumnQuery>& query, std::optional<T>& into) {
        into.reset();
        if (!query.has_value()) {
            return;
        }
        query->forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            if (!into.has_value() && !chunk.entities.empty()) {
                T value;
                std::memcpy(&value, chunk.columns[0], sizeof(T));
                into = value;
            }
        });
    };
    kFirstOf(state.sun, state.sunNow);
    kFirstOf(state.sky, state.skyNow);
    state.punctual.clear();
    for (auto& [query, spot] : state.lightQueries) {
        const bool kSpot = spot;
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                LightInstance instance{.entity = chunk.entities[row], .spot = kSpot};
                if (kSpot) {
                    std::memcpy(&instance.light, chunk.columns[0] + (row * sizeof(SpotLight)), sizeof(SpotLight));
                } else {
                    PointLight point;
                    std::memcpy(&point, chunk.columns[0] + (row * sizeof(PointLight)), sizeof(PointLight));
                    instance.light = SpotLight{
                        .lumens = point.lumens, .range = point.range, .color = point.color, .shadows = point.shadows};
                }
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                state.punctual.push_back(instance);
            }
        });
    }
    state.probes.clear();
    for (world::ColumnQuery& query : state.probeQueries) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                ProbeInstance instance{.entity = chunk.entities[row]};
                std::memcpy(
                    &instance.probe, chunk.columns[0] + (row * sizeof(ReflectionProbe)), sizeof(ReflectionProbe));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                    }
                }
                state.probes.push_back(instance);
            }
        });
    }
    state.emitters.clear();
    for (std::size_t component = 0; component < state.emitterQueries.size(); ++component) {
        state.emitterQueries[component].forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                EmitterInstance instance{.entity = chunk.entities[row],
                                         .component = static_cast<std::uint32_t>(component)};
                std::memcpy(
                    &instance.emitter, chunk.columns[0] + (row * sizeof(ParticleEmitter)), sizeof(ParticleEmitter));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.way = turnOf({pose->qx, pose->qy, pose->qz, pose->qw})[1];
                    }
                }
                state.emitters.push_back(instance);
            }
        });
    }
    extractRibbons(world, state.trailQueries, state.pose, state.trails);
    extractRibbons(world, state.beamQueries, state.pose, state.beams);
    state.decals.clear();
    for (world::ColumnQuery& query : state.decalQueries) {
        query.forEachChunk(world, [&](const world::ColumnChunk& chunk) {
            for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                DecalInstance instance{.entity = chunk.entities[row]};
                std::memcpy(&instance.decal, chunk.columns[0] + (row * sizeof(Decal)), sizeof(Decal));
                if (state.pose) {
                    if (const auto* pose =
                            static_cast<const physics3d::Pose3D*>(world.getErased(instance.entity, *state.pose))) {
                        instance.position = {pose->x, pose->y, pose->z};
                        instance.rotation = {pose->qx, pose->qy, pose->qz, pose->qw};
                    }
                }
                state.decals.push_back(instance);
            }
        });
    }
}

std::span<const Matrix> Scene::extractedPalette() const noexcept {
    return state_->posed;
}

std::span<const ModelInstance> Scene::extracted() const noexcept {
    return state_->extracted;
}

std::span<const LightInstance> Scene::extractedLights() const noexcept {
    return state_->punctual;
}

std::span<const ProbeInstance> Scene::extractedProbes() const noexcept {
    return state_->probes;
}

std::span<const EmitterInstance> Scene::extractedEmitters() const noexcept {
    return state_->emitters;
}

std::span<const TrailInstance> Scene::extractedTrails() const noexcept {
    return state_->trails;
}

std::span<const BeamInstance> Scene::extractedBeams() const noexcept {
    return state_->beams;
}

std::span<const DecalInstance> Scene::extractedDecals() const noexcept {
    return state_->decals;
}

} // namespace rawframe::render_scene
