#pragma once

#include "pipelines.h"
#include "rawframe/material/post_process.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <maul-rhi/frame.h>
#include <vector>

namespace rawframe::render_scene_gpu {

/// A post process as its shader reads it (D350): its material's form, its
/// texture's map, and its weight.
struct PostBlock {
    std::array<float, material::kPostProcessBlobFloats> form{};
    std::array<float, 4> weight{};
};
static_assert(sizeof(PostBlock) == 96, "a post process's shader reads its block as 96 bytes");

/// A frame's post processes (ADR-0051, D348 to D351), each a pass over the
/// chain's picture at its material's insertion point, in the camera's
/// order there: after the temporal slot and before the tonemapper over
/// scene-linear light, after the tonemapper and over the scene's output
/// over the display-referred picture, and over the composed picture
/// (`final_output`) once the canvas has drawn, copied back into it. One
/// whose pass a frame does not add is left out and counted.
class PostProcessPass {
public:
    explicit PostProcessPass(mrhiDevice* native) noexcept;

    /// Joins the open frame, its pipelines `made`: its post processes'
    /// textures among those `textures` holds (white for one not held), and
    /// what the upload pass writes added to `writes`.
    result::Status declare(const render_scene::SceneFrame& frame,
                           bool made,
                           const render::DeviceTextures& textures,
                           std::vector<mrhiAccess>& writes);

    /// How many run at `insertion` this frame.
    [[nodiscard]] std::size_t at(material::Insertion insertion) const noexcept;

    /// The passes at `insertion`, the first reading `input`, each the one
    /// before's, at `width` by `height`; the last into `into`, cleared
    /// first when `clears`, or into a picture of its own when `into` is
    /// none. What the last leaves, or `input` with none.
    result::Result<mrhiResourceId> addStage(material::Insertion insertion,
                                            mrhiResourceId input,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            mrhiResourceId into,
                                            bool clears);

    /// Its writes, in the upload pass.
    result::Status write(mrhiPassId upload);

    /// A pass copying `from` into `into`, kept from what it was: the last
    /// over the composed picture, which reads it and so cannot write it.
    result::Status addCopy(mrhiResourceId from, mrhiResourceId into);

    /// Its passes at `insertion` recorded, and the copy after those over
    /// the composed picture.
    result::Status record(const Pipelines& pipelines, material::Insertion insertion);

    /// The post processes run, and those left out, this frame.
    [[nodiscard]] std::size_t run() const noexcept;
    [[nodiscard]] std::size_t leftOut() const noexcept;

private:
    struct Step {
        material::Insertion insertion = material::Insertion::AfterTonemap;
        PostBlock block;
        mrhiResourceId blockResource{};
        mrhiResourceId texture{};
        std::size_t sampler = 0;
        mrhiResourceId input{};
        mrhiPassId pass{};
    };

    mrhiDevice* native_ = nullptr;
    std::vector<Step> steps_;
    /// The copy back into the composed picture, its block declared when a
    /// post process is over it.
    Step copy_;
    std::size_t leftOut_ = 0;
};

} // namespace rawframe::render_scene_gpu
