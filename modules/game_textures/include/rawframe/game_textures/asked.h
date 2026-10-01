#pragma once

// A game's textures read one by one as they are first asked for (D322,
// D378): a picture a World names (the sky's, a decal's) or an image a UI
// shows, each read alone, so a game's other textures are never read for
// one. What the game does not declare, or what cannot be asked for, has
// none; the asker hears why once.

#include "rawframe/game_textures/game_textures.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::game_textures {

/// What reading a texture needs: the content store and the CPU worker that
/// decodes, as the asker's owner within its scope.
struct TextureReading {
    content::ContentStore* store = nullptr;
    execution::Executor* cpu = nullptr;
    execution::OwnerId owner;
    execution::CancellationScope* scope = nullptr;
    const execution::MonotonicSource* clock = nullptr;
};

class AskedTextures {
public:
    /// Over the game's `declared` textures, read through `reading` within
    /// `budgetBytes` of decoded levels each; without a reading every one
    /// asked for has none.
    AskedTextures(std::optional<TextureReading> reading,
                  std::vector<world_kest::GameTextureResource> declared,
                  std::uint64_t budgetBytes);

    /// Asks for texture `id` on its first asking; nought is none. Why that
    /// first asking found none: `NotFound` for one the game does not
    /// declare, else why it could not be asked for. Nothing on a later
    /// asking.
    [[nodiscard]] std::optional<result::Error> ask(std::uint64_t id);
    /// Takes finished reads at `tick`: those that failed since, each once,
    /// with why.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, result::Error>> update(std::uint64_t tick);
    /// Texture `id` as it is at `tick`; none before it is read, and for one
    /// never asked for.
    [[nodiscard]] std::shared_ptr<const texture::Texture> texture(std::uint64_t id, std::uint64_t tick) const;

    /// Those asked for that are being read or were; those of them ready.
    [[nodiscard]] std::uint64_t read() const noexcept {
        return read_;
    }
    [[nodiscard]] std::uint64_t ready() const noexcept;

private:
    std::optional<TextureReading> reading_;
    std::vector<world_kest::GameTextureResource> declared_;
    std::uint64_t budgetBytes_ = 0;
    /// Every texture asked for; none for one that has none.
    std::map<std::uint64_t, std::unique_ptr<GameTextures>> asked_;
    std::uint64_t read_ = 0;
};

} // namespace rawframe::game_textures
