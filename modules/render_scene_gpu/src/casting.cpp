#include "casting.h"

#include "tables.h"

#include <array>
#include <maul-rhi/encoder.h>
#include <optional>

namespace rawframe::render_scene_gpu {

result::Status cast(const Casting& with, mrhiPassId pass, std::span<const Square> squares) {
    mrhiDevice* native = with.native;
    const Pipelines& pipelines = *with.pipelines;
    if (mrhiBeginPass(native, pass) != mrhi_success) {
        return failed("a shadow pass could not begin", mrhi_errorState);
    }
    const auto kDraw = [&with, native, pass](const Run& run) -> result::Status {
        RAWFRAME_TRY(with.held->bind(pass, *run.mesh));
        if (mrhiDrawIndexed(native, pass, run.indexCount, run.count, run.firstIndex, 0, run.first) != mrhi_success) {
            return failed("a caster could not be drawn", mrhi_errorState);
        }
        return {};
    };
    for (const Square& kSquare : squares) {
        if (kSquare.casters->empty()) {
            continue;
        }
        std::array<mrhiBinding, 4> binding = {bufferAt(0, kSquare.view, sizeof(Matrix4)),
                                              bufferAt(1, with.materials, with.materialsBytes),
                                              textureAt(2, resourceOf(with.textures->resource(0))),
                                              samplerAt(3, pipelines.materialSamplers[0])};
        if (mrhiSetViewport(native, pass, &kSquare.viewport) != mrhi_success) {
            return failed("a shadow square could not be set up", mrhi_errorState);
        }
        if (!kSquare.casters->solid.empty()) {
            if (mrhiSetGraphicsPipeline(native, pass, pipelines.casting.pipeline) != mrhi_success ||
                mrhiSetVertexBuffer(native, pass, 1, with.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                return failed("the casters could not be set up", mrhi_errorState);
            }
            for (const Run& run : kSquare.casters->solid) {
                RAWFRAME_TRY(kDraw(run));
            }
        }
        if (kSquare.casters->masked.empty()) {
            continue;
        }
        if (mrhiSetGraphicsPipeline(native, pass, pipelines.cutCasting.pipeline) != mrhi_success ||
            mrhiSetVertexBuffer(native, pass, 1, with.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
            return failed("the masked casters could not be set up", mrhi_errorState);
        }
        std::optional<render_scene::SceneTexture> bound;
        for (const Run& run : kSquare.casters->masked) {
            if (bound != run.texture.base) {
                bindTexture(*with.textures, pipelines, binding[2], binding[3], run.texture.base);
                if (mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                    return failed("a masked caster's texture could not be bound", mrhi_errorState);
                }
                bound = run.texture.base;
            }
            RAWFRAME_TRY(kDraw(run));
        }
    }
    if (mrhiEndPass(native, pass) != mrhi_success) {
        return failed("a shadow pass could not end", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render_scene_gpu
