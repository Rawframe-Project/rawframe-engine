#include "rawframe/render_scene_gpu/renderer.h"

#include "generated/scene_container.h"
#include "generated/shadow_container.h"
#include "generated/tonemap_container.h"
#include "rawframe/render_scene_gpu/errors.h"

#include <cmath>
#include <cstring>
#include <map>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, SceneGpuError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kSceneGpuDomain, code(error), why).error()};
}

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kSceneGpuDomain, code(SceneGpuError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

/// A vertex as the scene pipeline reads it: its position, then its normal.
constexpr std::uint32_t kVertexBytes = 24;

/// One column-major matrix, as a cascade's view is written.
using Matrix4 = std::array<float, 16>;

/// The frame's view and light as the scene's shaders read them (std140).
struct FrameBlock {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> toSun{};
    std::array<float, 4> sun{};
    std::array<float, 4> sky{};
    std::array<float, 4> exposure{};
    /// The eye's forward; each cascade's far end and texel; the cascades,
    /// the shadows' distance, and a cascade's side (D289).
    std::array<float, 4> forward{};
    std::array<float, 4> cascadeFar{};
    std::array<float, 4> cascadeTexel{};
    std::array<float, 4> shadow{};
    std::array<Matrix4, 4> cascades{};
};
static_assert(sizeof(FrameBlock) == 448, "the scene's shaders read the frame as 448 bytes");

/// The sun's shadow map (D289): the cascades' squares, two by two.
constexpr mrhiFormat kShadowFormat = mrhi_formatDepth32Float;

constexpr mrhiFormat kSceneFormat = mrhi_formatRgba16Float;
constexpr mrhiFormat kDepthFormat = mrhi_formatDepth32Float;
/// The frame's picture, as `render` declares it.
constexpr mrhiFormat kPictureFormat = mrhi_formatRgba8UnormSrgb;

/// A resource of the open frame from the key `render` names it by.
mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

/// The light an EV100 exposes to one (ADR-0047): a sensor's saturation at
/// 1.2 times two to the EV100.
float exposureOf(float ev100) noexcept {
    return 1.0F / (1.2F * std::exp2(ev100));
}

/// A mesh's vertices as the pipeline reads them, its normals made from its
/// faces where it has none.
std::vector<float> verticesOf(const mesh::Mesh& made) {
    std::vector<mesh::Vector3> normals = made.normals;
    if (normals.size() != made.positions.size()) {
        normals.assign(made.positions.size(), mesh::Vector3{0, 0, 0});
        for (std::size_t at = 0; at + 2 < made.indices.size(); at += 3) {
            const mesh::Vector3& kA = made.positions[made.indices[at]];
            const mesh::Vector3& kB = made.positions[made.indices[at + 1]];
            const mesh::Vector3& kC = made.positions[made.indices[at + 2]];
            const mesh::Vector3 kAb = {kB[0] - kA[0], kB[1] - kA[1], kB[2] - kA[2]};
            const mesh::Vector3 kAc = {kC[0] - kA[0], kC[1] - kA[1], kC[2] - kA[2]};
            // Weighted by the face's area, as its cross product is.
            const mesh::Vector3 kFace = {(kAb[1] * kAc[2]) - (kAb[2] * kAc[1]),
                                         (kAb[2] * kAc[0]) - (kAb[0] * kAc[2]),
                                         (kAb[0] * kAc[1]) - (kAb[1] * kAc[0])};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                mesh::Vector3& normal = normals[made.indices[at + corner]];
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    normal[axis] += kFace[axis];
                }
            }
        }
    }
    std::vector<float> vertices;
    vertices.reserve(made.positions.size() * 6);
    for (std::size_t at = 0; at < made.positions.size(); ++at) {
        vertices.insert(vertices.end(), made.positions[at].begin(), made.positions[at].end());
        vertices.insert(vertices.end(), normals[at].begin(), normals[at].end());
    }
    return vertices;
}

std::uint64_t bytesOf(const mesh::Mesh& made) noexcept {
    return (std::uint64_t{made.positions.size()} * kVertexBytes) + (std::uint64_t{made.indices.size()} * 4);
}

/// A mesh held on the device: the mesh it was made from, and whether its
/// vertices and indices are there yet.
struct Held {
    std::shared_ptr<const mesh::Mesh> source;
    mrhiBufferId vertices{};
    mrhiBufferId indices{};
    bool uploaded = false;
};

/// A pipeline asked of the device, and whether it is made.
struct Asked {
    mrhiGraphicsPipelineId pipeline{};
    std::uint64_t request = 0;
    bool ready = false;
};

} // namespace

struct SceneRenderer::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    RendererLimits limits;
    RendererStatistics statistics;
    mrhiShaderId sceneShader{};
    mrhiShaderId tonemapShader{};
    mrhiShaderId shadowShader{};
    /// Compares a shadow map's depths, blending four (hardware 2x2 PCF).
    mrhiSamplerId shadowSampler{};
    /// The shadow map's casters, the depth prepass, the lit models, and the
    /// picture.
    Asked casting;
    Asked depth;
    Asked lit;
    Asked tonemap;
    std::map<std::uint64_t, Held> held;
    /// What the next frame draws.
    const render_scene::SceneFrame* frame = nullptr;
    MeshSource meshes;

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        for (auto& [id, made] : held) {
            static_cast<void>(mrhiDestroyBuffer(native, made.vertices));
            static_cast<void>(mrhiDestroyBuffer(native, made.indices));
        }
        for (Asked* asked : {&casting, &depth, &lit, &tonemap}) {
            static_cast<void>(mrhiDestroyGraphicsPipeline(native, asked->pipeline));
        }
        static_cast<void>(mrhiDestroySampler(native, shadowSampler));
        static_cast<void>(mrhiDestroyShader(native, sceneShader));
        static_cast<void>(mrhiDestroyShader(native, tonemapShader));
        static_cast<void>(mrhiDestroyShader(native, shadowShader));
    }

    result::Status makeShader(std::span<const std::uint8_t> container, mrhiShaderId& shader) {
        mrhiShaderDef def = mrhiDefaultShaderDef();
        def.bytes = container.data();
        def.byteCount = container.size();
        if (const mrhiResult kMade = mrhiCreateShader(native, &def, &shader); kMade != mrhi_success) {
            return failed("a scene shader could not be made", kMade);
        }
        return {};
    }

    result::Status ask(const mrhiGraphicsPipelineDef& def, Asked& asked) {
        mrhiRequestId request{};
        if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &asked.pipeline, &request);
            kMade != mrhi_success) {
            return failed("a scene pipeline could not be asked for", kMade);
        }
        asked.request = render::requestKey(request.index1, request.generation);
        return {};
    }

    result::Status makePipelines() {
        RAWFRAME_TRY(makeShader(kSceneContainer, sceneShader));
        RAWFRAME_TRY(makeShader(kTonemapContainer, tonemapShader));
        RAWFRAME_TRY(makeShader(kShadowContainer, shadowShader));
        // Each vertex of the mesh, then each draw's placement.
        constexpr std::array<mrhiVertexBufferLayout, 2> kBuffers = {
            mrhiVertexBufferLayout{.stride = kVertexBytes, .stepMode = mrhi_stepVertex},
            mrhiVertexBufferLayout{.stride = kInstanceBytes, .stepMode = mrhi_stepInstance}};
        constexpr std::array<mrhiVertexAttribute, 9> kAttributes = {
            mrhiVertexAttribute{.buffer = 0, .location = 0, .format = mrhi_vertexFloat32x3, .offset = 0},
            mrhiVertexAttribute{.buffer = 0, .location = 1, .format = mrhi_vertexFloat32x3, .offset = 12},
            mrhiVertexAttribute{.buffer = 1, .location = 2, .format = mrhi_vertexFloat32x4, .offset = 0},
            mrhiVertexAttribute{.buffer = 1, .location = 3, .format = mrhi_vertexFloat32x4, .offset = 16},
            mrhiVertexAttribute{.buffer = 1, .location = 4, .format = mrhi_vertexFloat32x4, .offset = 32},
            mrhiVertexAttribute{.buffer = 1, .location = 5, .format = mrhi_vertexFloat32x3, .offset = 48},
            mrhiVertexAttribute{.buffer = 1, .location = 6, .format = mrhi_vertexFloat32x3, .offset = 60},
            mrhiVertexAttribute{.buffer = 1, .location = 7, .format = mrhi_vertexFloat32x3, .offset = 72},
            mrhiVertexAttribute{.buffer = 1, .location = 8, .format = mrhi_vertexFloat32x4, .offset = 84}};
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
        // The casters into the sun's shadow map: the vertex's place alone,
        // pushed from the sun by its slope (the depth half of ADR-0051's
        // bias; the normal half is where the map is read).
        constexpr std::array<mrhiVertexAttribute, 4> kCasterAttributes = {
            kAttributes[0], kAttributes[2], kAttributes[3], kAttributes[4]};
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
        // The lit models, drawn where the prepass left their depth.
        constexpr std::string_view kLitLabel = "rawframe.scene.models";
        models.label = kLitLabel.data();
        models.labelLength = kLitLabel.size();
        models.fragmentEntry = "fs";
        models.fragmentEntryLength = 2;
        models.depthWrite = false;
        models.depthCompare = mrhi_compareGreaterEqual;
        models.colorTargetCount = 1;
        models.colorTargets[0].format = kSceneFormat;
        RAWFRAME_TRY(ask(models, lit));
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
        return ask(picture, tonemap);
    }

    /// Whether every pipeline is made; an error if one could not be.
    result::Result<bool> ready() {
        bool all = true;
        for (Asked* asked : {&casting, &depth, &lit, &tonemap}) {
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

    /// The meshes this frame draws, made on the device as needed; those
    /// still to upload within the frame's budget are marked. A draw whose
    /// mesh is not here is left out.
    std::map<std::uint64_t, Held*> meshesOf(const render_scene::SceneFrame& scene,
                                            const MeshSource& given,
                                            std::uint64_t budget,
                                            std::vector<Held*>& uploading) {
        std::map<std::uint64_t, Held*> usable;
        std::vector<const render_scene::SceneDraw*> all;
        for (const std::vector<render_scene::SceneDraw>* kList : {&scene.draws, &scene.shadows.casters}) {
            for (const render_scene::SceneDraw& draw : *kList) {
                all.push_back(&draw);
            }
        }
        for (const render_scene::SceneDraw* kDraw : all) {
            const render_scene::SceneDraw& draw = *kDraw;
            if (usable.contains(draw.mesh)) {
                continue;
            }
            auto found = held.find(draw.mesh);
            if (found == held.end()) {
                std::shared_ptr<const mesh::Mesh> source = given ? given(draw.mesh) : nullptr;
                if (source == nullptr || source->positions.empty() || source->indices.empty() ||
                    held.size() >= limits.maximumMeshes) {
                    continue;
                }
                mrhiBufferDef vertexDef = mrhiDefaultBufferDef();
                vertexDef.size = std::uint64_t{source->positions.size()} * kVertexBytes;
                vertexDef.usage = mrhi_bufferVertex | mrhi_bufferCopyDestination;
                mrhiBufferDef indexDef = mrhiDefaultBufferDef();
                indexDef.size = std::uint64_t{source->indices.size()} * 4;
                indexDef.usage = mrhi_bufferIndex | mrhi_bufferCopyDestination;
                Held made{.source = std::move(source)};
                if (mrhiCreateBuffer(native, &vertexDef, &made.vertices) != mrhi_success) {
                    continue;
                }
                if (mrhiCreateBuffer(native, &indexDef, &made.indices) != mrhi_success) {
                    static_cast<void>(mrhiDestroyBuffer(native, made.vertices));
                    continue;
                }
                found = held.emplace(draw.mesh, std::move(made)).first;
            }
            Held& mesh = found->second;
            if (!mesh.uploaded) {
                const std::uint64_t kBytes = bytesOf(*mesh.source);
                if (kBytes > budget) {
                    ++statistics.uploadsDeferred;
                    continue;
                }
                budget -= kBytes;
                uploading.push_back(&mesh);
            }
            usable.emplace(draw.mesh, &mesh);
        }
        return usable;
    }

    static FrameBlock blockOf(const render_scene::SceneFrame& frame) noexcept {
        FrameBlock block;
        // The projection times the view, column-major.
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) {
                float sum = 0;
                for (std::size_t k = 0; k < 4; ++k) {
                    sum += frame.projection[(k * 4) + row] * frame.view[(column * 4) + k];
                }
                block.viewProjection[(column * 4) + row] = sum;
            }
        }
        const render_scene::SceneLights& kLights = frame.lights;
        block.toSun = {kLights.toSun[0], kLights.toSun[1], kLights.toSun[2], 0};
        block.sun = {kLights.sun[0], kLights.sun[1], kLights.sun[2], 0};
        block.sky = {kLights.sky[0], kLights.sky[1], kLights.sky[2], 0};
        block.exposure = {exposureOf(frame.exposure), 0, 0, 0};
        block.forward = {frame.forward[0], frame.forward[1], frame.forward[2], 0};
        const render_scene::SceneShadows& kShadows = frame.shadows;
        for (std::size_t at = 0; at < kShadows.count; ++at) {
            block.cascadeFar[at] = kShadows.cascades[at].far;
            block.cascadeTexel[at] = kShadows.cascades[at].texel;
            block.cascades[at] = kShadows.cascades[at].viewProjection;
        }
        block.shadow = {static_cast<float>(kShadows.count),
                        kShadows.distance,
                        static_cast<float>(std::max<std::uint32_t>(kShadows.side, 1)),
                        0};
        return block;
    }

    /// The placements of the draws whose mesh is here, in order, and the
    /// runs of one mesh each: an instanced draw apiece.
    struct Placed {
        std::vector<float> instances;
        /// Mesh, first instance, instances: the draws', then the casters'.
        std::vector<std::tuple<const Held*, std::uint32_t, std::uint32_t>> runs;
        std::vector<std::tuple<const Held*, std::uint32_t, std::uint32_t>> casterRuns;
    };

    Placed place(const render_scene::SceneFrame& scene, const std::map<std::uint64_t, Held*>& usable) {
        Placed placed;
        std::uint32_t count = 0;
        append(scene.draws, usable, placed, placed.runs, count, true);
        append(scene.shadows.casters, usable, placed, placed.casterRuns, count, false);
        return placed;
    }

    void append(const std::vector<render_scene::SceneDraw>& draws,
                const std::map<std::uint64_t, Held*>& usable,
                Placed& placed,
                std::vector<std::tuple<const Held*, std::uint32_t, std::uint32_t>>& runs,
                std::uint32_t& count,
                bool counted) {
        for (const render_scene::SceneDraw& draw : draws) {
            const auto kMesh = usable.find(draw.mesh);
            if (kMesh == usable.end()) {
                statistics.modelsLeftOut += counted ? 1 : 0;
                continue;
            }
            // The model's rows, then the normals' columns, then the color.
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    placed.instances.push_back(draw.model[(column * 4) + row]);
                }
            }
            for (std::size_t column = 0; column < 3; ++column) {
                for (std::size_t row = 0; row < 3; ++row) {
                    placed.instances.push_back(draw.normal[(column * 4) + row]);
                }
            }
            placed.instances.insert(placed.instances.end(), draw.color.begin(), draw.color.end());
            if (runs.empty() || std::get<0>(runs.back()) != kMesh->second) {
                runs.emplace_back(kMesh->second, count, 0);
            }
            ++std::get<2>(runs.back());
            ++count;
        }
    }

    struct Declared;

    /// The shadow pass recorded: each cascade's square of the map drawn from
    /// its view, the casters in their runs.
    result::Status castShadows(const Declared& now) {
        if (mrhiBeginPass(native, now.shadowPass) != mrhi_success) {
            return failed("the shadow pass could not begin", mrhi_errorState);
        }
        if (now.casters && now.cascadeCount > 0) {
            if (mrhiSetGraphicsPipeline(native, now.shadowPass, casting.pipeline) != mrhi_success ||
                mrhiSetVertexBuffer(native, now.shadowPass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                return failed("the casters could not be set up", mrhi_errorState);
            }
            for (std::size_t at = 0; at < now.cascadeCount; ++at) {
                const std::array<mrhiBinding, 1> kCascade = {mrhiBinding{.slot = 0,
                                                                         .resource = now.cascades[at],
                                                                         .offset = 0,
                                                                         .size = sizeof(Matrix4),
                                                                         .viewKind = mrhi_texture2d,
                                                                         .viewFormat = mrhi_formatNone,
                                                                         .range = {},
                                                                         .sampler = {}}};
                const auto kSide = static_cast<float>(now.side);
                const mrhiViewport kSquare{.x = static_cast<float>(at % 2) * kSide,
                                           .y = static_cast<float>(at / 2) * kSide,
                                           .width = kSide,
                                           .height = kSide,
                                           .minDepth = 0,
                                           .maxDepth = 1};
                if (mrhiSetBindings(native, now.shadowPass, 0, kCascade.data(), kCascade.size()) != mrhi_success ||
                    mrhiSetViewport(native, now.shadowPass, &kSquare) != mrhi_success) {
                    return failed("a cascade could not be set up", mrhi_errorState);
                }
                for (const auto& [kMesh, kFirst, kCount] : now.placed.casterRuns) {
                    const auto& [kVertexResource, kIndexResource] = now.imported.at(kMesh);
                    if (mrhiSetVertexBuffer(native, now.shadowPass, 0, kVertexResource, 0, MRHI_WHOLE_SIZE) !=
                            mrhi_success ||
                        mrhiSetIndexBuffer(
                            native, now.shadowPass, kIndexResource, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) !=
                            mrhi_success ||
                        mrhiDrawIndexed(native,
                                        now.shadowPass,
                                        static_cast<std::uint32_t>(kMesh->source->indices.size()),
                                        kCount,
                                        0,
                                        0,
                                        kFirst) != mrhi_success) {
                        return failed("a caster could not be drawn", mrhi_errorState);
                    }
                }
            }
        }
        if (mrhiEndPass(native, now.shadowPass) != mrhi_success) {
            return failed("the shadow pass could not end", mrhi_errorState);
        }
        return {};
    }

    /// What the open frame declared, until it is recorded and ends.
    struct Declared {
        std::vector<Held*> uploads;
        Placed placed;
        FrameBlock block;
        std::map<const Held*, std::pair<mrhiResourceId, mrhiResourceId>> imported;
        mrhiResourceId instances{};
        mrhiResourceId blockResource{};
        mrhiResourceId scene{};
        mrhiPassId upload{};
        mrhiPassId depthPass{};
        mrhiPassId litPass{};
        mrhiPassId picturePass{};
        /// The sun's shadow map, each cascade's view, and the pass drawing
        /// the casters into it.
        mrhiResourceId shadowMap{};
        std::array<mrhiResourceId, 4> cascades{};
        std::size_t cascadeCount = 0;
        std::uint32_t side = 0;
        mrhiPassId shadowPass{};
        bool draws = false;
        bool casters = false;
    };

    result::Status declare(render::Frame& open) {
        declared.reset();
        if (frame == nullptr) {
            return {};
        }
        device->pump();
        RAWFRAME_TRY_ASSIGN(const bool kReady, ready());
        if (!kReady) {
            ++statistics.framesWaiting;
            return {};
        }
        Declared now;
        // The placements come first in the frame's uploads; the meshes share
        // what is left.
        const std::uint64_t kPlacementBytes = std::uint64_t{frame->draws.size()} * kInstanceBytes;
        const std::uint64_t kBudget =
            kPlacementBytes < limits.uploadBytesPerFrame ? limits.uploadBytesPerFrame - kPlacementBytes : 0;
        const std::map<std::uint64_t, Held*> kUsable = meshesOf(*frame, meshes, kBudget, now.uploads);
        now.placed = place(*frame, kUsable);
        now.block = blockOf(*frame);
        // Everything this frame uses: the meshes it draws, imported; its
        // placements and view; and its targets.
        for (const auto& [id, mesh] : kUsable) {
            mrhiResourceId vertices{};
            mrhiResourceId indices{};
            if (const mrhiResult kImported = mrhiImportBuffer(native, mesh->vertices, &vertices);
                kImported != mrhi_success) {
                return failed("a mesh could not join the frame", kImported);
            }
            if (const mrhiResult kImported = mrhiImportBuffer(native, mesh->indices, &indices);
                kImported != mrhi_success) {
                return failed("a mesh could not join the frame", kImported);
            }
            now.imported.emplace(mesh, std::pair{vertices, indices});
        }
        now.draws = !now.placed.runs.empty();
        now.casters = !now.placed.casterRuns.empty();
        if (now.draws || now.casters) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = now.placed.instances.size() * sizeof(float);
            if (mrhiDeclareBuffer(native, &def, &now.instances) != mrhi_success) {
                return failed("the frame's placements could not be declared", mrhi_errorCapacity);
            }
        }
        mrhiBufferDef blockDef = mrhiDefaultBufferDef();
        blockDef.size = sizeof(FrameBlock);
        if (mrhiDeclareBuffer(native, &blockDef, &now.blockResource) != mrhi_success) {
            return failed("the frame's view could not be declared", mrhi_errorCapacity);
        }
        // The shadow map: the cascades' squares two by two, or a texel
        // nothing reads the depth of when there are none.
        now.cascadeCount = frame->shadows.count;
        now.side = now.cascadeCount > 0 ? frame->shadows.side : 1;
        mrhiTextureDef shadowDef = mrhiDefaultTextureDef();
        shadowDef.format = kShadowFormat;
        shadowDef.width = now.cascadeCount > 0 ? 2 * now.side : 1;
        shadowDef.height = shadowDef.width;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &shadowDef, &now.shadowMap);
            kDeclared != mrhi_success) {
            return failed("the shadow map could not be declared", kDeclared);
        }
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = sizeof(Matrix4);
            if (mrhiDeclareBuffer(native, &def, &now.cascades[at]) != mrhi_success) {
                return failed("a cascade's view could not be declared", mrhi_errorCapacity);
            }
        }
        mrhiResourceId depthTarget{};
        for (const auto& [kFormat, kMade] :
             {std::pair{kSceneFormat, &now.scene}, std::pair{kDepthFormat, &depthTarget}}) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kFormat;
            def.width = open.width;
            def.height = open.height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, kMade); kDeclared != mrhi_success) {
                return failed("a target could not be declared", kDeclared);
            }
        }

        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes = {wholeOf(now.blockResource, mrhi_accessCopyDestination)};
        if (now.draws || now.casters) {
            writes.push_back(wholeOf(now.instances, mrhi_accessCopyDestination));
        }
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            writes.push_back(wholeOf(now.cascades[at], mrhi_accessCopyDestination));
        }
        for (const Held* mesh : now.uploads) {
            writes.push_back(wholeOf(now.imported.at(mesh).first, mrhi_accessCopyDestination));
            writes.push_back(wholeOf(now.imported.at(mesh).second, mrhi_accessCopyDestination));
        }
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = writes.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(writes.size());
        if (const mrhiResult kAdded = mrhiAddPass(native, &uploadDef, &now.upload); kAdded != mrhi_success) {
            return failed("the upload pass could not be added", kAdded);
        }
        // The shadow map first: its casters from each cascade's view.
        std::vector<mrhiAccess> meshReads;
        if (now.draws || now.casters) {
            meshReads.push_back(wholeOf(now.instances, mrhi_accessVertex));
        }
        for (const auto& [mesh, resources] : now.imported) {
            meshReads.push_back(wholeOf(resources.first, mrhi_accessVertex));
            meshReads.push_back(wholeOf(resources.second, mrhi_accessIndex));
        }
        std::vector<mrhiAccess> shadowReads = meshReads;
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            shadowReads.push_back(wholeOf(now.cascades[at], mrhi_accessUniform));
        }
        mrhiPassDef shadowDef2 = mrhiDefaultPassDef();
        shadowDef2.accesses = shadowReads.data();
        shadowDef2.accessCount = static_cast<std::uint32_t>(shadowReads.size());
        shadowDef2.depthTarget = mrhiDepthTarget{.resource = now.shadowMap,
                                                 .mip = 0,
                                                 .layer = 0,
                                                 .depthLoad = mrhi_loadClear,
                                                 .depthStore = mrhi_storeKeep,
                                                 .clearDepth = 0,
                                                 .stencilLoad = mrhi_loadDiscard,
                                                 .stencilStore = mrhi_storeDiscard,
                                                 .clearStencil = 0,
                                                 .readOnly = false};
        if (const mrhiResult kAdded = mrhiAddPass(native, &shadowDef2, &now.shadowPass); kAdded != mrhi_success) {
            return failed("the shadow pass could not be added", kAdded);
        }
        // The models' passes read the shadow map too: the scene's table
        // holds it for both.
        std::vector<mrhiAccess> reads = meshReads;
        reads.push_back(wholeOf(now.blockResource, mrhi_accessUniform));
        reads.push_back(mrhiAccess{.resource = now.shadowMap,
                                   .kind = mrhi_accessSampled,
                                   .range = {.baseMip = 0,
                                             .mipCount = MRHI_REMAINING,
                                             .baseLayer = 0,
                                             .layerCount = 1,
                                             .aspect = mrhi_aspectDepthOnly}});
        mrhiPassDef depthDef = mrhiDefaultPassDef();
        depthDef.accesses = reads.data();
        depthDef.accessCount = static_cast<std::uint32_t>(reads.size());
        depthDef.depthTarget = mrhiDepthTarget{.resource = depthTarget,
                                               .mip = 0,
                                               .layer = 0,
                                               .depthLoad = mrhi_loadClear,
                                               .depthStore = mrhi_storeKeep,
                                               .clearDepth = 0,
                                               .stencilLoad = mrhi_loadDiscard,
                                               .stencilStore = mrhi_storeDiscard,
                                               .clearStencil = 0,
                                               .readOnly = false};
        if (const mrhiResult kAdded = mrhiAddPass(native, &depthDef, &now.depthPass); kAdded != mrhi_success) {
            return failed("the depth pass could not be added", kAdded);
        }
        // Behind every model, the sky's light.
        const float kExposure = now.block.exposure[0];
        mrhiPassDef litDef = depthDef;
        litDef.colorTargets[0].resource = now.scene;
        litDef.colorTargets[0].load = mrhi_loadClear;
        litDef.colorTargets[0].store = mrhi_storeKeep;
        litDef.colorTargets[0].clear = mrhiClearColor{.red = now.block.sky[0] * kExposure,
                                                      .green = now.block.sky[1] * kExposure,
                                                      .blue = now.block.sky[2] * kExposure,
                                                      .alpha = 1};
        litDef.colorTargetCount = 1;
        litDef.depthTarget.depthLoad = mrhi_loadKeep;
        litDef.depthTarget.depthStore = mrhi_storeKeep;
        litDef.depthTarget.readOnly = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &litDef, &now.litPass); kAdded != mrhi_success) {
            return failed("the models' pass could not be added", kAdded);
        }
        // The picture: every pixel of it written, over whatever was there.
        const mrhiAccess kScene = wholeOf(now.scene, mrhi_accessSampled);
        mrhiPassDef pictureDef = mrhiDefaultPassDef();
        pictureDef.colorTargets[0].resource = resourceOf(open.picture);
        pictureDef.colorTargets[0].load = open.clearsPicture() ? mrhi_loadClear : mrhi_loadKeep;
        pictureDef.colorTargets[0].store = mrhi_storeKeep;
        pictureDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        pictureDef.colorTargetCount = 1;
        pictureDef.accesses = &kScene;
        pictureDef.accessCount = 1;
        pictureDef.neverCull = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &pictureDef, &now.picturePass); kAdded != mrhi_success) {
            return failed("the picture's pass could not be added", kAdded);
        }
        declared = std::move(now);
        return {};
    }

    result::Status record() {
        if (!declared.has_value()) {
            return {};
        }
        const Declared& now = *declared;
        if (mrhiBeginPass(native, now.upload) != mrhi_success ||
            mrhiWriteBuffer(native, now.upload, now.blockResource, 0, &now.block, sizeof(FrameBlock)) != mrhi_success ||
            ((now.draws || now.casters) &&
             mrhiWriteBuffer(native,
                             now.upload,
                             now.instances,
                             0,
                             now.placed.instances.data(),
                             now.placed.instances.size() * sizeof(float)) != mrhi_success)) {
            return failed("the frame's placements could not be written", mrhi_errorCapacity);
        }
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            if (mrhiWriteBuffer(native, now.upload, now.cascades[at], 0, &now.block.cascades[at], sizeof(Matrix4)) !=
                mrhi_success) {
                return failed("a cascade's view could not be written", mrhi_errorCapacity);
            }
        }
        for (const Held* mesh : now.uploads) {
            const std::vector<float> kVertices = verticesOf(*mesh->source);
            const auto& [kVertexResource, kIndexResource] = now.imported.at(mesh);
            if (mrhiWriteBuffer(
                    native, now.upload, kVertexResource, 0, kVertices.data(), kVertices.size() * sizeof(float)) !=
                    mrhi_success ||
                mrhiWriteBuffer(native,
                                now.upload,
                                kIndexResource,
                                0,
                                mesh->source->indices.data(),
                                mesh->source->indices.size() * 4) != mrhi_success) {
                return failed("a mesh could not be written", mrhi_errorCapacity);
            }
        }
        if (mrhiEndPass(native, now.upload) != mrhi_success) {
            return failed("the upload pass could not end", mrhi_errorState);
        }
        RAWFRAME_TRY(castShadows(now));
        const std::array<mrhiBinding, 3> kFrameBinding = {mrhiBinding{.slot = 0,
                                                                      .resource = now.blockResource,
                                                                      .offset = 0,
                                                                      .size = sizeof(FrameBlock),
                                                                      .viewKind = mrhi_texture2d,
                                                                      .viewFormat = mrhi_formatNone,
                                                                      .range = {},
                                                                      .sampler = {}},
                                                          mrhiBinding{.slot = 1,
                                                                      .resource = now.shadowMap,
                                                                      .offset = 0,
                                                                      .size = 0,
                                                                      .viewKind = mrhi_texture2d,
                                                                      .viewFormat = mrhi_formatNone,
                                                                      .range = {.baseMip = 0,
                                                                                .mipCount = MRHI_REMAINING,
                                                                                .baseLayer = 0,
                                                                                .layerCount = 1,
                                                                                .aspect = mrhi_aspectDepthOnly},
                                                                      .sampler = {}},
                                                          mrhiBinding{.slot = 2,
                                                                      .resource = {},
                                                                      .offset = 0,
                                                                      .size = 0,
                                                                      .viewKind = mrhi_texture2d,
                                                                      .viewFormat = mrhi_formatNone,
                                                                      .range = {},
                                                                      .sampler = shadowSampler}};
        for (const auto& [kPass, kPipeline] :
             {std::pair{now.depthPass, depth.pipeline}, std::pair{now.litPass, lit.pipeline}}) {
            if (mrhiBeginPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not begin", mrhi_errorState);
            }
            if (now.draws) {
                if (mrhiSetGraphicsPipeline(native, kPass, kPipeline) != mrhi_success ||
                    mrhiSetBindings(native, kPass, 0, kFrameBinding.data(), kFrameBinding.size()) != mrhi_success ||
                    mrhiSetVertexBuffer(native, kPass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                    return failed("the models could not be set up", mrhi_errorState);
                }
                for (const auto& [kMesh, kFirst, kCount] : now.placed.runs) {
                    const auto& [kVertexResource, kIndexResource] = now.imported.at(kMesh);
                    if (mrhiSetVertexBuffer(native, kPass, 0, kVertexResource, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                        mrhiSetIndexBuffer(native, kPass, kIndexResource, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) !=
                            mrhi_success ||
                        mrhiDrawIndexed(native,
                                        kPass,
                                        static_cast<std::uint32_t>(kMesh->source->indices.size()),
                                        kCount,
                                        0,
                                        0,
                                        kFirst) != mrhi_success) {
                        return failed("a model could not be drawn", mrhi_errorState);
                    }
                }
            }
            if (mrhiEndPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not end", mrhi_errorState);
            }
        }
        const std::array<mrhiBinding, 1> kSceneBinding = {mrhiBinding{
            .slot = 0,
            .resource = now.scene,
            .offset = 0,
            .size = 0,
            .viewKind = mrhi_texture2d,
            .viewFormat = mrhi_formatNone,
            .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
            .sampler = {}}};
        if (mrhiBeginPass(native, now.picturePass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native, now.picturePass, tonemap.pipeline) != mrhi_success ||
            mrhiSetBindings(native, now.picturePass, 0, kSceneBinding.data(), kSceneBinding.size()) != mrhi_success ||
            mrhiDraw(native, now.picturePass, 3, 1, 0, 0) != mrhi_success ||
            mrhiEndPass(native, now.picturePass) != mrhi_success) {
            return failed("the picture could not be drawn", mrhi_errorState);
        }
        return {};
    }

    void ended(bool submitted) noexcept {
        if (!declared.has_value()) {
            return;
        }
        if (submitted) {
            for (Held* mesh : declared->uploads) {
                mesh->uploaded = true;
                ++statistics.meshesUploaded;
                statistics.uploadBytes += bytesOf(*mesh->source);
            }
            ++statistics.frames;
            statistics.models += declared->placed.instances.size() * sizeof(float) / kInstanceBytes;
            statistics.drawCalls += declared->placed.runs.size();
        }
        declared.reset();
    }

    std::optional<Declared> declared;
};

SceneRenderer::SceneRenderer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

SceneRenderer::~SceneRenderer() = default;

result::Result<std::unique_ptr<SceneRenderer>> SceneRenderer::create(render::Device& device, RendererLimits limits) {
    if (device.native() == nullptr) {
        return refuse(
            result::ErrorClass::FailedPrecondition, SceneGpuError::State, "a scene renderer needs a ready device");
    }
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    state->limits = limits;
    RAWFRAME_TRY(state->makePipelines());
    return std::unique_ptr<SceneRenderer>{new SceneRenderer{std::move(state)}};
}

void SceneRenderer::prepare(const render_scene::SceneFrame* frame, MeshSource meshes) {
    state_->frame = frame;
    state_->meshes = std::move(meshes);
}

result::Status SceneRenderer::declare(render::Frame& frame) {
    return state_->declare(frame);
}

result::Status SceneRenderer::record(render::Frame& /*frame*/) {
    return state_->record();
}

void SceneRenderer::ended(bool submitted) noexcept {
    state_->ended(submitted);
}

const RendererStatistics& SceneRenderer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render_scene_gpu
