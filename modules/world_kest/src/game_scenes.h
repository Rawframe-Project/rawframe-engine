#pragma once

// A game's scenes read as spawns (ADR-0048): its own scenes, the scenes its
// mods contribute, and its prefabs, each read against the game's components
// and the layouts its program gives them.

#include "rawframe/base/bits128.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_kest/kest_systems.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_kest {

/// The values a spawn gives each of its components, laid out as the
/// program lays each out.
using SpawnValues = std::vector<std::pair<schema::ComponentTypeId, std::vector<std::byte>>>;

/// A scene entity's field that names another: the spawn it is written into,
/// the component and where its entity's slot and generation lie, and the
/// spawn it names.
struct SceneReference {
    std::size_t spawn = 0;
    schema::ComponentTypeId component;
    std::size_t slot = 0;
    std::size_t generation = 0;
    std::size_t target = 0;
};

/// A scene's entities as spawns of one each, and the references among them
/// by their places in that list.
struct SceneSpawns {
    std::vector<GameSpawn> spawns;
    std::vector<SceneReference> references;
    /// Each spawn's entity id in the resolved scene.
    std::vector<base::Bits128> ids;
};

/// Reads scenes for one game: `layouts` in the order of `game.components`.
/// Both must outlive it.
class GameScenes {
public:
    GameScenes(const GameDescription& game, std::span<const kest::TypeLayout> layouts) noexcept
        : game_(game), layouts_(layouts) {
    }

    /// The scene the description names `path`, read with its instances
    /// resolved: its components must be the game's, laid out as the scene
    /// was authored against.
    [[nodiscard]] result::Result<SceneSpawns> sceneSpawns(const GameFiles& files, const std::string& path) const;
    /// The same of a scene's text, named `path` in what refuses it.
    [[nodiscard]] result::Result<SceneSpawns>
    sceneSpawns(const GameFiles& files, std::string_view sceneText, const std::string& path) const;

    /// Appends each scene the description names to `spawns`, its persistent
    /// entities named and its references to `references`.
    [[nodiscard]] result::Status
    addScenes(const GameFiles& files, std::vector<GameSpawn>& spawns, std::vector<SceneReference>& references) const;
    /// Appends each scene a mod contributes (D180): every entity one spawn
    /// holding the point's component and nothing else, so a mod adds values
    /// and never a behavior, a reference, or a persistent name.
    [[nodiscard]] result::Status addModScenes(const GameFiles& files, std::vector<GameSpawn>& spawns) const;
    /// Appends each prefab the description names, as a program spawns it.
    [[nodiscard]] result::Status addPrefabs(const GameFiles& files, std::vector<KestPrefab>& prefabs) const;

    /// The values one spawn gives each of its components.
    [[nodiscard]] result::Result<SpawnValues> spawnValues(const GameSpawn& spawn) const;

private:
    /// Where `field` of `component` holds an entity: a field the description
    /// declares with an `entity` line, laid out as rawframe.world's Entity.
    [[nodiscard]] result::Result<SceneReference>
    referenceIn(std::string_view component, std::string_view field, std::string_view scene) const;
    /// A scene entity that is persistent is named from the scene's identity
    /// and its own id, so the same level in a new World names it the same
    /// (world/persistent.h). A prefab's are not: each copy is named where it
    /// is made.
    [[nodiscard]] result::Status
    namePersistent(const GameFiles& files, const std::string& path, SceneSpawns& scene) const;
    [[nodiscard]] const GameComponent* componentNamed(std::string_view name) const noexcept;

    const GameDescription& game_;
    std::span<const kest::TypeLayout> layouts_;
};

} // namespace rawframe::world_kest
