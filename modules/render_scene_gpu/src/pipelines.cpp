#include "pipelines.h"

#include "blocks.h"
#include "generated/fxaa_container.h"
#include "generated/meter_container.h"
#include "generated/scene_container.h"
#include "generated/shadow_container.h"
#include "generated/sky_container.h"
#include "generated/temporal_container.h"
#include "generated/tonemap_container.h"
#include "rawframe/render_scene_gpu/errors.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <array>
#include <string>

namespace rawframe::render_scene_gpu {

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kSceneGpuDomain, code(SceneGpuError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

Pipelines::~Pipelines() {
    if (native == nullptr) {
        return;
    }
    // Maul RHI retires what a frame still uses once the frame is done.
    for (Asked* asked :
         {&casting, &cutCasting, &depth, &cutout, &lit, &maskedLit, &glass, &sky, &temporal, &tonemap, &fxaa}) {
        static_cast<void>(mrhiDestroyGraphicsPipeline(native, asked->pipeline));
    }
    for (Asked* asked : {&histogram, &adapt}) {
        static_cast<void>(mrhiDestroyComputePipeline(native, asked->compute));
    }
    static_cast<void>(mrhiDestroySampler(native, shadowSampler));
    static_cast<void>(mrhiDestroySampler(native, filteredSampler));
    for (const mrhiSamplerId kSampler : materialSamplers) {
        static_cast<void>(mrhiDestroySampler(native, kSampler));
    }
    for (const mrhiShaderId kShader :
         {sceneShader, tonemapShader, shadowShader, temporalShader, skyShader, meterShader, fxaaShader}) {
        static_cast<void>(mrhiDestroyShader(native, kShader));
    }
}

result::Status Pipelines::makeShader(std::span<const std::uint8_t> container, mrhiShaderId& shader) {
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = container.data();
    def.byteCount = container.size();
    if (const mrhiResult kMade = mrhiCreateShader(native, &def, &shader); kMade != mrhi_success) {
        return failed("a scene shader could not be made", kMade);
    }
    return {};
}

result::Status Pipelines::ask(const mrhiGraphicsPipelineDef& def, Asked& asked) {
    mrhiRequestId request{};
    if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &asked.pipeline, &request);
        kMade != mrhi_success) {
        return failed("a scene pipeline could not be asked for", kMade);
    }
    asked.request = render::requestKey(request.index1, request.generation);
    return {};
}

result::Status Pipelines::ask(const mrhiComputePipelineDef& def, Asked& asked) {
    mrhiRequestId request{};
    if (const mrhiResult kMade = mrhiCreateComputePipeline(native, &def, &asked.compute, &request);
        kMade != mrhi_success) {
        return failed("a scene pipeline could not be asked for", kMade);
    }
    asked.request = render::requestKey(request.index1, request.generation);
    return {};
}

result::Status Pipelines::make() {
    RAWFRAME_TRY(makeShader(kSceneContainer, sceneShader));
    RAWFRAME_TRY(makeShader(kTonemapContainer, tonemapShader));
    RAWFRAME_TRY(makeShader(kShadowContainer, shadowShader));
    RAWFRAME_TRY(makeShader(kTemporalContainer, temporalShader));
    RAWFRAME_TRY(makeShader(kSkyContainer, skyShader));
    RAWFRAME_TRY(makeShader(kMeterContainer, meterShader));
    RAWFRAME_TRY(makeShader(kFxaaContainer, fxaaShader));
    // Each vertex of the mesh, then each draw's placement.
    constexpr std::array<mrhiVertexBufferLayout, 2> kBuffers = {
        mrhiVertexBufferLayout{.stride = kVertexBytes, .stepMode = mrhi_stepVertex},
        mrhiVertexBufferLayout{.stride = kInstanceBytes, .stepMode = mrhi_stepInstance}};
    constexpr std::array<mrhiVertexAttribute, 16> kAttributes = {
        mrhiVertexAttribute{.buffer = 0, .location = 0, .format = mrhi_vertexFloat32x3, .offset = 0},
        mrhiVertexAttribute{.buffer = 0, .location = 1, .format = mrhi_vertexFloat32x3, .offset = 12},
        mrhiVertexAttribute{.buffer = 0, .location = 13, .format = mrhi_vertexFloat32x2, .offset = 24},
        mrhiVertexAttribute{.buffer = 1, .location = 2, .format = mrhi_vertexFloat32x4, .offset = 0},
        mrhiVertexAttribute{.buffer = 1, .location = 3, .format = mrhi_vertexFloat32x4, .offset = 16},
        mrhiVertexAttribute{.buffer = 1, .location = 4, .format = mrhi_vertexFloat32x4, .offset = 32},
        mrhiVertexAttribute{.buffer = 1, .location = 5, .format = mrhi_vertexFloat32x3, .offset = 48},
        mrhiVertexAttribute{.buffer = 1, .location = 6, .format = mrhi_vertexFloat32x3, .offset = 60},
        mrhiVertexAttribute{.buffer = 1, .location = 7, .format = mrhi_vertexFloat32x3, .offset = 72},
        mrhiVertexAttribute{.buffer = 1, .location = 8, .format = mrhi_vertexFloat32x4, .offset = 84},
        mrhiVertexAttribute{.buffer = 1, .location = 9, .format = mrhi_vertexFloat32x4, .offset = 100},
        mrhiVertexAttribute{.buffer = 1, .location = 10, .format = mrhi_vertexFloat32x4, .offset = 116},
        mrhiVertexAttribute{.buffer = 1, .location = 11, .format = mrhi_vertexFloat32x4, .offset = 132},
        mrhiVertexAttribute{.buffer = 1, .location = 12, .format = mrhi_vertexFloat32, .offset = 148},
        mrhiVertexAttribute{.buffer = 0, .location = 14, .format = mrhi_vertexFloat32x4, .offset = 32},
        mrhiVertexAttribute{.buffer = 1, .location = 15, .format = mrhi_vertexFloat32, .offset = 152}};
    mrhiGraphicsPipelineDef models = mrhiDefaultGraphicsPipelineDef();
    models.shader = sceneShader;
    models.vertexEntry = "vs";
    models.vertexEntryLength = 2;
    models.vertexBuffers = kBuffers.data();
    models.vertexBufferCount = static_cast<std::uint32_t>(kBuffers.size());
    models.vertexAttributes = kAttributes.data();
    models.vertexAttributeCount = static_cast<std::uint32_t>(kAttributes.size());
    // A model scaled negatively turns inside out, so no face is culled.
    models.cullMode = mrhi_cullNone;
    models.depthStencilFormat = kDepthFormat;
    // The depth prepass: nearer is greater (reversed-Z).
    mrhiGraphicsPipelineDef prepass = models;
    constexpr std::string_view kDepthLabel = "rawframe.scene.depth";
    prepass.label = kDepthLabel.data();
    prepass.labelLength = kDepthLabel.size();
    prepass.depthWrite = true;
    prepass.depthCompare = mrhi_compareGreater;
    prepass.colorTargetCount = 0;
    RAWFRAME_TRY(ask(prepass, depth));
    mrhiGraphicsPipelineDef cutDef = prepass;
    constexpr std::string_view kCutLabel = "rawframe.scene.depth.masked";
    cutDef.label = kCutLabel.data();
    cutDef.labelLength = kCutLabel.size();
    cutDef.fragmentEntry = "cut";
    cutDef.fragmentEntryLength = 3;
    RAWFRAME_TRY(ask(cutDef, cutout));
    // The casters into the sun's shadow map: the vertex's place alone,
    // pushed from the sun by its slope (the depth half of ADR-0051's
    // bias; the normal half is where the map is read).
    constexpr std::array<mrhiVertexAttribute, 4> kCasterAttributes = {
        kAttributes[0], kAttributes[3], kAttributes[4], kAttributes[5]};
    mrhiGraphicsPipelineDef casters = prepass;
    constexpr std::string_view kCastingLabel = "rawframe.scene.shadows";
    casters.label = kCastingLabel.data();
    casters.labelLength = kCastingLabel.size();
    casters.shader = shadowShader;
    casters.vertexAttributes = kCasterAttributes.data();
    casters.vertexAttributeCount = static_cast<std::uint32_t>(kCasterAttributes.size());
    casters.depthStencilFormat = kShadowFormat;
    casters.depthBiasSlopeScale = -2.0F;
    RAWFRAME_TRY(ask(casters, casting));
    // Masked casters: their color, material, and texture coordinates too.
    constexpr std::array<mrhiVertexAttribute, 7> kCutAttributes = {kAttributes[0],
                                                                   kAttributes[3],
                                                                   kAttributes[4],
                                                                   kAttributes[5],
                                                                   kAttributes[9],
                                                                   kAttributes[13],
                                                                   kAttributes[2]};
    mrhiGraphicsPipelineDef cutCasters = casters;
    constexpr std::string_view kCutCastingLabel = "rawframe.scene.shadows.masked";
    cutCasters.label = kCutCastingLabel.data();
    cutCasters.labelLength = kCutCastingLabel.size();
    cutCasters.vertexEntry = "vsCut";
    cutCasters.vertexEntryLength = 5;
    cutCasters.fragmentEntry = "cut";
    cutCasters.fragmentEntryLength = 3;
    cutCasters.vertexAttributes = kCutAttributes.data();
    cutCasters.vertexAttributeCount = static_cast<std::uint32_t>(kCutAttributes.size());
    RAWFRAME_TRY(ask(cutCasters, cutCasting));
    mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
    samplerDef.magFilter = mrhi_filterLinear;
    samplerDef.minFilter = mrhi_filterLinear;
    samplerDef.addressU = mrhi_addressClampToEdge;
    samplerDef.addressV = mrhi_addressClampToEdge;
    samplerDef.addressW = mrhi_addressClampToEdge;
    // Lit where the point is at least as near the sun as the nearest
    // caster (reversed-Z).
    samplerDef.compare = mrhi_compareGreaterEqual;
    if (const mrhiResult kMade = mrhiCreateSampler(native, &samplerDef, &shadowSampler); kMade != mrhi_success) {
        return failed("the shadow sampler could not be made", kMade);
    }
    // A material's texture's (D309): across its levels as it is filtered,
    // repeated or clamped.
    for (const material::Filter kFilter : {material::Filter::Linear, material::Filter::Nearest}) {
        for (const material::Address kAddress : {material::Address::Repeat, material::Address::Clamp}) {
            mrhiSamplerDef def = mrhiDefaultSamplerDef();
            const mrhiFilter kHow = kFilter == material::Filter::Linear ? mrhi_filterLinear : mrhi_filterNearest;
            def.magFilter = kHow;
            def.minFilter = kHow;
            def.mipFilter = kHow;
            const mrhiAddressMode kAt =
                kAddress == material::Address::Repeat ? mrhi_addressRepeat : mrhi_addressClampToEdge;
            def.addressU = kAt;
            def.addressV = kAt;
            def.addressW = kAt;
            if (const mrhiResult kMade =
                    mrhiCreateSampler(native, &def, &materialSamplers[samplerOf(kFilter, kAddress)]);
                kMade != mrhi_success) {
                return failed("a material's sampler could not be made", kMade);
            }
        }
    }
    // The lit models, drawn where the prepass left their depth.
    constexpr std::string_view kLitLabel = "rawframe.scene.models";
    models.label = kLitLabel.data();
    models.labelLength = kLitLabel.size();
    models.fragmentEntry = "fs";
    models.fragmentEntryLength = 2;
    models.depthWrite = false;
    models.depthCompare = mrhi_compareGreaterEqual;
    models.colorTargetCount = 2;
    models.colorTargets[0].format = kSceneFormat;
    models.colorTargets[1].format = kMotionFormat;
    RAWFRAME_TRY(ask(models, lit));
    mrhiGraphicsPipelineDef maskedDef = models;
    constexpr std::string_view kMaskedLabel = "rawframe.scene.models.masked";
    maskedDef.label = kMaskedLabel.data();
    maskedDef.labelLength = kMaskedLabel.size();
    maskedDef.depthCompare = mrhi_compareEqual;
    RAWFRAME_TRY(ask(maskedDef, maskedLit));
    // The translucent models (D305): over the opaque ones and the sky,
    // tested against their depth but writing none, blended by their
    // opacity, and leaving the motion to what is behind them.
    mrhiGraphicsPipelineDef glassDef = models;
    constexpr std::string_view kGlassLabel = "rawframe.scene.translucent";
    glassDef.label = kGlassLabel.data();
    glassDef.labelLength = kGlassLabel.size();
    glassDef.colorTargets[0].blend = true;
    glassDef.colorTargets[0].color = {
        .srcFactor = mrhi_blendSrcAlpha, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
    glassDef.colorTargets[0].alpha = {
        .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
    glassDef.colorTargets[1].writeMask = 0;
    RAWFRAME_TRY(ask(glassDef, glass));
    // The sky, where no model's depth lies: a triangle over the target at
    // reversed-Z's far end, into the models' targets.
    mrhiGraphicsPipelineDef behind = mrhiDefaultGraphicsPipelineDef();
    constexpr std::string_view kSkyLabel = "rawframe.scene.sky";
    behind.label = kSkyLabel.data();
    behind.labelLength = kSkyLabel.size();
    behind.shader = skyShader;
    behind.vertexEntry = "vs";
    behind.vertexEntryLength = 2;
    behind.fragmentEntry = "fs";
    behind.fragmentEntryLength = 2;
    behind.cullMode = mrhi_cullNone;
    behind.depthStencilFormat = kDepthFormat;
    behind.depthWrite = false;
    behind.depthCompare = mrhi_compareGreaterEqual;
    behind.colorTargetCount = 2;
    behind.colorTargets[0].format = kSceneFormat;
    behind.colorTargets[1].format = kMotionFormat;
    RAWFRAME_TRY(ask(behind, sky));
    // The metering (D293): the frame's histogram, then the exposure moved.
    for (const auto& [kEntry, kAsked] :
         {std::pair{std::string_view{"histogram"}, &histogram}, std::pair{std::string_view{"adapt"}, &adapt}}) {
        mrhiComputePipelineDef metering = mrhiDefaultComputePipelineDef();
        metering.shader = meterShader;
        metering.entry = kEntry.data();
        metering.entryLength = kEntry.size();
        RAWFRAME_TRY(ask(metering, *kAsked));
    }
    // The temporal pass: the frame and the picture before, into the
    // picture kept for the next.
    mrhiGraphicsPipelineDef resolving = mrhiDefaultGraphicsPipelineDef();
    constexpr std::string_view kTemporalLabel = "rawframe.scene.temporal";
    resolving.label = kTemporalLabel.data();
    resolving.labelLength = kTemporalLabel.size();
    resolving.shader = temporalShader;
    resolving.vertexEntry = "vs";
    resolving.vertexEntryLength = 2;
    resolving.fragmentEntry = "fs";
    resolving.fragmentEntryLength = 2;
    resolving.colorTargetCount = 1;
    resolving.colorTargets[0].format = kSceneFormat;
    RAWFRAME_TRY(ask(resolving, temporal));
    mrhiSamplerDef blendingDef = mrhiDefaultSamplerDef();
    blendingDef.magFilter = mrhi_filterLinear;
    blendingDef.minFilter = mrhi_filterLinear;
    blendingDef.addressU = mrhi_addressClampToEdge;
    blendingDef.addressV = mrhi_addressClampToEdge;
    blendingDef.addressW = mrhi_addressClampToEdge;
    if (const mrhiResult kMade = mrhiCreateSampler(native, &blendingDef, &filteredSampler); kMade != mrhi_success) {
        return failed("the filtering sampler could not be made", kMade);
    }
    mrhiGraphicsPipelineDef picture = mrhiDefaultGraphicsPipelineDef();
    constexpr std::string_view kPictureLabel = "rawframe.scene.tonemap";
    picture.label = kPictureLabel.data();
    picture.labelLength = kPictureLabel.size();
    picture.shader = tonemapShader;
    picture.vertexEntry = "vs";
    picture.vertexEntryLength = 2;
    picture.fragmentEntry = "fs";
    picture.fragmentEntryLength = 2;
    picture.colorTargetCount = 1;
    picture.colorTargets[0].format = kPictureFormat;
    RAWFRAME_TRY(ask(picture, tonemap));
    // FXAA: the tonemapped picture into the frame's (D296).
    mrhiGraphicsPipelineDef smoothed = picture;
    constexpr std::string_view kFxaaLabel = "rawframe.scene.fxaa";
    smoothed.label = kFxaaLabel.data();
    smoothed.labelLength = kFxaaLabel.size();
    smoothed.shader = fxaaShader;
    return ask(smoothed, fxaa);
}

result::Result<bool> Pipelines::ready() {
    bool all = true;
    for (Asked* asked : {&casting,
                         &cutCasting,
                         &depth,
                         &cutout,
                         &lit,
                         &maskedLit,
                         &glass,
                         &sky,
                         &histogram,
                         &adapt,
                         &temporal,
                         &tonemap,
                         &fxaa}) {
        if (!asked->ready) {
            if (const auto kAnswer = device->answer(asked->request)) {
                if (!kAnswer->has_value()) {
                    return std::unexpected<result::Error>{kAnswer->error().clone()};
                }
                asked->ready = true;
            }
        }
        all = all && asked->ready;
    }
    return all;
}

} // namespace rawframe::render_scene_gpu
