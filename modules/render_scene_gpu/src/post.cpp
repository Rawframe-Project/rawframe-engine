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
    copy_ = {};
    leftOut_ = frame.postProcessesLeftOut;
    if (!made) {
        return {};
    }
    for (const render_scene::ScenePostProcess& kProcess : frame.postProcesses) {
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
    if (at(material::Insertion::FinalOutput) > 0) {
        // The copy: the picture times one, wholly.
        copy_.block.form[3] = 1;
        copy_.block.form[4] = 1;
        copy_.block.form[5] = 1;
        copy_.block.form[6] = 1;
        copy_.block.form[16] = 1;
        copy_.block.form[17] = 1;
        copy_.block.weight[0] = 1;
        copy_.texture = resourceOf(textures.resource(0));
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = sizeof(PostBlock);
        if (mrhiDeclareBuffer(native_, &def, &copy_.blockResource) != mrhi_success) {
            return failed("a post process could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(copy_.blockResource, mrhi_accessCopyDestination));
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

result::Status PostProcessPass::addCopy(mrhiResourceId from, mrhiResourceId into) {
    const std::array<mrhiAccess, 3> kReads = {wholeOf(from, mrhi_accessSampled),
                                              wholeOf(copy_.blockResource, mrhi_accessUniform),
                                              wholeOf(copy_.texture, mrhi_accessSampled)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = into;
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargetCount = 1;
    def.neverCull = true;
    def.accesses = kReads.data();
    def.accessCount = static_cast<std::uint32_t>(kReads.size());
    if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &copy_.pass); kAdded != mrhi_success) {
        return failed("the composed picture's copy could not be added", kAdded);
    }
    copy_.input = from;
    return {};
}

result::Status PostProcessPass::write(mrhiPassId upload) {
    for (const Step& kStep : steps_) {
        if (mrhiWriteBuffer(native_, upload, kStep.blockResource, 0, &kStep.block, sizeof(PostBlock)) != mrhi_success) {
            return failed("a post process could not be written", mrhi_errorCapacity);
        }
    }
    if (copy_.blockResource.index1 != 0 &&
        mrhiWriteBuffer(native_, upload, copy_.blockResource, 0, &copy_.block, sizeof(PostBlock)) != mrhi_success) {
        return failed("a post process could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status PostProcessPass::record(const Pipelines& pipelines, material::Insertion insertion) {
    for (const Step& kStep : steps_) {
        if (kStep.insertion != insertion || kStep.pass.index1 == 0) {
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
    if (insertion != material::Insertion::FinalOutput || copy_.pass.index1 == 0) {
        return {};
    }
    const std::array<mrhiBinding, 4> kBinding = {textureAt(0, copy_.input),
                                                 bufferAt(1, copy_.blockResource, sizeof(PostBlock)),
                                                 textureAt(2, copy_.texture),
                                                 samplerAt(3, pipelines.materialSamplers[0])};
    if (mrhiBeginPass(native_, copy_.pass) != mrhi_success ||
        mrhiSetGraphicsPipeline(native_, copy_.pass, pipelines.postDisplay.pipeline) != mrhi_success ||
        mrhiSetBindings(native_, copy_.pass, 0, kBinding.data(), kBinding.size()) != mrhi_success ||
        mrhiDraw(native_, copy_.pass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, copy_.pass) != mrhi_success) {
        return failed("the composed picture could not be copied back", mrhi_errorState);
    }
    return {};
}

std::size_t PostProcessPass::run() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(steps_, [](const Step& step) {
        return step.pass.index1 != 0;
    }));
}

std::size_t PostProcessPass::leftOut() const noexcept {
    return leftOut_ + steps_.size() - run();
}

} // namespace rawframe::render_scene_gpu
