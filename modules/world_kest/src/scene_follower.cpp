#include "scene_follower.h"

#include "rawframe/base/sha256.h"

#include <algorithm>
#include <cstring>
#include <map>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#include <iterator>
#endif

namespace rawframe::world_kest {

namespace {

/// The values of `component` among `values`, if it is one of them.
const std::vector<std::byte>* valuesOf(const SpawnValues& values, schema::ComponentTypeId component) {
    const auto kFound = std::ranges::find(values, component, &SpawnValues::value_type::first);
    return kFound != values.end() ? &kFound->second : nullptr;
}

} // namespace

std::string sceneDigest(std::string_view text) {
    const base::Sha256Digest kDigest = base::sha256(text);
    std::string made;
    for (const std::byte kByte : kDigest) {
        made += "0123456789abcdef"[std::to_integer<unsigned>(kByte) >> 4U];
        made += "0123456789abcdef"[std::to_integer<unsigned>(kByte) & 0xFU];
    }
    return made;
}

result::Result<std::vector<SpawnValues>>
resolvedValues(const GameScenes& scenes, const SceneSpawns& scene, std::span<const world::EntityHandle> entities) {
    std::vector<SpawnValues> made;
    made.reserve(scene.spawns.size());
    for (std::size_t index = 0; index < scene.spawns.size(); ++index) {
        RAWFRAME_TRY_ASSIGN(SpawnValues values, scenes.spawnValues(scene.spawns[index]));
        for (const SceneReference& reference : scene.references) {
            if (reference.spawn != index) {
                continue;
            }
            const auto kValue = std::ranges::find(values, reference.component, &SpawnValues::value_type::first);
            const world::EntityHandle kTarget = entities[reference.target];
            std::memcpy(kValue->second.data() + reference.slot, &kTarget.slot, sizeof kTarget.slot);
            std::memcpy(kValue->second.data() + reference.generation, &kTarget.generation, sizeof kTarget.generation);
        }
        made.push_back(std::move(values));
    }
    return made;
}

SceneFollower SceneFollower::spawned(const GameFiles& files,
                                     std::span<const SceneRange> ranges,
                                     std::span<const std::vector<world::EntityHandle>> made,
                                     std::span<SpawnValues> authored) {
    SceneFollower follower;
    for (const SceneRange& range : ranges) {
        FollowedScene followed{.path = range.path, .ids = range.ids};
        if (const auto kText = files.scene(range.path); kText.has_value()) {
            followed.digest = sceneDigest(*kText);
        }
        for (std::size_t at = 0; at < range.ids.size(); ++at) {
            followed.entities.push_back(made[range.first + at].front());
            followed.authored.push_back(std::move(authored[range.first + at]));
        }
        follower.add(std::move(followed));
    }
    return follower;
}

#if RAWFRAME_FILE_SYSTEM
std::vector<std::optional<std::string>> readScenes(const std::filesystem::path& directory,
                                                   std::span<const std::string> paths) {
    std::vector<std::optional<std::string>> texts;
    for (const std::string& path : paths) {
        std::ifstream file{directory / path, std::ios::binary};
        texts.push_back(file ? std::optional{std::string{std::istreambuf_iterator<char>{file}, {}}} : std::nullopt);
    }
    return texts;
}
#endif

std::vector<std::string> SceneFollower::paths() const {
    std::vector<std::string> made;
    for (const FollowedScene& scene : scenes_) {
        made.push_back(scene.path);
    }
    return made;
}

std::vector<SceneChanges> SceneFollower::follow(world::World& world,
                                                const GameScenes& scenes,
                                                const GameFiles& files,
                                                std::span<const std::optional<std::string>> texts,
                                                std::vector<result::Error>& refusals) {
    std::vector<SceneChanges> changes;
    for (std::size_t at = 0; at < scenes_.size() && at < texts.size(); ++at) {
        FollowedScene& followed = scenes_[at];
        if (!texts[at].has_value()) {
            continue;
        }
        const std::string kDigest = sceneDigest(*texts[at]);
        if (kDigest == followed.digest) {
            continue;
        }
        // Whatever comes of it, this text is not read again.
        followed.digest = kDigest;
        auto now = scenes.namedSceneSpawns(files, *texts[at], followed.path);
        if (!now.has_value()) {
            refusals.push_back(std::move(now).error());
            continue;
        }
        auto applied = apply(world, scenes, followed, *now);
        if (!applied.has_value()) {
            refusals.push_back(std::move(applied).error());
            continue;
        }
        changes.push_back(std::move(*applied));
    }
    return changes;
}

result::Result<SceneChanges>
SceneFollower::apply(world::World& world, const GameScenes& scenes, FollowedScene& followed, const SceneSpawns& now) {
    SceneChanges changes{.path = followed.path};
    // Each entity the scene had, by its id.
    std::map<base::Bits128, std::size_t> before;
    for (std::size_t index = 0; index < followed.ids.size(); ++index) {
        before.emplace(followed.ids[index], index);
    }
    // Every entity now: the one it had, or one made for it, so references
    // can name each before any value is written.
    std::vector<world::EntityHandle> entities(now.spawns.size());
    std::vector<const SpawnValues*> previously(now.spawns.size(), nullptr);
    for (std::size_t index = 0; index < now.spawns.size(); ++index) {
        const auto kHad = before.find(now.ids[index]);
        if (kHad != before.end() && world.alive(followed.entities[kHad->second])) {
            entities[index] = followed.entities[kHad->second];
            previously[index] = &followed.authored[kHad->second];
            before.erase(kHad);
        } else {
            if (kHad != before.end()) {
                before.erase(kHad);
            }
            RAWFRAME_TRY_ASSIGN(entities[index], world.create());
            ++changes.created;
        }
    }
    RAWFRAME_TRY_ASSIGN(std::vector<SpawnValues> authored, resolvedValues(scenes, now, entities));
    for (std::size_t index = 0; index < now.spawns.size(); ++index) {
        const world::EntityHandle kEntity = entities[index];
        const SpawnValues* kBefore = previously[index];
        bool changed = false;
        for (auto& [component, bytes] : authored[index]) {
            RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, world.registry().find(component));
            const std::vector<std::byte>* kWas = kBefore != nullptr ? valuesOf(*kBefore, component) : nullptr;
            void* held = world.getErased(kEntity, kId);
            if (held == nullptr) {
                std::vector<std::byte> copy = bytes;
                RAWFRAME_TRY(world.insertErased(kEntity, kId, copy.data()));
                changed = true;
            } else if (kWas == nullptr || *kWas != bytes) {
                // Only what the scene changed: play's own changes stand.
                std::memcpy(held, bytes.data(), bytes.size());
                changed = true;
            }
        }
        if (kBefore != nullptr) {
            for (const auto& [component, bytes] : *kBefore) {
                if (valuesOf(authored[index], component) != nullptr) {
                    continue;
                }
                RAWFRAME_TRY_ASSIGN(const schema::ComponentRuntimeId kId, world.registry().find(component));
                if (world.hasErased(kEntity, kId)) {
                    RAWFRAME_TRY(world.removeErased(kEntity, kId));
                    changed = true;
                }
            }
        }
        changes.changed += changed && kBefore != nullptr ? 1 : 0;
    }
    // The entities the scene no longer has.
    for (const auto& [id, index] : before) {
        if (world.alive(followed.entities[index])) {
            RAWFRAME_TRY(world.destroy(followed.entities[index]));
            ++changes.destroyed;
        }
    }
    followed.ids = now.ids;
    followed.entities = std::move(entities);
    followed.authored = std::move(authored);
    return changes;
}

} // namespace rawframe::world_kest
