#include "post.h"

#include "tables.h"

#include <algorithm>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

/// Whether a post process at `insertion` runs over scene-linear light.
bool linear(material::Insertion insertion) noexcept {
    return insertion == material::Insertion::AfterTemporal || insertion == material::Insertion::BeforeTonemap;
}

} // namespace

PostProcessPass::PostProcessPass(mrhiDevice* native) noexcept : native_(native) {
}

result::Status PostProcessPass::declare(const render_scene::SceneFrame& frame,
                                        bool made,
                                        const render::DeviceTextures& textures,
                                        std::vector<mrhiAccess>& writes) {
    steps_.clear();
    leftOut_ = frame.postProcessesLeftOut;
    if (!made) {
        return {};
    }
    for (const render_scene::ScenePostProcess& kProcess : frame.postProcesses) {
        if (kProcess.insertion == material::Insertion::FinalOutput) {
            ++leftOut_;
            continue;
        }
        const std::uint64_t kHeld = kProcess.texture.id != 0 ? textures.resource(kProcess.texture.id) : 0;
        Step& step = steps_.emplace_back(Step{
            .insertion = kProcess.insertion,
            .block = {.form = kProcess.blob, .weight = {kProcess.weight, 0, 0, 0}},
            .texture = resourceOf(kHeld != 0 && !textures.cube(kProcess.texture.id) ? kHeld : textures.resource(0)),
            .sampler = samplerOf(kProcess.texture.filter, kProcess.texture.address)});
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = sizeof(PostBlock);
        if (mrhiDeclareBuffer(native_, &def, &step.blockResource) != mrhi_success) {
            return failed("a post process could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(step.blockResource, mrhi_accessCopyDestination));
    }
    return {};
}

std::size_t PostProcessPass::at(material::Insertion insertion) const noexcept {
    return static_cast<std::size_t>(std::ranges::count(steps_, insertion, &Step::insertion));
}

result::Result<mrhiResourceId> PostProcessPass::addStage(material::Insertion insertion,
                                                         mrhiResourceId input,
                                                         std::uint32_t width,
                                                         std::uint32_t height,
                                                         mrhiResourceId into,
                                                         bool clears) {
    std::size_t left = at(insertion);
    mrhiResourceId shown = input;
    for (Step& step : steps_) {
        if (step.insertion != insertion) {
            continue;
        }
        --left;
        mrhiResourceId target = left == 0 ? into : mrhiResourceId{};
        const bool kOwn = target.index1 == 0;
        if (kOwn) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = linear(insertion) ? kSceneFormat : kPictureFormat;
            def.width = width;
            def.height = height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native_, &def, &target); kDeclared != mrhi_success) {
                return failed("a post process's picture could not be declared", kDeclared);
            }
        }
        const std::array<mrhiAccess, 3> kReads = {wholeOf(shown, mrhi_accessSampled),
                                                  wholeOf(step.blockResource, mrhi_accessUniform),
                                                  wholeOf(step.texture, mrhi_accessSampled)};
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0].resource = target;
        def.colorTargets[0].load = kOwn ? mrhi_loadDiscard : clears ? mrhi_loadClear : mrhi_loadKeep;
        def.colorTargets[0].store = mrhi_storeKeep;
        def.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        def.colorTargetCount = 1;
        def.neverCull = !kOwn;
        def.accesses = kReads.data();
        def.accessCount = static_cast<std::uint32_t>(kReads.size());
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &step.pass); kAdded != mrhi_success) {
            return failed("a post process's pass could not be added", kAdded);
        }
        step.input = shown;
        shown = target;
    }
    return shown;
}

result::Status PostProcessPass::write(mrhiPassId upload) {
    for (const Step& kStep : steps_) {
        if (mrhiWriteBuffer(native_, upload, kStep.blockResource, 0, &kStep.block, sizeof(PostBlock)) != mrhi_success) {
            return failed("a post process could not be written", mrhi_errorCapacity);
        }
    }
    return {};
}

result::Status PostProcessPass::record(const Pipelines& pipelines, material::Insertion insertion) {
    for (const Step& kStep : steps_) {
        if (kStep.insertion != insertion) {
            continue;
        }
        const std::array<mrhiBinding, 4> kBinding = {textureAt(0, kStep.input),
                                                     bufferAt(1, kStep.blockResource, sizeof(PostBlock)),
                                                     textureAt(2, kStep.texture),
                                                     samplerAt(3, pipelines.materialSamplers.at(kStep.sampler))};
        const mrhiGraphicsPipelineId kPipeline =
            linear(insertion) ? pipelines.postLinear.pipeline : pipelines.postDisplay.pipeline;
        if (mrhiBeginPass(native_, kStep.pass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native_, kStep.pass, kPipeline) != mrhi_success ||
            mrhiSetBindings(native_, kStep.pass, 0, kBinding.data(), kBinding.size()) != mrhi_success ||
            mrhiDraw(native_, kStep.pass, 3, 1, 0, 0) != mrhi_success ||
            mrhiEndPass(native_, kStep.pass) != mrhi_success) {
            return failed("a post process could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

std::size_t PostProcessPass::run() const noexcept {
    return steps_.size();
}

std::size_t PostProcessPass::leftOut() const noexcept {
    return leftOut_;
}

} // namespace rawframe::render_scene_gpu
