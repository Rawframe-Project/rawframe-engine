#include "models.h"

#include "tables.h"

#include <maul-rhi/encoder.h>
#include <maul-rhi/resources.h>
#include <optional>
#include <tuple>

namespace rawframe::render_scene_gpu {

ModelPasses::ModelPasses(mrhiDevice* native) noexcept : native_(native) {
}

result::Status ModelPasses::declare(std::uint32_t width, std::uint32_t height, std::uint32_t samples, bool surfaced) {
    samples_ = samples;
    surfaced_ = surfaced;
    surfaces_ = {};
    sampledScene_ = {};
    sampledMotion_ = {};
    sampledDepth_ = {};
    sampledSurfaces_ = {};
    resolve_ = {};
    // The lit pipeline writes the motion whether or not it is read; the
    // twins only where the frame is multisampled (D343).
    for (const auto& [kFormat, kMade, kSamples, kWanted] :
         {std::tuple{kSceneFormat, &scene_, 1U, true},
          std::tuple{kMotionFormat, &motion_, 1U, true},
          std::tuple{kDepthFormat, &depth_, 1U, true},
          std::tuple{kSurfaceFormat, &surfaces_, 1U, surfaced},
          std::tuple{kSceneFormat, &sampledScene_, samples, samples > 1},
          std::tuple{kMotionFormat, &sampledMotion_, samples, samples > 1},
          std::tuple{kDepthFormat, &sampledDepth_, samples, samples > 1},
          std::tuple{kSurfaceFormat, &sampledSurfaces_, samples, samples > 1 && surfaced}}) {
        if (!kWanted) {
            continue;
        }
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = kFormat;
        def.width = width;
        def.height = height;
        def.sampleCount = kSamples;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, kMade); kDeclared != mrhi_success) {
            return failed("a target of the models' passes could not be declared", kDeclared);
        }
    }
    return {};
}

mrhiDepthTarget ModelPasses::depthTarget() const noexcept {
    return mrhiDepthTarget{.resource = samples_ > 1 ? sampledDepth_ : depth_,
                           .mip = 0,
                           .layer = 0,
                           .depthLoad = mrhi_loadClear,
                           .depthStore = mrhi_storeKeep,
                           .clearDepth = 0,
                           .stencilLoad = mrhi_loadDiscard,
                           .stencilStore = mrhi_storeDiscard,
                           .clearStencil = 0,
                           .readOnly = false};
}

namespace {

/// A target cleared to `clear`; multisampled, its samples resolved into
/// its one-sample twin as its pass ends, which keeps them no longer.
void target(mrhiColorTarget& into, mrhiResourceId many, mrhiResourceId single, mrhiClearColor clear) noexcept {
    const bool kSampled = many.index1 != 0;
    into.resource = kSampled ? many : single;
    into.resolve = kSampled ? single : mrhiResourceId{};
    into.store = kSampled ? mrhi_storeDiscard : mrhi_storeKeep;
    into.load = mrhi_loadClear;
    into.clear = clear;
}

} // namespace

result::Status ModelPasses::addPrepass(const std::vector<mrhiAccess>& reads) {
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = reads.data();
    def.accessCount = static_cast<std::uint32_t>(reads.size());
    if (surfaced_) {
        target(def.colorTargets[0], sampledSurfaces_, surfaces_, {.red = 0, .green = 1, .blue = 0, .alpha = 1});
        def.colorTargetCount = 1;
    }
    def.depthTarget = depthTarget();
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &prepass_); kAdded != mrhi_success) {
        return failed("the depth pass could not be added", kAdded);
    }
    if (samples_ == 1) {
        return {};
    }
    // The multisampled depth's first sample, into the one-sample depth the
    // screen-space effects and the post chain read.
    const mrhiAccess kSamples{
        .resource = sampledDepth_,
        .kind = mrhi_accessSampled,
        .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = mrhi_aspectDepthOnly}};
    mrhiPassDef resolveDef = mrhiDefaultPassDef();
    resolveDef.accesses = &kSamples;
    resolveDef.accessCount = 1;
    resolveDef.depthTarget = depthTarget();
    resolveDef.depthTarget.resource = depth_;
    resolveDef.depthTarget.depthLoad = mrhi_loadDiscard;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &resolveDef, &resolve_); kAdded != mrhi_success) {
        return failed("the depth's resolve could not be added", kAdded);
    }
    return {};
}

result::Status ModelPasses::addLitPass(const std::vector<mrhiAccess>& reads) {
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = reads.data();
    def.accessCount = static_cast<std::uint32_t>(reads.size());
    target(def.colorTargets[0], sampledScene_, scene_, {.red = 0, .green = 0, .blue = 0, .alpha = 1});
    target(def.colorTargets[1], sampledMotion_, motion_, {.red = 0, .green = 0, .blue = 0, .alpha = 0});
    def.colorTargetCount = 2;
    def.depthTarget = depthTarget();
    def.depthTarget.depthLoad = mrhi_loadKeep;
    def.depthTarget.readOnly = true;
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &lit_); kAdded != mrhi_success) {
        return failed("the models' pass could not be added", kAdded);
    }
    return {};
}

result::Status ModelPasses::drawRuns(const Drawing& with,
                                     std::span<mrhiBinding> table,
                                     mrhiPassId pass,
                                     const Asked& single,
                                     const Asked& many,
                                     const Runs& runs,
                                     std::optional<Shaded> shaded) {
    if (runs.empty()) {
        return {};
    }
    // Multisampled, every pipeline's twin taking the frame's samples (D343).
    const mrhiGraphicsPipelineId kEngine = (samples_ > 1 ? many : single).pipeline;
    mrhiGraphicsPipelineId set = kEngine;
    if (mrhiSetGraphicsPipeline(native_, pass, set) != mrhi_success ||
        mrhiSetVertexBuffer(native_, pass, 1, with.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
        return failed("the models could not be set up", mrhi_errorState);
    }
    // The table set again where a run samples another texture than the one
    // before (D309), white for none and for one not held this frame, or
    // where another pipeline is set.
    std::optional<render_scene::SceneTextures> bound;
    for (const Run& run : runs) {
        mrhiGraphicsPipelineId wanted = kEngine;
        if (const Asked* kOwn =
                shaded.has_value() && run.program != nullptr
                    ? with.pipelines->programPipeline(run.program, shaded->shade, shaded->decaled, samples_ > 1)
                    : nullptr;
            kOwn != nullptr) {
            wanted = kOwn->pipeline;
        }
        if (wanted.index1 != set.index1 || wanted.generation != set.generation) {
            if (mrhiSetGraphicsPipeline(native_, pass, wanted) != mrhi_success) {
                return failed("a material's program could not be set", mrhi_errorState);
            }
            set = wanted;
            bound.reset();
        }
        if (bound != run.texture) {
            bindTexture(*with.textures, *with.pipelines, table[10], table[11], run.texture.base);
            bindTexture(*with.textures, *with.pipelines, table[12], table[13], run.texture.packed);
            bindTexture(*with.textures, *with.pipelines, table[14], table[15], run.texture.emission);
            bindTexture(*with.textures, *with.pipelines, table[16], table[17], run.texture.normal);
            if (mrhiSetBindings(native_, pass, 0, table.data(), table.size()) != mrhi_success) {
                return failed("a material's texture could not be bound", mrhi_errorState);
            }
            bound = run.texture;
        }
        RAWFRAME_TRY(with.held->bind(pass, *run.mesh, posedOf(with.skinning, run)));
        if (mrhiDrawIndexed(native_, pass, run.indexCount, run.count, run.firstIndex, 0, run.first) != mrhi_success) {
            return failed("a model could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

result::Status ModelPasses::recordPrepass(const Drawing& with) {
    const Pipelines& pipelines = *with.pipelines;
    const Multisampled& kMany = pipelines.multisampled;
    // The prepass, which the occlusion, the reflections, and the contact
    // shadows come after, reads white for them.
    std::array<mrhiBinding, kTableSlots> table = with.table;
    for (const std::uint32_t kSlot : {21U, 22U, 23U}) {
        table[kSlot] = textureAt(kSlot, resourceOf(with.textures->resource(0)));
    }
    if (mrhiBeginPass(native_, prepass_) != mrhi_success) {
        return failed("a scene pass could not begin", mrhi_errorState);
    }
    // A material's program cuts its masked models and gives its surfaces
    // (D487); the opaque models' depth alone runs none of its code.
    RAWFRAME_TRY(surfaced_ ? drawRuns(with,
                                      table,
                                      prepass_,
                                      pipelines.surfaces,
                                      kMany.surfaces,
                                      with.placed->runs,
                                      Shaded{.shade = Shade::Surfaces})
                           : drawRuns(with, table, prepass_, pipelines.depth, kMany.depth, with.placed->runs));
    RAWFRAME_TRY(surfaced_ ? drawRuns(with,
                                      table,
                                      prepass_,
                                      pipelines.cutSurfaces,
                                      kMany.cutSurfaces,
                                      with.placed->maskedRuns,
                                      Shaded{.shade = Shade::CutSurfaces})
                           : drawRuns(with,
                                      table,
                                      prepass_,
                                      pipelines.cutout,
                                      kMany.cutout,
                                      with.placed->maskedRuns,
                                      Shaded{.shade = Shade::Cut}));
    if (mrhiEndPass(native_, prepass_) != mrhi_success) {
        return failed("a scene pass could not end", mrhi_errorState);
    }
    // The resolve, unless nothing reads the depth it leaves.
    bool resolved = false;
    if (samples_ > 1 && mrhiIsPassKept(native_, resolve_, &resolved) != mrhi_success) {
        return failed("the depth's resolve could not be looked at", mrhi_errorState);
    }
    if (!resolved) {
        return {};
    }
    const std::array<mrhiBinding, 1> kSamples = {depthAt(0, sampledDepth_)};
    if (mrhiBeginPass(native_, resolve_) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, resolve_, kMany.resolveDepth.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, resolve_, 0, kSamples.data(), kSamples.size()) != mrhi_success ||
        mrhiDraw(native_, resolve_, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, resolve_) != mrhi_success) {
        return failed("the depth could not be resolved", mrhi_errorState);
    }
    return {};
}

result::Status ModelPasses::recordLit(const Drawing& with, bool decaled, std::span<const mrhiBinding> sky) {
    const Pipelines& pipelines = *with.pipelines;
    const Multisampled& kMany = pipelines.multisampled;
    std::array<mrhiBinding, kTableSlots> table = with.table;
    if (mrhiBeginPass(native_, lit_) != mrhi_success) {
        return failed("a scene pass could not begin", mrhi_errorState);
    }
    const Shaded kLit{.shade = Shade::Lit, .decaled = decaled};
    const Shaded kMasked{.shade = Shade::Masked, .decaled = decaled};
    const Shaded kGlass{.shade = Shade::Glass, .decaled = decaled};
    RAWFRAME_TRY(decaled ? drawRuns(with, table, lit_, pipelines.litDecaled, kMany.litDecaled, with.placed->runs, kLit)
                         : drawRuns(with, table, lit_, pipelines.lit, kMany.lit, with.placed->runs, kLit));
    RAWFRAME_TRY(
        decaled ? drawRuns(with,
                           table,
                           lit_,
                           pipelines.maskedLitDecaled,
                           kMany.maskedLitDecaled,
                           with.placed->maskedRuns,
                           kMasked)
                : drawRuns(with, table, lit_, pipelines.maskedLit, kMany.maskedLit, with.placed->maskedRuns, kMasked));
    if (mrhiSetGraphicsPipeline(native_, lit_, (samples_ > 1 ? kMany.sky : pipelines.sky).pipeline) != mrhi_success ||
        mrhiSetBindings(native_, lit_, 0, sky.data(), sky.size()) != mrhi_success ||
        mrhiDraw(native_, lit_, 3, 1, 0, 0) != mrhi_success) {
        return failed("the sky could not be drawn", mrhi_errorState);
    }
    RAWFRAME_TRY(
        decaled
            ? drawRuns(
                  with, table, lit_, pipelines.glassDecaled, kMany.glassDecaled, with.placed->translucentRuns, kGlass)
            : drawRuns(with, table, lit_, pipelines.glass, kMany.glass, with.placed->translucentRuns, kGlass));
    if (mrhiEndPass(native_, lit_) != mrhi_success) {
        return failed("a scene pass could not end", mrhi_errorState);
    }
    return {};
}

mrhiResourceId ModelPasses::scene() const noexcept {
    return scene_;
}

mrhiResourceId ModelPasses::motion() const noexcept {
    return motion_;
}

mrhiResourceId ModelPasses::depth() const noexcept {
    return depth_;
}

mrhiResourceId ModelPasses::surfaces() const noexcept {
    return surfaces_;
}

std::uint32_t ModelPasses::samples() const noexcept {
    return samples_;
}

bool ModelPasses::surfaced() const noexcept {
    return surfaced_;
}

} // namespace rawframe::render_scene_gpu
