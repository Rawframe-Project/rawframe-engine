#pragma once

// What a scene reads as it loads: its `scene.*` keys, and
// the game's materials from its cooked content.

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/participant.h"
#include "rawframe/content/store.h"
#include "rawframe/material/material.h"
#include "rawframe/render_scene/materials.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game_files.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::render_scene {

/// A scene's `scene.*` keys, read and bounded.
struct SceneConfiguration {
    /// The client whose World is drawn, where configuration names one
    /// rather than the process's own players.
    std::optional<std::size_t> client;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    ShadowSettings shadows;
    LightShadowSettings lightShadows;
    AntiAliasing antiAliasing = AntiAliasing::Taa;
    std::uint32_t multisamples = kDefaultMultisamples;
    material::Quality quality = material::Quality::High;
    /// Each local player's view drawn at most at this many hundredths of
    /// its region's pixels each way, and at least at the least.
    std::uint32_t renderScalePercent = 100;
    std::uint32_t leastRenderScalePercent = 100;
};

/// Reads every `scene.*` key, refusing one out of its bounds.
[[nodiscard]] result::Result<SceneConfiguration> readConfiguration(const composition::Configuration& configuration);

/// The game's materials at `quality` read from `store`, surfaces into
/// `materials` and post processes into `postProcesses`; one that cannot be
/// read is drawn as none, and its path and why put in `unread`. What
/// `readGameMaterials` reads once its representations are admitted, and
/// what a reload reads again.
void readMaterials(content::ContentStore& store,
                   const world_kest::GameFiles& files,
                   material::Quality quality,
                   Programs& programs,
                   std::vector<SceneMaterial>& materials,
                   std::vector<ScenePostProcessMaterial>& postProcesses,
                   std::vector<std::pair<std::string, std::string>>& unread);

/// Each of the game's materials' cooked revision in the catalog `store`
/// reads in now, in the game's order; an empty digest for one it does not
/// hold.
[[nodiscard]] std::vector<content::ContentDigest> materialRevisions(const content::ContentStore& store,
                                                                    const world_kest::GameFiles& files);

/// The game's materials at `quality`, surfaces into `materials` and post
/// processes into `postProcesses`, where its content is held; one that
/// cannot be read is drawn as none, and its path and why put in `unread`
/// for the scene to say when it starts.
[[nodiscard]] result::Status readGameMaterials(composition::ParticipantContext& context,
                                               const world_kest::GameFiles& files,
                                               material::Quality quality,
                                               Programs& programs,
                                               std::vector<SceneMaterial>& materials,
                                               std::vector<ScenePostProcessMaterial>& postProcesses,
                                               std::vector<std::pair<std::string, std::string>>& unread);

} // namespace rawframe::render_scene
