#pragma once

#include "blocks.h"
#include "meshes.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/result/result.h"
#include "runs.h"

#include <cstdint>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <span>

namespace rawframe::render_scene_gpu {

/// A square of a shadow map: its view, where it lies, and its casters.
struct Square {
    mrhiResourceId view{};
    mrhiViewport viewport{};
    const Casters* casters = nullptr;
};

/// What a frame's shadow passes draw with: the device, its pipelines, the
/// meshes and textures held, and the frame's placements and materials.
struct Casting {
    mrhiDevice* native = nullptr;
    const Pipelines* pipelines = nullptr;
    const DeviceMeshes* held = nullptr;
    const render::DeviceTextures* textures = nullptr;
    mrhiResourceId instances{};
    mrhiResourceId materials{};
    std::uint64_t materialsBytes = 0;
};

/// A shadow pass recorded: each square drawn from its view, its solid
/// casters in their runs, then its masked ones cut by their material's
/// texture (D310).
result::Status cast(const Casting& with, mrhiPassId pass, std::span<const Square> squares);

} // namespace rawframe::render_scene_gpu
