#pragma once

#include "meshes.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/result/result.h"
#include "runs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::render_scene_gpu {

/// The slots of the scene's table, as the lit shaders read it.
inline constexpr std::size_t kTableSlots = 28;

/// What a frame's models' passes draw with: the pipelines, the meshes and
/// textures held, the frame's placements and their runs, and the scene's
/// table, every slot of it set but each run's textures.
struct Drawing {
    const Pipelines* pipelines = nullptr;
    const DeviceMeshes* held = nullptr;
    const render::DeviceTextures* textures = nullptr;
    mrhiResourceId instances{};
    const Placed* placed = nullptr;
    std::array<mrhiBinding, kTableSlots> table{};
};

/// A frame's models' passes (D284, D343): the prepass's depth, and each
/// point's surface beside it when a screen-space effect reads them (D327,
/// D331); the lit pass's light and motion over that depth, the sky where
/// none lies; and, multisampled, each target's many-sample twin, resolved
/// as its pass ends, the depth by a pass of its own.
class ModelPasses {
public:
    explicit ModelPasses(mrhiDevice* native) noexcept;

    /// The targets declared for a frame of `width` by `height`, taking
    /// `samples` a pixel, with surfaces when `surfaced`.
    result::Status declare(std::uint32_t width, std::uint32_t height, std::uint32_t samples, bool surfaced);

    /// The prepass and the depth's resolve, the prepass reading `reads`.
    result::Status addPrepass(const std::vector<mrhiAccess>& reads);
    /// The lit pass, reading `reads`.
    result::Status addLitPass(const std::vector<mrhiAccess>& reads);

    /// The prepass recorded, the masked runs cut (D310), and the depth
    /// resolved unless nothing reads it.
    result::Status recordPrepass(const Drawing& with);
    /// The lit pass recorded: the opaque runs, the masked, the sky with
    /// `sky`'s table, then the translucent over them (D305); by the twins
    /// that lay decals when `decaled` (D339).
    result::Status recordLit(const Drawing& with, bool decaled, std::span<const mrhiBinding> sky);

    [[nodiscard]] mrhiResourceId scene() const noexcept;
    [[nodiscard]] mrhiResourceId motion() const noexcept;
    [[nodiscard]] mrhiResourceId depth() const noexcept;
    [[nodiscard]] mrhiResourceId surfaces() const noexcept;
    [[nodiscard]] std::uint32_t samples() const noexcept;
    [[nodiscard]] bool surfaced() const noexcept;

private:
    /// What lit runs are lit as: their shade, and whether under decals.
    struct Lighting {
        Shade shade = Shade::Lit;
        bool decaled = false;
    };

    /// Runs of models drawn in `pass` by `single`'s pipeline, or by
    /// `many`'s when multisampled, with `table`; lit, a run whose material
    /// has its own program by that program's twin, once it is made (D485).
    result::Status drawRuns(const Drawing& with,
                            std::span<mrhiBinding> table,
                            mrhiPassId pass,
                            const Asked& single,
                            const Asked& many,
                            const Runs& runs,
                            std::optional<Lighting> lighting = std::nullopt);
    [[nodiscard]] mrhiDepthTarget depthTarget() const noexcept;

    mrhiDevice* native_ = nullptr;
    std::uint32_t samples_ = 1;
    bool surfaced_ = false;
    mrhiResourceId scene_{};
    mrhiResourceId motion_{};
    mrhiResourceId depth_{};
    mrhiResourceId surfaces_{};
    mrhiResourceId sampledScene_{};
    mrhiResourceId sampledMotion_{};
    mrhiResourceId sampledDepth_{};
    mrhiResourceId sampledSurfaces_{};
    mrhiPassId prepass_{};
    mrhiPassId resolve_{};
    mrhiPassId lit_{};
};

} // namespace rawframe::render_scene_gpu
