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

/// A decal as the lit pass reads it (std430, D339, D342): the
/// eye-relative World into its box, its tint with how much it covers, and
/// its layer of the colors' atlas, below nought for none this frame; its
/// layer of the normals' atlas, below nought for none; and the roughness
/// it lays, nought for the surface's.
struct DecalBlock {
    Matrix4 toBox{};
    std::array<float, 4> color{};
    std::array<float, 4> layer{};
};
static_assert(sizeof(DecalBlock) == 96, "the scene's shaders read a decal as 96 bytes");

/// The decals' textures as the lit pass samples them (ADR-0051, D339,
/// D342): two array textures kept from frame to frame, their layers square
/// with every mip, one of the decals' colors (sRGB) and one of their
/// normal textures (linear). A texture is drawn into a layer the first
/// frame it is drawn and held, each mip from the texture's own nearest
/// that size, a few textures a frame; it keeps its layer while it is
/// drawn, and the layer least lately drawn is taken for another. A decal
/// whose texture has no layer this frame is left out; one whose normal
/// texture has none leaves the normals as they are.
class DecalAtlas {
public:
    /// A layer's side in texels, its mips, and the layers of each atlas.
    static constexpr std::uint32_t kSide = 256;
    static constexpr std::uint32_t kLevels = 9;
    static constexpr std::uint32_t kLayers = 32;
    /// The textures drawn into each atlas's layers in one frame, at most.
    static constexpr std::size_t kMostFills = 4;

    explicit DecalAtlas(mrhiDevice* native) noexcept;
    DecalAtlas(const DecalAtlas&) = delete;
    DecalAtlas& operator=(const DecalAtlas&) = delete;
    ~DecalAtlas();

    /// The array textures made.
    result::Status make();

    /// Joins the open frame: the atlases imported; each of `frame`'s
    /// decals given its textures' layers when `made`, its pipelines are
    /// made, the new ones to be drawn from `textures`; its blocks declared,
    /// and what the upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const render::DeviceTextures& textures,
                           std::vector<mrhiAccess>& writes);

    /// The colors' atlas joined to the open frame, none for a frame
    /// without decals, and the normals', none for a frame whose decals
    /// have none; its decals' blocks, how many it draws, and how many of
    /// those bend the normals.
    [[nodiscard]] mrhiResourceId colors() const noexcept;
    [[nodiscard]] mrhiResourceId normals() const noexcept;
    [[nodiscard]] mrhiResourceId blocks() const noexcept;
    [[nodiscard]] std::uint64_t blockBytes() const noexcept;
    [[nodiscard]] std::size_t drawn() const noexcept;
    [[nodiscard]] std::size_t bent() const noexcept;

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
    /// One array texture, in `format`, and what its layers hold.
    struct Layers {
        mrhiFormat format = kPictureFormat;
        mrhiTextureId texture{};
        mrhiResourceId atlas{};
        std::array<Layer, kLayers> held{};
        std::vector<Fill> fills;
    };

    result::Status make(Layers& layers, const char* why);
    result::Status import(Layers& layers, const char* why);
    /// `texture`'s layer of `layers` this frame, drawn from the frame's
    /// textures if it is new; below nought for none.
    float layerOf(Layers& layers, std::uint64_t texture, const render::DeviceTextures& textures);
    result::Status addPasses(Layers& layers);
    result::Status record(const Layers& layers, const Asked& fill, const Pipelines& pipelines);

    mrhiDevice* native_ = nullptr;
    Layers colors_{.format = kPictureFormat};
    Layers normals_{.format = kNormalsFormat};
    std::vector<DecalBlock> blocks_;
    mrhiResourceId blocksResource_{};
    std::size_t drawn_ = 0;
    std::size_t bent_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace rawframe::render_scene_gpu
