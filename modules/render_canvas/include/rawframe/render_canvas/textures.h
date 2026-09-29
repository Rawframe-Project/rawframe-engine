#pragma once

// A game's textures read by resource identity (ADR-0025, D255): each
// texture its `texture` lines declare asked of the content store once,
// decoded into its levels on a CPU worker, and held while this lives, so
// nothing a device draws from is evicted under it. A texture recooked while
// this lives follows the store's reload to its new revision (D262). The
// upload to a device is `rawframe.render`'s, when it exists.

#include "rawframe/assets/assets.h"
#include "rawframe/content/store.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"
#include "rawframe/world_kest/game_files.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace rawframe::render_canvas {

/// The representation a cooked texture has, admitted wherever textures are
/// read.
[[nodiscard]] std::vector<content::AdmittedRepresentation> textureRepresentations();

/// How the declared textures stand.
struct TextureCounts {
    std::size_t ready = 0;
    std::size_t pending = 0;
    std::size_t failed = 0;
    /// The decoded levels' bytes held.
    std::uint64_t bytes = 0;
};

/// What changed in one update, each texture by the identity the game gives
/// it.
struct TextureChanges {
    /// Failed since the last update, each once, with why.
    std::vector<std::pair<std::uint64_t, result::Error>> failed;
    /// Replaced by a new revision the store published.
    std::vector<std::uint64_t> reloaded;
    /// Whose new revision could not be made: the old one stays, with why.
    std::vector<std::pair<std::uint64_t, result::Error>> notReloaded;
    /// Every declared texture ready: true once, in the update it became so.
    bool read = false;
};

class CanvasTextures {
public:
    /// Asks for every texture of `declared` at once, decoding on `cpu` as
    /// `owner`, within `budgetBytes` of decoded levels. Refuses what a
    /// request refuses: a texture the catalog does not hold, or holds as
    /// another type.
    [[nodiscard]] static result::Result<std::unique_ptr<CanvasTextures>>
    create(content::ContentStore& store,
           execution::Executor& cpu,
           execution::OwnerId owner,
           execution::CancellationScope& parent,
           const execution::MonotonicSource& clock,
           std::vector<world_kest::GameTextureResource> declared,
           std::uint64_t budgetBytes);

    CanvasTextures(const CanvasTextures&) = delete;
    CanvasTextures& operator=(const CanvasTextures&) = delete;
    ~CanvasTextures();

    /// Once a frame, on the owner's thread: takes finished reads, decodes,
    /// and reloads, and says what changed.
    [[nodiscard]] TextureChanges update(std::uint64_t tick);

    /// The texture the game names `id`, decoded, once ready; none before,
    /// for one that failed, or for one the game does not declare.
    [[nodiscard]] std::shared_ptr<const texture::Texture> texture(std::uint64_t id, std::uint64_t tick) const;

    [[nodiscard]] TextureCounts counts() const noexcept;

    struct State;

private:
    explicit CanvasTextures(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::render_canvas
