#pragma once

#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <map>
#include <maul-rhi/frame.h>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::render_scene_gpu {

/// An emitter as the particles' shaders read it (std140, D353): its
/// anchor relative to the eye; where it spawns relative to its anchor, and
/// the sphere's radius; its way, and the cosine of the cone's half angle;
/// its particles' speed, life, drag, and variation; their acceleration,
/// and when on the particle clock the frame began; their size at birth and
/// death, the step between births, and the clock now; their color at birth
/// and death; its ring's first slot in the pool, its size, where this
/// spawn starts in it, and how many it spawns; and how many of them are
/// steady, its seed, and its material's place.
struct EmitterBlock {
    std::array<float, 4> anchor{};
    std::array<float, 4> origin{};
    std::array<float, 4> direction{};
    std::array<float, 4> motion{};
    std::array<float, 4> acceleration{};
    std::array<float, 4> sizes{};
    std::array<float, 4> colorStart{};
    std::array<float, 4> colorEnd{};
    std::array<std::uint32_t, 4> ring{};
    std::array<std::uint32_t, 4> more{};
};
static_assert(sizeof(EmitterBlock) == 160, "the particles' shaders read an emitter as 160 bytes");

/// The view as the particles' shaders read it (D353): the eye's right and
/// up in the World's axes, and the particle clock now; and the near plane.
struct ParticleViewBlock {
    std::array<float, 4> right{};
    std::array<float, 4> up{};
    std::array<float, 4> lens{};
};

/// The bytes one particle takes in the pool (D353): where it started and
/// when it was born, its velocity and life, and the scale on its size.
inline constexpr std::uint64_t kParticleBytes = 48;

/// What the particles draw with that the frame holds (D353): the frame's
/// view, the exposure, the materials and their textures, and the depth the
/// prepass left.
struct ParticleDrawing {
    const render::DeviceTextures* textures = nullptr;
    mrhiResourceId block{};
    mrhiResourceId exposure{};
    mrhiResourceId materials{};
    std::uint64_t materialsBytes = 0;
    mrhiResourceId depth{};
};

/// The particles on the device (ADR-0053's GPU simulation, D353): one pool
/// kept from frame to frame, made the first time a view draws an emitter,
/// in which each emitter a frame draws holds a ring of its own, found
/// where the pool has room and cleared by a compute pass when it starts;
/// a compute pass writing the frame's births into the rings; and a pass
/// drawing every slot of each ring over the models' light, farthest
/// emitter first, each particle where its age puts it, faded where it
/// meets what is behind it.
/// An emitter whose ring the pool cannot hold is left out and counted; a
/// ring is let go once its emitter is not drawn.
class ParticlePass {
public:
    ParticlePass(mrhiDevice* native, std::uint32_t capacity) noexcept;
    ParticlePass(const ParticlePass&) = delete;
    ParticlePass& operator=(const ParticlePass&) = delete;
    ~ParticlePass();

    /// Joins the open frame when it draws emitters and their pipelines are
    /// `made`: the pool made or imported, each emitter's ring found, and
    /// what the upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame, bool made, std::vector<mrhiAccess>& writes);

    /// The births, then the particles drawn over `scene`, reading the
    /// prepass's `depth` and `reads` (the frame's view, the exposure, the
    /// materials, and their textures).
    result::Status addPasses(mrhiResourceId scene, mrhiResourceId depth, std::span<const mrhiAccess> reads);

    /// Its writes, in the upload pass: its emitters and its view.
    result::Status write(mrhiPassId upload);

    /// Its passes recorded.
    result::Status record(const Pipelines& pipelines, const ParticleDrawing& with);

    /// The frame ended: a ring a frame that was not submitted would have
    /// cleared is found again.
    void ended(bool submitted) noexcept;

    /// The open frame's emitters drawn, those left out, and the particles
    /// spawned.
    [[nodiscard]] std::size_t drawn() const noexcept;
    [[nodiscard]] std::size_t leftOut() const noexcept;
    [[nodiscard]] std::uint64_t spawned() const noexcept;

private:
    /// A ring of the pool: its first slot and its size, and the ring of its
    /// emitter it holds.
    struct Ring {
        std::uint32_t offset = 0;
        std::uint32_t capacity = 0;
        std::uint32_t generation = 0;
    };

    /// An emitter the open frame draws: its ring, what it spawns, and its
    /// material's textures.
    struct Drawn {
        std::uint32_t offset = 0;
        std::uint32_t capacity = 0;
        std::uint32_t spawned = 0;
        bool fresh = false;
        render_scene::SceneTextures textures;
    };

    /// Where the pool has room for `capacity` slots, the first that does.
    [[nodiscard]] std::optional<std::uint32_t> roomFor(std::uint32_t capacity) const;

    mrhiDevice* native_ = nullptr;
    std::uint32_t capacity_ = 0;
    mrhiBufferId buffer_{};
    /// Each drawn emitter's ring, by its key.
    std::map<std::uint64_t, Ring> rings_;
    /// The open frame's: whether it draws particles; its emitters' blocks,
    /// at a stride every device's uniform offsets allow; the rings it
    /// clears, by key; its view; and what it declared.
    bool enabled_ = false;
    std::vector<std::uint8_t> blocks_;
    std::vector<Drawn> drawing_;
    std::vector<std::uint64_t> cleared_;
    ParticleViewBlock view_;
    std::size_t leftOut_ = 0;
    std::uint64_t spawned_ = 0;
    mrhiResourceId pool_{};
    mrhiResourceId blocksResource_{};
    mrhiResourceId viewResource_{};
    mrhiPassId clearPass_{};
    mrhiPassId spawnPass_{};
    mrhiPassId drawPass_{};
};

} // namespace rawframe::render_scene_gpu
