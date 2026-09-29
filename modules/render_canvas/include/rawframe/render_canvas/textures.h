#pragma once

// A game's textures read by resource identity (ADR-0025, D255): each
// texture its `texture` lines declare asked of the content store once,
// decoded into its levels on a CPU worker, and held while this lives, so
// nothing a device draws from is evicted under it. The upload to a device
// is `rawframe.render`'s, when it exists.

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

    /// Once a frame, on the owner's thread: takes finished reads and
    /// decodes. Returns the textures that failed since the last call, each
    /// once, by the identity the game gives it, with why.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, result::Error>> update(std::uint64_t tick);

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
