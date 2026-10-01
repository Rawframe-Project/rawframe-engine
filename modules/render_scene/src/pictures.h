#pragma once

// The textures a scene's materials sample (D309), read together and held,
// and the pictures its World names (the sky's, a reflection probe's, a
// decal's, a grading table: D322, D325, D339, D342, D344), each read alone
// on its first naming, so a game's other textures are never read for one.
// What cannot be read is said so and sampled as white, or is none.

#include "rawframe/composition/participant.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/game_textures/asked.h"
#include "rawframe/game_textures/game_textures.h"
#include "rawframe/render_scene/scene.h"
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

class ScenePictures {
public:
    /// At load: asks for the textures `materials` and `postProcesses`
    /// sample, of those the game declares, decoded on a CPU worker; a
    /// material sampling one it does not declare samples white (its
    /// texture made nought), and is said so at start. A render texture is
    /// drawn, not read (D361).
    [[nodiscard]] result::Status read(composition::ParticipantContext& context,
                                      const world_kest::GameFiles& files,
                                      std::vector<SceneMaterial>& materials,
                                      std::vector<ScenePostProcessMaterial>& postProcesses);
    /// At start: says what load could not ask for, and says through
    /// `emitter` from then on.
    void start(const diagnostics::Emitter& emitter);

    /// Asks for picture `id` on its first naming; nought is none. One the
    /// game does not declare, or that cannot be asked for, is none, and is
    /// said so once.
    void ask(std::uint64_t id) noexcept;
    /// Every picture `frame` names.
    void askAll(const SceneFrame& frame) noexcept;
    /// Takes finished reads and reloads at `tick`.
    void update(std::uint64_t tick) noexcept;
    /// Texture or picture `id` as it is at `tick`; none before it is read.
    [[nodiscard]] std::shared_ptr<const texture::Texture> texture(std::uint64_t id, std::uint64_t tick) const;

    /// The materials' textures sampled and ready; the pictures asked for
    /// and ready.
    [[nodiscard]] std::uint64_t sampled() const noexcept {
        return sampled_;
    }
    [[nodiscard]] std::uint64_t texturesReady() const noexcept;
    [[nodiscard]] std::uint64_t picturesRead() const noexcept {
        return pictures_.read();
    }
    [[nodiscard]] std::uint64_t picturesReady() const noexcept {
        return pictures_.ready();
    }

private:
    diagnostics::Emitter emitter_;
    std::unique_ptr<game_textures::GameTextures> textures_;
    std::uint64_t sampled_ = 0;
    /// The materials naming a texture the game does not declare, and why
    /// the textures could not be asked for.
    std::vector<std::pair<std::string, std::string>> unknownTextures_;
    std::optional<std::string> unreadTextures_;
    /// The pictures asked for.
    game_textures::AskedTextures pictures_{std::nullopt, {}, 0};
};

} // namespace rawframe::render_scene
