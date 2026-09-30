#include "rawframe/render_scene_gpu/renderer.h"

#include "generated/scene_container.h"
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

/// The frame's view and light as the scene's shaders read them (std140).
struct FrameBlock {
    std::array<float, 16> viewProjection{};
    std::array<float, 4> toSun{};
    std::array<float, 4> sun{};
    std::array<float, 4> sky{};
    std::array<float, 4> exposure{};
};
static_assert(sizeof(FrameBlock) == 128, "the scene's shaders read the frame as 128 bytes");

constexpr mrhiFormat kSceneFormat = mrhi_formatRgba16Float;
constexpr mrhiFormat kDepthFormat = mrhi_formatDepth32Float;
constexpr mrhiFormat kPictureFormat = mrhi_formatRgba8UnormSrgb;

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
    /// The depth prepass, the lit models, and the picture.
    Asked depth;
    Asked lit;
    Asked tonemap;
    std::map<std::uint64_t, Held> held;
    /// The last frame submitted, and its readback, if it read one.
    std::optional<std::uint64_t> frame_;
    std::optional<mrhiRequestId> readback;
    std::size_t readbackBytes = 0;
    bool frameDone = false;
    std::optional<std::vector<std::byte>> pixels;

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        for (auto& [id, made] : held) {
            static_cast<void>(mrhiDestroyBuffer(native, made.vertices));
            static_cast<void>(mrhiDestroyBuffer(native, made.indices));
        }
        for (Asked* asked : {&depth, &lit, &tonemap}) {
            static_cast<void>(mrhiDestroyGraphicsPipeline(native, asked->pipeline));
        }
        static_cast<void>(mrhiDestroyShader(native, sceneShader));
        static_cast<void>(mrhiDestroyShader(native, tonemapShader));
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
        for (Asked* asked : {&depth, &lit, &tonemap}) {
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
    std::map<std::uint64_t, Held*> prepare(const render_scene::SceneFrame& frame,
                                           const MeshSource& meshes,
                                           std::uint64_t budget,
                                           std::vector<Held*>& uploads) {
        std::map<std::uint64_t, Held*> usable;
        for (const render_scene::SceneDraw& draw : frame.draws) {
            if (usable.contains(draw.mesh)) {
                continue;
            }
            auto found = held.find(draw.mesh);
            if (found == held.end()) {
                std::shared_ptr<const mesh::Mesh> source = meshes ? meshes(draw.mesh) : nullptr;
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
                uploads.push_back(&mesh);
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
        return block;
    }

    /// The placements of the draws whose mesh is here, in order, and the
    /// runs of one mesh each: an instanced draw apiece.
    struct Placed {
        std::vector<float> instances;
        /// Mesh, first instance, instances.
        std::vector<std::tuple<const Held*, std::uint32_t, std::uint32_t>> runs;
    };

    Placed place(const render_scene::SceneFrame& frame, const std::map<std::uint64_t, Held*>& usable) {
        Placed placed;
        std::uint32_t count = 0;
        for (const render_scene::SceneDraw& draw : frame.draws) {
            const auto kMesh = usable.find(draw.mesh);
            if (kMesh == usable.end()) {
                ++statistics.modelsLeftOut;
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
            if (placed.runs.empty() || std::get<0>(placed.runs.back()) != kMesh->second) {
                placed.runs.emplace_back(kMesh->second, count, 0);
            }
            ++std::get<2>(placed.runs.back());
            ++count;
        }
        return placed;
    }

    result::Result<bool>
    render(const render_scene::SceneFrame& frame, const MeshSource& meshes, const SceneTarget& target) {
        if (target.width == 0 || target.height == 0 || target.width > limits.maximumSide ||
            target.height > limits.maximumSide) {
            return refuse(result::ErrorClass::OutOfRange,
                          SceneGpuError::OverLimit,
                          "a target's sides are from 1 to the renderer's maximum");
        }
        device->pump();
        if (device->lost()) {
            return refuse(result::ErrorClass::Unavailable, SceneGpuError::State, "the device was lost");
        }
        RAWFRAME_TRY_ASSIGN(const bool kReady, ready());
        if (!kReady) {
            ++statistics.framesWaiting;
            return false;
        }
        takeDone();
        // The placements come first in the frame's uploads; the meshes share
        // what is left.
        const std::uint64_t kPlacementBytes = std::uint64_t{frame.draws.size()} * kInstanceBytes;
        const std::uint64_t kBudget =
            kPlacementBytes < limits.uploadBytesPerFrame ? limits.uploadBytesPerFrame - kPlacementBytes : 0;
        std::vector<Held*> uploads;
        const std::map<std::uint64_t, Held*> kUsable = prepare(frame, meshes, kBudget, uploads);
        const Placed kPlaced = place(frame, kUsable);
        const FrameBlock kBlock = blockOf(frame);

        const mrhiFrameDef kFrame = mrhiDefaultFrameDef();
        if (const mrhiResult kBegun = mrhiBeginFrame(native, &kFrame); kBegun != mrhi_success) {
            return failed("a frame could not begin", kBegun);
        }
        const auto kDrop = [this](std::string_view why, mrhiResult outcome) {
            static_cast<void>(mrhiDropFrame(native));
            return failed(why, outcome);
        };
        // Everything this frame uses: the meshes it draws, imported; its
        // placements and view; and its targets.
        std::map<const Held*, std::pair<mrhiResourceId, mrhiResourceId>> imported;
        for (const auto& [id, mesh] : kUsable) {
            mrhiResourceId vertices{};
            mrhiResourceId indices{};
            if (const mrhiResult kImported = mrhiImportBuffer(native, mesh->vertices, &vertices);
                kImported != mrhi_success) {
                return kDrop("a mesh could not join the frame", kImported);
            }
            if (const mrhiResult kImported = mrhiImportBuffer(native, mesh->indices, &indices);
                kImported != mrhi_success) {
                return kDrop("a mesh could not join the frame", kImported);
            }
            imported.emplace(mesh, std::pair{vertices, indices});
        }
        const bool kDraws = !kPlaced.runs.empty();
        mrhiResourceId instances{};
        if (kDraws) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = kPlaced.instances.size() * sizeof(float);
            if (mrhiDeclareBuffer(native, &def, &instances) != mrhi_success) {
                return kDrop("the frame's placements could not be declared", mrhi_errorCapacity);
            }
        }
        mrhiBufferDef blockDef = mrhiDefaultBufferDef();
        blockDef.size = sizeof(FrameBlock);
        mrhiResourceId block{};
        if (mrhiDeclareBuffer(native, &blockDef, &block) != mrhi_success) {
            return kDrop("the frame's view could not be declared", mrhi_errorCapacity);
        }
        const auto kDeclare = [this](mrhiFormat format, const SceneTarget& size, mrhiResourceId& made) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = format;
            def.width = size.width;
            def.height = size.height;
            return mrhiDeclareTexture(native, &def, &made);
        };
        mrhiResourceId scene{};
        mrhiResourceId depthTarget{};
        mrhiResourceId picture{};
        for (const auto& [kFormat, kMade] : {std::pair{kSceneFormat, &scene},
                                             std::pair{kDepthFormat, &depthTarget},
                                             std::pair{kPictureFormat, &picture}}) {
            if (const mrhiResult kDeclared = kDeclare(kFormat, target, *kMade); kDeclared != mrhi_success) {
                return kDrop("a target could not be declared", kDeclared);
            }
        }

        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes = {wholeOf(block, mrhi_accessCopyDestination)};
        if (kDraws) {
            writes.push_back(wholeOf(instances, mrhi_accessCopyDestination));
        }
        for (const Held* mesh : uploads) {
            writes.push_back(wholeOf(imported.at(mesh).first, mrhi_accessCopyDestination));
            writes.push_back(wholeOf(imported.at(mesh).second, mrhi_accessCopyDestination));
        }
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = writes.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(writes.size());
        mrhiPassId upload{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &uploadDef, &upload); kAdded != mrhi_success) {
            return kDrop("the upload pass could not be added", kAdded);
        }
        std::vector<mrhiAccess> reads = {wholeOf(block, mrhi_accessUniform)};
        if (kDraws) {
            reads.push_back(wholeOf(instances, mrhi_accessVertex));
        }
        for (const auto& [mesh, resources] : imported) {
            reads.push_back(wholeOf(resources.first, mrhi_accessVertex));
            reads.push_back(wholeOf(resources.second, mrhi_accessIndex));
        }
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
        mrhiPassId depthPass{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &depthDef, &depthPass); kAdded != mrhi_success) {
            return kDrop("the depth pass could not be added", kAdded);
        }
        // Behind every model, the sky's light.
        const float kExposure = kBlock.exposure[0];
        mrhiPassDef litDef = depthDef;
        litDef.colorTargets[0].resource = scene;
        litDef.colorTargets[0].load = mrhi_loadClear;
        litDef.colorTargets[0].store = mrhi_storeKeep;
        litDef.colorTargets[0].clear = mrhiClearColor{.red = kBlock.sky[0] * kExposure,
                                                      .green = kBlock.sky[1] * kExposure,
                                                      .blue = kBlock.sky[2] * kExposure,
                                                      .alpha = 1};
        litDef.colorTargetCount = 1;
        litDef.depthTarget.depthLoad = mrhi_loadKeep;
        litDef.depthTarget.depthStore = mrhi_storeKeep;
        litDef.depthTarget.readOnly = true;
        mrhiPassId litPass{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &litDef, &litPass); kAdded != mrhi_success) {
            return kDrop("the models' pass could not be added", kAdded);
        }
        const mrhiAccess kScene = wholeOf(scene, mrhi_accessSampled);
        mrhiPassDef pictureDef = mrhiDefaultPassDef();
        pictureDef.colorTargets[0].resource = picture;
        pictureDef.colorTargets[0].load = mrhi_loadClear;
        pictureDef.colorTargets[0].store = mrhi_storeKeep;
        pictureDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        pictureDef.colorTargetCount = 1;
        pictureDef.accesses = &kScene;
        pictureDef.accessCount = 1;
        pictureDef.neverCull = true;
        mrhiPassId picturePass{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &pictureDef, &picturePass); kAdded != mrhi_success) {
            return kDrop("the picture's pass could not be added", kAdded);
        }
        const mrhiAccess kRead = wholeOf(picture, mrhi_accessCopySource);
        std::optional<mrhiPassId> reading;
        if (target.readBack) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.passClass = mrhi_passTransfer;
            def.accesses = &kRead;
            def.accessCount = 1;
            def.neverCull = true;
            mrhiPassId made{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &made); kAdded != mrhi_success) {
                return kDrop("the reading pass could not be added", kAdded);
            }
            reading = made;
        }
        if (const mrhiResult kCompiled = mrhiCompileFrame(native); kCompiled != mrhi_success) {
            return kDrop("the frame could not be compiled", kCompiled);
        }

        // Recorded.
        if (mrhiBeginPass(native, upload) != mrhi_success ||
            mrhiWriteBuffer(native, upload, block, 0, &kBlock, sizeof(FrameBlock)) != mrhi_success ||
            (kDraws &&
             mrhiWriteBuffer(
                 native, upload, instances, 0, kPlaced.instances.data(), kPlaced.instances.size() * sizeof(float)) !=
                 mrhi_success)) {
            return kDrop("the frame's placements could not be written", mrhi_errorCapacity);
        }
        for (const Held* mesh : uploads) {
            const std::vector<float> kVertices = verticesOf(*mesh->source);
            const auto& [kVertexResource, kIndexResource] = imported.at(mesh);
            if (mrhiWriteBuffer(
                    native, upload, kVertexResource, 0, kVertices.data(), kVertices.size() * sizeof(float)) !=
                    mrhi_success ||
                mrhiWriteBuffer(native,
                                upload,
                                kIndexResource,
                                0,
                                mesh->source->indices.data(),
                                mesh->source->indices.size() * 4) != mrhi_success) {
                return kDrop("a mesh could not be written", mrhi_errorCapacity);
            }
        }
        if (mrhiEndPass(native, upload) != mrhi_success) {
            return kDrop("the upload pass could not end", mrhi_errorState);
        }
        const std::array<mrhiBinding, 1> kFrameBinding = {mrhiBinding{.slot = 0,
                                                                      .resource = block,
                                                                      .offset = 0,
                                                                      .size = sizeof(FrameBlock),
                                                                      .viewKind = mrhi_texture2d,
                                                                      .viewFormat = mrhi_formatNone,
                                                                      .range = {},
                                                                      .sampler = {}}};
        for (const auto& [kPass, kPipeline] :
             {std::pair{depthPass, depth.pipeline}, std::pair{litPass, lit.pipeline}}) {
            if (mrhiBeginPass(native, kPass) != mrhi_success) {
                return kDrop("a scene pass could not begin", mrhi_errorState);
            }
            if (kDraws) {
                if (mrhiSetGraphicsPipeline(native, kPass, kPipeline) != mrhi_success ||
                    mrhiSetBindings(native, kPass, 0, kFrameBinding.data(), kFrameBinding.size()) != mrhi_success ||
                    mrhiSetVertexBuffer(native, kPass, 1, instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                    return kDrop("the models could not be set up", mrhi_errorState);
                }
                for (const auto& [kMesh, kFirst, kCount] : kPlaced.runs) {
                    const auto& [kVertexResource, kIndexResource] = imported.at(kMesh);
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
                        return kDrop("a model could not be drawn", mrhi_errorState);
                    }
                }
            }
            if (mrhiEndPass(native, kPass) != mrhi_success) {
                return kDrop("a scene pass could not end", mrhi_errorState);
            }
        }
        const std::array<mrhiBinding, 1> kSceneBinding = {mrhiBinding{
            .slot = 0,
            .resource = scene,
            .offset = 0,
            .size = 0,
            .viewKind = mrhi_texture2d,
            .viewFormat = mrhi_formatNone,
            .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
            .sampler = {}}};
        if (mrhiBeginPass(native, picturePass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native, picturePass, tonemap.pipeline) != mrhi_success ||
            mrhiSetBindings(native, picturePass, 0, kSceneBinding.data(), kSceneBinding.size()) != mrhi_success ||
            mrhiDraw(native, picturePass, 3, 1, 0, 0) != mrhi_success ||
            mrhiEndPass(native, picturePass) != mrhi_success) {
            return kDrop("the picture could not be drawn", mrhi_errorState);
        }
        readback.reset();
        pixels.reset();
        if (reading.has_value()) {
            const mrhiTextureCopy kSource{.resource = picture};
            const mrhiExtent3d kExtent{.width = target.width, .height = target.height, .depthOrLayers = 1};
            mrhiRequestId request{};
            if (mrhiBeginPass(native, *reading) != mrhi_success ||
                mrhiReadTexture(native, *reading, &kSource, &kExtent, &request) != mrhi_success ||
                mrhiEndPass(native, *reading) != mrhi_success) {
                return kDrop("the picture could not be read back", mrhi_errorState);
            }
            readback = request;
            readbackBytes = std::size_t{target.width} * target.height * 4;
        }
        mrhiRequestId token{};
        if (const mrhiResult kSubmitted = mrhiSubmitFrame(native, &token); kSubmitted != mrhi_success) {
            return failed("the frame could not be submitted", kSubmitted);
        }
        for (Held* mesh : uploads) {
            mesh->uploaded = true;
            ++statistics.meshesUploaded;
            statistics.uploadBytes += bytesOf(*mesh->source);
        }
        frame_ = render::requestKey(token.index1, token.generation);
        frameDone = false;
        ++statistics.frames;
        statistics.models += kPlaced.instances.size() * sizeof(float) / kInstanceBytes;
        statistics.drawCalls += kPlaced.runs.size();
        return true;
    }

    /// Takes the last frame's answer, and its pixels once they are ready.
    void takeDone() {
        if (!frame_.has_value() || frameDone) {
            return;
        }
        device->pump();
        if (const auto kAnswer = device->answer(*frame_)) {
            frameDone = true;
        }
        if (frameDone && readback.has_value()) {
            if (const auto kAnswer = device->answer(render::requestKey(readback->index1, readback->generation));
                kAnswer.has_value() && kAnswer->has_value()) {
                std::vector<std::byte> bytes(readbackBytes);
                std::size_t taken = 0;
                if (mrhiTakeReadback(native, *readback, bytes.data(), bytes.size(), &taken) == mrhi_success &&
                    taken == bytes.size()) {
                    pixels = std::move(bytes);
                }
                readback.reset();
            }
        }
    }
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

result::Result<bool>
SceneRenderer::render(const render_scene::SceneFrame& frame, const MeshSource& meshes, const SceneTarget& target) {
    return state_->render(frame, meshes, target);
}

result::Result<bool> SceneRenderer::done() {
    state_->takeDone();
    return state_->frameDone;
}

result::Status SceneRenderer::finish(std::uint64_t nanoseconds) {
    if (!state_->frame_.has_value() || state_->frameDone) {
        return {};
    }
    const std::uint64_t kKey = *state_->frame_;
    const mrhiRequestId kToken{.index1 = static_cast<std::uint32_t>(kKey >> 32U),
                               .generation = static_cast<std::uint32_t>(kKey)};
    if (const mrhiResult kWaited = mrhiWaitFrame(state_->native, kToken, nanoseconds); kWaited != mrhi_success) {
        return failed("the frame did not finish", kWaited);
    }
    state_->takeDone();
    return {};
}

std::optional<std::vector<std::byte>> SceneRenderer::pixels() {
    state_->takeDone();
    return std::exchange(state_->pixels, std::nullopt);
}

const RendererStatistics& SceneRenderer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render_scene_gpu
