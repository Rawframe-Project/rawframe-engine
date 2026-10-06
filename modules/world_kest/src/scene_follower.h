#pragma once

// A game's scenes followed into a running World (D410): ADR-0032's live
// World is a projection of the documents, so an edit to a scene, by an
// authoring session or an editor, reaches the World by the document. Each
// scene's entities are known by their id in it, with the entity spawned for
// each and the values it was authored with; when a scene's file changes,
// the difference between what was authored and what is now is applied:
// entities made and destroyed, components added and removed, and a
// component's value written only where the scene changed it, so what play
// did to the rest stands.

#include "game_scenes.h"
#include "rawframe/base/platform.h"
#include "rawframe/result/result.h"
#include "rawframe/world/entity.h"
#include "rawframe/world/world.h"
#include "rawframe/world_kest/game_files.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::world_kest {

/// One scene as the World holds it.
struct FollowedScene {
    std::string path;
    /// The digest of the text it was last read from.
    std::string digest;
    std::vector<base::Bits128> ids;
    std::vector<world::EntityHandle> entities;
    /// Each entity's values as the scene authored them, references written.
    std::vector<SpawnValues> authored;
};

/// What following one scene did.
struct SceneChanges {
    std::string path;
    std::size_t created = 0;
    std::size_t destroyed = 0;
    /// Entities whose components were added, removed, or written.
    std::size_t changed = 0;
};

class SceneFollower {
public:
    /// The game's scenes as just spawned: `ranges` where each lies among the
    /// spawns, `made` each spawn's entities, `authored` each spawn's values
    /// with references written (moved from).
    [[nodiscard]] static SceneFollower spawned(const GameFiles& files,
                                               std::span<const SceneRange> ranges,
                                               std::span<const std::vector<world::EntityHandle>> made,
                                               std::span<SpawnValues> authored);

    void add(FollowedScene scene) {
        scenes_.push_back(std::move(scene));
    }
    [[nodiscard]] bool empty() const noexcept {
        return scenes_.empty();
    }

    /// Each followed scene's path, in order.
    [[nodiscard]] std::vector<std::string> paths() const;

    /// Applies to `world` what changed in each scene whose text `texts`
    /// gives now (in the order of `paths`, none for one that cannot be
    /// read), between ticks with its structure unlocked. A scene whose text
    /// is unchanged is skipped; one that no longer reads, or does not apply,
    /// is refused whole, the error in `refusals`, and is read again only
    /// once its text changes.
    [[nodiscard]] std::vector<SceneChanges> follow(world::World& world,
                                                   const GameScenes& scenes,
                                                   const GameFiles& files,
                                                   std::span<const std::optional<std::string>> texts,
                                                   std::vector<result::Error>& refusals);

private:
    [[nodiscard]] result::Result<SceneChanges>
    apply(world::World& world, const GameScenes& scenes, FollowedScene& followed, const SceneSpawns& now);

    std::vector<FollowedScene> scenes_;
};

#if RAWFRAME_FILE_SYSTEM
/// Each scene's text now, read from under `directory`; none for one that
/// cannot be read.
[[nodiscard]] std::vector<std::optional<std::string>> readScenes(const std::filesystem::path& directory,
                                                                 std::span<const std::string> paths);
#endif

/// The digest of a scene's text, to tell when it changed.
[[nodiscard]] std::string sceneDigest(std::string_view text);

/// Each spawn's values with its references written: `entities[i]` is the
/// entity spawn `i` makes, and a reference names its target's.
[[nodiscard]] result::Result<std::vector<SpawnValues>>
resolvedValues(const GameScenes& scenes, const SceneSpawns& scene, std::span<const world::EntityHandle> entities);

} // namespace rawframe::world_kest
