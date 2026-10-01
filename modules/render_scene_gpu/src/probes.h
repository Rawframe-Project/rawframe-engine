#pragma once

#include "blocks.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <functional>
#include <maul-rhi/frame.h>
#include <maul-rhi/resources.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A reflection probe as the lit pass reads it (std430, D325, D340): its
/// box's middle relative to the eye and its cube of the atlas, below
/// nought for none this frame; its half sides; and its light's scale.
struct ProbeBlock {
    std::array<float, 4> place{};
    std::array<float, 4> extent{};
    std::array<float, 4> light{};
};
static_assert(sizeof(ProbeBlock) == 48, "the lit shader reads a probe as 48 bytes");

/// The reflection probes' pictures as the lit pass samples them (ADR-0051,
/// D340): one cube array kept from frame to frame, each cube's mips the
/// light as ever rougher surfaces reflect it. A probe's picture is drawn
/// into a cube the first frame it is drawn and held, a few pictures a
/// frame; it keeps its cube while it is drawn, and the cube least lately
/// drawn is taken for another. A probe whose picture has no cube this
/// frame is passed over, as one that is not there.
class ProbeAtlas {
public:
    /// A face's side in texels, its mips, and the cubes.
    static constexpr std::uint32_t kSide = 128;
    static constexpr std::uint32_t kLevels = 8;
    static constexpr std::uint32_t kCubes = 16;
    /// The pictures drawn into cubes in one frame, at most.
    static constexpr std::size_t kMostFills = 2;

    explicit ProbeAtlas(mrhiDevice* native) noexcept;
    ProbeAtlas(const ProbeAtlas&) = delete;
    ProbeAtlas& operator=(const ProbeAtlas&) = delete;
    ~ProbeAtlas();

    /// The cube array made.
    result::Status make();

    /// Whether `picture` is held in a cube, so a frame need not hold it.
    [[nodiscard]] bool holds(std::uint64_t picture) const noexcept;

    /// Joins the open frame: the atlas imported; each of `frame`'s probes
    /// given its picture's cube when `made`, its pipeline is made, the new
    /// ones to be drawn from `textures` where `usable` says the picture is
    /// an environment; its blocks declared, and what the upload pass
    /// writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const render::DeviceTextures& textures,
                           const std::function<bool(std::uint64_t)>& usable,
                           std::vector<mrhiAccess>& writes);

    /// The atlas joined to the open frame, none for a frame without
    /// probes; its probes' blocks, and how many it draws.
    [[nodiscard]] mrhiResourceId atlas() const noexcept;
    [[nodiscard]] mrhiResourceId blocks() const noexcept;
    [[nodiscard]] std::uint64_t blockBytes() const noexcept;
    [[nodiscard]] std::size_t drawn() const noexcept;

    /// Its passes: each new picture drawn into its cube's faces and mips.
    result::Status addPasses();

    /// Its write, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines);

    /// The frame ended: a cube drawn this frame holds its picture only
    /// when the frame was submitted.
    void ended(bool submitted) noexcept;

private:
    struct Cube {
        std::uint64_t picture = 0;
        /// The frame it was last drawn in.
        std::uint64_t used = 0;
    };
    struct Fill {
        std::uint32_t cube = 0;
        mrhiResourceId source{};
        std::array<mrhiPassId, std::size_t{6} * kLevels> passes{};
    };

    mrhiDevice* native_ = nullptr;
    mrhiTextureId texture_{};
    mrhiResourceId atlas_{};
    std::array<Cube, kCubes> cubes_{};
    std::vector<Fill> fills_;
    std::vector<ProbeBlock> blocks_;
    mrhiResourceId blocksResource_{};
    std::size_t drawn_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace rawframe::render_scene_gpu
