#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A decal as the lit pass reads it (std430, D339): the eye-relative World
/// into its box, its tint with how much it covers, and its layer of the
/// atlas, below nought for none this frame.
struct DecalBlock {
    Matrix4 toBox{};
    std::array<float, 4> color{};
    std::array<float, 4> layer{};
};
static_assert(sizeof(DecalBlock) == 96, "the scene's shaders read a decal as 96 bytes");

/// The decals' textures as the lit pass samples them (ADR-0051, D339): one
/// array texture kept from frame to frame, its layers square with every
/// mip. A decal's texture is drawn into a layer the first frame it is drawn
/// and held, each mip from the texture's own nearest that size, a few
/// textures a frame; it keeps its layer while it is drawn, and the layer
/// least lately drawn is taken for another. A decal whose texture has no
/// layer this frame is left out.
class DecalAtlas {
public:
    /// A layer's side in texels, its mips, and the layers.
    static constexpr std::uint32_t kSide = 256;
    static constexpr std::uint32_t kLevels = 9;
    static constexpr std::uint32_t kLayers = 32;
    /// The textures drawn into layers in one frame, at most.
    static constexpr std::size_t kMostFills = 4;

    explicit DecalAtlas(mrhiDevice* native) noexcept;
    DecalAtlas(const DecalAtlas&) = delete;
    DecalAtlas& operator=(const DecalAtlas&) = delete;
    ~DecalAtlas();

    /// The array texture made.
    result::Status make();

    /// Joins the open frame: the atlas imported; each of `frame`'s decals
    /// given its texture's layer when `made`, its pipeline is made, the new
    /// ones to be drawn from `textures`; its blocks declared, and what the
    /// upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const render::DeviceTextures& textures,
                           std::vector<mrhiAccess>& writes);

    /// The atlas joined to the open frame, none for a frame without
    /// decals; its decals' blocks, and how many it draws.
    [[nodiscard]] mrhiResourceId atlas() const noexcept;
    [[nodiscard]] mrhiResourceId blocks() const noexcept;
    [[nodiscard]] std::uint64_t blockBytes() const noexcept;
    [[nodiscard]] std::size_t drawn() const noexcept;

    /// Its passes: each new texture drawn into its layer's mips.
    result::Status addPasses();

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines);

    /// The frame ended: a layer drawn this frame holds its texture only
    /// when the frame was submitted.
    void ended(bool submitted) noexcept;

private:
    struct Layer {
        std::uint64_t texture = 0;
        /// The frame it was last drawn in.
        std::uint64_t used = 0;
    };
    struct Fill {
        std::uint32_t layer = 0;
        mrhiResourceId source{};
        std::array<mrhiPassId, kLevels> passes{};
    };

    mrhiDevice* native_ = nullptr;
    mrhiTextureId texture_{};
    mrhiResourceId atlas_{};
    std::array<Layer, kLayers> layers_{};
    std::vector<Fill> fills_;
    std::vector<DecalBlock> blocks_;
    mrhiResourceId blocksResource_{};
    std::size_t drawn_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace rawframe::render_scene_gpu
