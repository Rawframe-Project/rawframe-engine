#include "pipelines.h"

#include "blocks.h"
#include "generated/bloom_container.h"
#include "generated/contact_container.h"
#include "generated/decal_container.h"
#include "generated/focus_container.h"
#include "generated/fxaa_container.h"
#include "generated/meter_container.h"
#include "generated/motion_container.h"
#include "generated/occlusion_container.h"
#include "generated/post_container.h"
#include "generated/probe_container.h"
#include "generated/reflect_container.h"
#include "generated/resolve_container.h"
#include "generated/scene_container.h"
#include "generated/shadow_container.h"
#include "generated/sky_container.h"
#include "generated/temporal_container.h"
#include "generated/tonemap_container.h"
#include "rawframe/render_scene_gpu/errors.h"
#include "rawframe/render_scene_gpu/renderer.h"

#include <array>
#include <string>
#include <tuple>

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
    for (Asked* asked : {&casting,
                         &cutCasting,
                         &depth,
                         &cutout,
                         &surfaces,
                         &cutSurfaces,
                         &occlude,
                         &blurOcclusion,
                         &march,
                         &motionTiles,
                         &motionNeighbors,
                         &motionGather,
                         &focusPrefilter,
                         &focusBokeh,
                         &focusCombine,
                         &contactShade,
                         &decalFill,
                         &litDecaled,
                         &maskedLitDecaled,
                         &glassDecaled,
                         &probeFill,
                         &decalNormalFill,
                         &lit,
                         &maskedLit,
                         &glass,
                         &sky,
                         &temporal,
                         &tonemap,
                         &fxaa,
                         &postLinear,
                         &postDisplay,
                         &grade,
                         &bloomFirst,
                         &bloomDown,
                         &bloomUp,
                         &multisampled.depth,
                         &multisampled.cutout,
                         &multisampled.surfaces,
                         &multisampled.cutSurfaces,
                         &multisampled.lit,
                         &multisampled.maskedLit,
                         &multisampled.glass,
                         &multisampled.litDecaled,
                         &multisampled.maskedLitDecaled,
                         &multisampled.glassDecaled,
                         &multisampled.sky,
                         &multisampled.resolveDepth}) {
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
    for (const mrhiShaderId kShader : {sceneShader,
                                       tonemapShader,
                                       shadowShader,
                                       temporalShader,
                                       skyShader,
                                       meterShader,
                                       fxaaShader,
                                       occlusionShader,
                                       bloomShader,
                                       reflectShader,
                                       motionShader,
                                       focusShader,
                                       contactShader,
                                       decalShader,
                                       probeShader,
                                       resolveShader,
                                       postShader}) {
        static_cast<void>(mrhiDestroyShader(native, kShader));
    }
    for (auto& [kProgram, shading] : programs) {
        for (std::size_t at = 0; at < shading.variants.size(); ++at) {
            if (shading.asked.at(at)) {
                static_cast<void>(mrhiDestroyGraphicsPipeline(native, shading.variants.at(at).pipeline));
            }
        }
        static_cast<void>(mrhiDestroyShader(native, shading.shader));
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

result::Status Pipelines::refused(mrhiResult outcome, const char* label, std::size_t labelLength) {
    result::Error error = failed("a scene pipeline could not be asked for", outcome).error();
    if (labelLength > 0) {
        error = std::move(error).withContext("pipeline", std::string{label, labelLength});
    }
    // Maul RHI's own word on an input it refused (mrhi-0027).
    mrhiDiagnostic diagnostic{};
    if (mrhiNextDeviceDiagnostic(native, &diagnostic) == mrhi_success) {
        error = std::move(error).withContext("diagnostic", mrhiDiagnosticText(diagnostic.code));
    }
    return std::unexpected<result::Error>{std::move(error)};
}

result::Status Pipelines::ask(const mrhiGraphicsPipelineDef& def, Asked& asked) {
    mrhiRequestId request{};
    if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &asked.pipeline, &request);
        kMade != mrhi_success) {
        return refused(kMade, def.label, def.labelLength);
    }
    asked.request = render::requestKey(request.index1, request.generation);
    return {};
}

result::Status Pipelines::ask(const mrhiComputePipelineDef& def, Asked& asked) {
    mrhiRequestId request{};
    if (const mrhiResult kMade = mrhiCreateComputePipeline(native, &def, &asked.compute, &request);
        kMade != mrhi_success) {
        return refused(kMade, def.label, def.labelLength);
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
    // Each vertex of the mesh, then each draw's placement.
    static constexpr std::array<mrhiVertexBufferLayout, 2> kBuffers = {
        mrhiVertexBufferLayout{.stride = kVertexBytes, .stepMode = mrhi_stepVertex},
        mrhiVertexBufferLayout{.stride = kInstanceBytes, .stepMode = mrhi_stepInstance}};
    static constexpr std::array<mrhiVertexAttribute, 15> kAttributes = {
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
        mrhiVertexAttribute{.buffer = 0, .location = 14, .format = mrhi_vertexFloat32x4, .offset = 32}};
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
    // What the effects' pipelines are made from when a view first wants
    // them (D337).
    prepass_ = prepass;
    shading_ = {models, maskedDef, glassDef};
    cut_ = cutDef;
    sky_ = behind;
    picture_ = picture;
    return {};
}

namespace {

/// The prepass also leaving each point's surface (D327, D331), whole and
/// masked, from the prepass's pipeline.
std::array<mrhiGraphicsPipelineDef, 2> surfacing(const mrhiGraphicsPipelineDef& prepass) {
    mrhiGraphicsPipelineDef surfacesDef = prepass;
    constexpr std::string_view kSurfacesLabel = "rawframe.scene.depth.surfaces";
    surfacesDef.label = kSurfacesLabel.data();
    surfacesDef.labelLength = kSurfacesLabel.size();
    surfacesDef.fragmentEntry = "normal";
    surfacesDef.fragmentEntryLength = 6;
    surfacesDef.colorTargetCount = 1;
    surfacesDef.colorTargets[0].format = kSurfaceFormat;
    mrhiGraphicsPipelineDef cutSurfacesDef = surfacesDef;
    constexpr std::string_view kCutSurfacesLabel = "rawframe.scene.depth.surfaces.masked";
    cutSurfacesDef.label = kCutSurfacesLabel.data();
    cutSurfacesDef.labelLength = kCutSurfacesLabel.size();
    cutSurfacesDef.fragmentEntry = "cutNormal";
    cutSurfacesDef.fragmentEntryLength = 9;
    return {surfacesDef, cutSurfacesDef};
}

} // namespace

mrhiGraphicsPipelineDef decaledOf(mrhiGraphicsPipelineDef def, std::string_view label) {
    def.label = label.data();
    def.labelLength = label.size();
    def.fragmentEntry = "fsDecaled";
    def.fragmentEntryLength = 9;
    return def;
}

result::Status Pipelines::askFor(Effect effect) {
    switch (effect) {
    case Effect::Surfaces: {
        // The prepass also leaving each point's surface, for the ambient
        // occlusion and the reflections (D327, D331): whole, and masked.
        const auto [kSurfacesDef, kCutSurfacesDef] = surfacing(prepass_);
        RAWFRAME_TRY(ask(kSurfacesDef, surfaces));
        return ask(kCutSurfacesDef, cutSurfaces);
    }
    case Effect::Multisampled: {
        // The models' passes taking `samples` a pixel (D343): every
        // pipeline drawing into their targets, the same but for that.
        const auto [kSurfacesDef, kCutSurfacesDef] = surfacing(prepass_);
        const std::array<std::pair<mrhiGraphicsPipelineDef, Asked*>, 11> kSampled = {
            std::pair{prepass_, &multisampled.depth},
            std::pair{cut_, &multisampled.cutout},
            std::pair{kSurfacesDef, &multisampled.surfaces},
            std::pair{kCutSurfacesDef, &multisampled.cutSurfaces},
            std::pair{shading_[0], &multisampled.lit},
            std::pair{shading_[1], &multisampled.maskedLit},
            std::pair{shading_[2], &multisampled.glass},
            std::pair{decaledOf(shading_[0], "rawframe.scene.lit.decaled"), &multisampled.litDecaled},
            std::pair{decaledOf(shading_[1], "rawframe.scene.lit.masked.decaled"), &multisampled.maskedLitDecaled},
            std::pair{decaledOf(shading_[2], "rawframe.scene.glass.decaled"), &multisampled.glassDecaled},
            std::pair{sky_, &multisampled.sky}};
        for (const auto& [kDef, kAsked] : kSampled) {
            mrhiGraphicsPipelineDef sampled = kDef;
            sampled.sampleCount = samples;
            RAWFRAME_TRY(ask(sampled, *kAsked));
        }
        // The prepass's depth, its first sample, to one sample.
        RAWFRAME_TRY(makeShader(kResolveContainer, resolveShader));
        mrhiGraphicsPipelineDef resolve = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.scene.depth.resolve";
        resolve.label = kLabel.data();
        resolve.labelLength = kLabel.size();
        resolve.shader = resolveShader;
        resolve.vertexEntry = "vs";
        resolve.vertexEntryLength = 2;
        resolve.fragmentEntry = "depth";
        resolve.fragmentEntryLength = 5;
        resolve.cullMode = mrhi_cullNone;
        resolve.depthStencilFormat = kDepthFormat;
        resolve.depthWrite = true;
        resolve.depthCompare = mrhi_compareAlways;
        resolve.colorTargetCount = 0;
        return ask(resolve, multisampled.resolveDepth);
    }
    case Effect::Occlusion: {
        RAWFRAME_TRY(makeShader(kOcclusionContainer, occlusionShader));
        // The ambient occlusion from the prepass's depth and surfaces, then
        // blurred, each a triangle over the target.
        for (const auto& [kEntry, kLabel, kAsked] :
             {std::tuple{std::string_view{"occlude"}, std::string_view{"rawframe.scene.occlusion"}, &occlude},
              std::tuple{
                  std::string_view{"blur"}, std::string_view{"rawframe.scene.occlusion.blur"}, &blurOcclusion}}) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.shader = occlusionShader;
            def.vertexEntry = "vs";
            def.vertexEntryLength = 2;
            def.fragmentEntry = kEntry.data();
            def.fragmentEntryLength = kEntry.size();
            def.colorTargetCount = 1;
            def.colorTargets[0].format = kAmbientFormat;
            RAWFRAME_TRY(ask(def, *kAsked));
        }
        return {};
    }
    case Effect::Reflections: {
        RAWFRAME_TRY(makeShader(kReflectContainer, reflectShader));
        // The screen-space reflections from the prepass's depth and surfaces
        // and the picture before (D331), a triangle over the target.
        mrhiGraphicsPipelineDef marching = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kMarchLabel = "rawframe.scene.reflections";
        marching.label = kMarchLabel.data();
        marching.labelLength = kMarchLabel.size();
        marching.shader = reflectShader;
        marching.vertexEntry = "vs";
        marching.vertexEntryLength = 2;
        marching.fragmentEntry = "march";
        marching.fragmentEntryLength = 5;
        marching.colorTargetCount = 1;
        marching.colorTargets[0].format = kReflectionFormat;
        RAWFRAME_TRY(ask(marching, march));
        return {};
    }
    case Effect::MotionBlur: {
        RAWFRAME_TRY(makeShader(kMotionContainer, motionShader));
        // The motion blur's tiles and their neighbors, then its gathering, each
        // a triangle over its target (D334).
        for (const auto& [kEntry, kLabel, kFormat, kAsked] :
             {std::tuple{std::string_view{"tile"},
                         std::string_view{"rawframe.scene.motion.tiles"},
                         kMotionFormat,
                         &motionTiles},
              std::tuple{std::string_view{"neighbor"},
                         std::string_view{"rawframe.scene.motion.neighbors"},
                         kMotionFormat,
                         &motionNeighbors},
              std::tuple{std::string_view{"gather"},
                         std::string_view{"rawframe.scene.motion.gather"},
                         kSceneFormat,
                         &motionGather}}) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.shader = motionShader;
            def.vertexEntry = "vs";
            def.vertexEntryLength = 2;
            def.fragmentEntry = kEntry.data();
            def.fragmentEntryLength = kEntry.size();
            def.colorTargetCount = 1;
            def.colorTargets[0].format = kFormat;
            RAWFRAME_TRY(ask(def, *kAsked));
        }
        return {};
    }
    case Effect::DepthOfField: {
        RAWFRAME_TRY(makeShader(kFocusContainer, focusShader));
        // The depth of field's halving and bokeh at half size, then its blend,
        // each a triangle over its target (D336).
        for (const auto& [kEntry, kLabel, kAsked] :
             {std::tuple{
                  std::string_view{"prefilter"}, std::string_view{"rawframe.scene.focus.halved"}, &focusPrefilter},
              std::tuple{std::string_view{"bokeh"}, std::string_view{"rawframe.scene.focus.bokeh"}, &focusBokeh},
              std::tuple{
                  std::string_view{"combine"}, std::string_view{"rawframe.scene.focus.combined"}, &focusCombine}}) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.shader = focusShader;
            def.vertexEntry = "vs";
            def.vertexEntryLength = 2;
            def.fragmentEntry = kEntry.data();
            def.fragmentEntryLength = kEntry.size();
            def.colorTargetCount = 1;
            def.colorTargets[0].format = kSceneFormat;
            RAWFRAME_TRY(ask(def, *kAsked));
        }
        return {};
    }
    case Effect::Bloom: {
        RAWFRAME_TRY(makeShader(kBloomContainer, bloomShader));
        // The bloom's chain (D328): halvings, then doublings added to the
        // level above.
        for (const auto& [kEntry, kLabel, kAsked] :
             {std::tuple{std::string_view{"first"}, std::string_view{"rawframe.scene.bloom.first"}, &bloomFirst},
              std::tuple{std::string_view{"down"}, std::string_view{"rawframe.scene.bloom.down"}, &bloomDown},
              std::tuple{std::string_view{"up"}, std::string_view{"rawframe.scene.bloom.up"}, &bloomUp}}) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.shader = bloomShader;
            def.vertexEntry = "vs";
            def.vertexEntryLength = 2;
            def.fragmentEntry = kEntry.data();
            def.fragmentEntryLength = kEntry.size();
            def.colorTargetCount = 1;
            def.colorTargets[0].format = kSceneFormat;
            if (kAsked == &bloomUp) {
                def.colorTargets[0].blend = true;
                def.colorTargets[0].color = {
                    .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd};
                def.colorTargets[0].alpha = {
                    .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendZero, .operation = mrhi_blendAdd};
            }
            RAWFRAME_TRY(ask(def, *kAsked));
        }
        return {};
    }
    case Effect::ContactShadows: {
        // The contact shadows from the prepass's depth (D338), a triangle
        // over the target.
        RAWFRAME_TRY(makeShader(kContactContainer, contactShader));
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.scene.contact";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = contactShader;
        def.vertexEntry = "vs";
        def.vertexEntryLength = 2;
        def.fragmentEntry = "shade";
        def.fragmentEntryLength = 5;
        def.colorTargetCount = 1;
        def.colorTargets[0].format = kAmbientFormat;
        return ask(def, contactShade);
    }
    case Effect::Decals: {
        // A decal's texture into one mip of its layer of the atlas (D339).
        RAWFRAME_TRY(makeShader(kDecalContainer, decalShader));
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.scene.decals.fill";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = decalShader;
        def.vertexEntry = "vs";
        def.vertexEntryLength = 2;
        def.fragmentEntry = "fill";
        def.fragmentEntryLength = 4;
        def.colorTargetCount = 1;
        def.colorTargets[0].format = kPictureFormat;
        RAWFRAME_TRY(ask(def, decalFill));
        // A decal's normal texture into the normals' atlas (D342).
        constexpr std::string_view kNormalLabel = "rawframe.scene.decals.fill.normal";
        def.label = kNormalLabel.data();
        def.labelLength = kNormalLabel.size();
        def.colorTargets[0].format = kNormalsFormat;
        RAWFRAME_TRY(ask(def, decalNormalFill));
        // The lit, masked, and translucent models under the decals.
        constexpr std::array<std::string_view, 3> kLabels = {
            "rawframe.scene.lit.decaled", "rawframe.scene.lit.masked.decaled", "rawframe.scene.glass.decaled"};
        const std::array<Asked*, 3> kDecaled = {&litDecaled, &maskedLitDecaled, &glassDecaled};
        for (std::size_t at = 0; at < kDecaled.size(); ++at) {
            RAWFRAME_TRY(ask(decaledOf(shading_[at], kLabels[at]), *kDecaled[at]));
        }
        return {};
    }
    case Effect::Probes: {
        // A probe's picture into one face's mip of its cube of the atlas
        // (D340).
        RAWFRAME_TRY(makeShader(kProbeContainer, probeShader));
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.scene.probes.fill";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = probeShader;
        def.vertexEntry = "vs";
        def.vertexEntryLength = 2;
        def.fragmentEntry = "fill";
        def.fragmentEntryLength = 4;
        def.colorTargetCount = 1;
        def.colorTargets[0].format = kProbeFormat;
        return ask(def, probeFill);
    }
    case Effect::PostProcess: {
        RAWFRAME_TRY(makeShader(kPostContainer, postShader));
        // A post process over the chain's light, or over the picture
        // (D350); and the picture graded only, from the picture's shader.
        mrhiGraphicsPipelineDef def = picture_;
        def.shader = postShader;
        for (const auto& [kLabel, kFormat, kAsked] :
             {std::tuple{std::string_view{"rawframe.scene.post.linear"}, kSceneFormat, &postLinear},
              std::tuple{std::string_view{"rawframe.scene.post.display"}, kPictureFormat, &postDisplay}}) {
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.colorTargets[0].format = kFormat;
            RAWFRAME_TRY(ask(def, *kAsked));
        }
        mrhiGraphicsPipelineDef graded = picture_;
        constexpr std::string_view kGradeLabel = "rawframe.scene.grade";
        graded.label = kGradeLabel.data();
        graded.labelLength = kGradeLabel.size();
        graded.colorTargets[0].format = kSceneFormat;
        return ask(graded, grade);
    }
    case Effect::Fxaa: {
        RAWFRAME_TRY(makeShader(kFxaaContainer, fxaaShader));
        // FXAA: the tonemapped picture into the frame's (D296).
        mrhiGraphicsPipelineDef smoothed = picture_;
        constexpr std::string_view kFxaaLabel = "rawframe.scene.fxaa";
        smoothed.label = kFxaaLabel.data();
        smoothed.labelLength = kFxaaLabel.size();
        smoothed.shader = fxaaShader;
        return ask(smoothed, fxaa);
    }
    }
    return {};
}

result::Result<bool> Pipelines::answered(std::initializer_list<Asked*> pipelines) {
    bool all = true;
    for (Asked* asked : pipelines) {
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

result::Result<bool> Pipelines::ready() {
    return answered({&casting,
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
                     &tonemap});
}

result::Result<bool> Pipelines::wanted(Effect effect) {
    const auto kAt = static_cast<std::size_t>(effect);
    if (!asked_[kAt]) {
        asked_[kAt] = true;
        RAWFRAME_TRY(askFor(effect));
    }
    switch (effect) {
    case Effect::Surfaces:
        return answered({&surfaces, &cutSurfaces});
    case Effect::Occlusion:
        return answered({&occlude, &blurOcclusion});
    case Effect::Reflections:
        return answered({&march});
    case Effect::MotionBlur:
        return answered({&motionTiles, &motionNeighbors, &motionGather});
    case Effect::DepthOfField:
        return answered({&focusPrefilter, &focusBokeh, &focusCombine});
    case Effect::Bloom:
        return answered({&bloomFirst, &bloomDown, &bloomUp});
    case Effect::Fxaa:
        return answered({&fxaa});
    case Effect::PostProcess:
        return answered({&postLinear, &postDisplay, &grade});
    case Effect::ContactShadows:
        return answered({&contactShade});
    case Effect::Decals:
        return answered({&decalFill, &decalNormalFill, &litDecaled, &maskedLitDecaled, &glassDecaled});
    case Effect::Probes:
        return answered({&probeFill});
    case Effect::Multisampled: {
        Multisampled& sampled = multisampled;
        return answered({&sampled.depth,
                         &sampled.cutout,
                         &sampled.surfaces,
                         &sampled.cutSurfaces,
                         &sampled.lit,
                         &sampled.maskedLit,
                         &sampled.glass,
                         &sampled.litDecaled,
                         &sampled.maskedLitDecaled,
                         &sampled.glassDecaled,
                         &sampled.sky,
                         &sampled.resolveDepth});
    }
    }
    return false;
}

} // namespace rawframe::render_scene_gpu
