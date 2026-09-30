#include "rawframe/render_scene_gpu/renderer.h"

#include "blocks.h"
#include "metering.h"
#include "pipelines.h"
#include "rawframe/render_scene_gpu/errors.h"
#include "temporal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, SceneGpuError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kSceneGpuDomain, code(error), why).error()};
}

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

/// A mesh held on the device: the mesh it was made from, and whether its
/// vertices and indices are there yet.
struct Held {
    std::shared_ptr<const mesh::Mesh> source;
    mrhiBufferId vertices{};
    mrhiBufferId indices{};
    bool uploaded = false;
};

} // namespace

struct SceneRenderer::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    RendererLimits limits;
    RendererStatistics statistics;
    /// Its shaders, samplers, and pipelines.
    Pipelines pipelines;
    /// The temporal pass and its pictures (D291).
    std::optional<TemporalPass> temporal;
    /// The exposure the device holds, and its metering (D293).
    std::optional<Metering> metering;
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
        for (const std::vector<render_scene::SceneDraw>* kList :
             {&scene.draws, &scene.shadows.casters, &scene.lightShadows.casters}) {
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

    /// The placements of the draws whose mesh is here, in order, and the
    /// runs of one mesh each: an instanced draw apiece.
    /// Mesh, first instance, instances.
    using Runs = std::vector<std::tuple<const Held*, std::uint32_t, std::uint32_t>>;

    struct Placed {
        std::vector<float> instances;
        /// The draws', then the sun's casters', then each square of the
        /// punctual shadows' atlas's (D292).
        Runs runs;
        Runs casterRuns;
        std::vector<Runs> slotRuns;
    };

    Placed place(const render_scene::SceneFrame& scene, const std::map<std::uint64_t, Held*>& usable) {
        Placed placed;
        std::uint32_t count = 0;
        append(scene.draws, usable, placed, placed.runs, count, true);
        append(scene.shadows.casters, usable, placed, placed.casterRuns, count, false);
        const std::span<const render_scene::SceneDraw> kCasters = scene.lightShadows.casters;
        for (const render_scene::ShadowSlot& slot : scene.lightShadows.slots) {
            append(kCasters.subspan(slot.firstCaster, slot.casterCount),
                   usable,
                   placed,
                   placed.slotRuns.emplace_back(),
                   count,
                   false);
        }
        return placed;
    }

    void append(std::span<const render_scene::SceneDraw> draws,
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
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    placed.instances.push_back(draw.previous[(column * 4) + row]);
                }
            }
            if (runs.empty() || std::get<0>(runs.back()) != kMesh->second) {
                runs.emplace_back(kMesh->second, count, 0);
            }
            ++std::get<2>(runs.back());
            ++count;
        }
    }

    struct Declared;

    /// A square of a shadow map: its view, where it lies, and its casters.
    struct Square {
        mrhiResourceId view{};
        mrhiViewport viewport{};
        const Runs* casters = nullptr;
    };

    /// A shadow pass recorded: each square drawn from its view, its casters
    /// in their runs.
    result::Status cast(const Declared& now, mrhiPassId pass, std::span<const Square> squares) {
        if (mrhiBeginPass(native, pass) != mrhi_success) {
            return failed("a shadow pass could not begin", mrhi_errorState);
        }
        bool set = false;
        for (const Square& kSquare : squares) {
            if (kSquare.casters->empty()) {
                continue;
            }
            if (!set && (mrhiSetGraphicsPipeline(native, pass, pipelines.casting.pipeline) != mrhi_success ||
                         mrhiSetVertexBuffer(native, pass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success)) {
                return failed("the casters could not be set up", mrhi_errorState);
            }
            set = true;
            const std::array<mrhiBinding, 1> kView = {mrhiBinding{.slot = 0,
                                                                  .resource = kSquare.view,
                                                                  .offset = 0,
                                                                  .size = sizeof(Matrix4),
                                                                  .viewKind = mrhi_texture2d,
                                                                  .viewFormat = mrhi_formatNone,
                                                                  .range = {},
                                                                  .sampler = {}}};
            if (mrhiSetBindings(native, pass, 0, kView.data(), kView.size()) != mrhi_success ||
                mrhiSetViewport(native, pass, &kSquare.viewport) != mrhi_success) {
                return failed("a shadow square could not be set up", mrhi_errorState);
            }
            for (const auto& [kMesh, kFirst, kCount] : *kSquare.casters) {
                const auto& [kVertexResource, kIndexResource] = now.imported.at(kMesh);
                if (mrhiSetVertexBuffer(native, pass, 0, kVertexResource, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                    mrhiSetIndexBuffer(native, pass, kIndexResource, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) !=
                        mrhi_success ||
                    mrhiDrawIndexed(native,
                                    pass,
                                    static_cast<std::uint32_t>(kMesh->source->indices.size()),
                                    kCount,
                                    0,
                                    0,
                                    kFirst) != mrhi_success) {
                    return failed("a caster could not be drawn", mrhi_errorState);
                }
            }
        }
        if (mrhiEndPass(native, pass) != mrhi_success) {
            return failed("a shadow pass could not end", mrhi_errorState);
        }
        return {};
    }

    /// The sun's cascades, each its square of the map two by two; and the
    /// punctual lights' squares of their atlas.
    result::Status castShadows(const Declared& now) {
        std::vector<Square> squares;
        const auto kSide = static_cast<float>(now.side);
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            squares.push_back({.view = now.cascades[at],
                               .viewport = {.x = static_cast<float>(at % 2) * kSide,
                                            .y = static_cast<float>(at / 2) * kSide,
                                            .width = kSide,
                                            .height = kSide,
                                            .minDepth = 0,
                                            .maxDepth = 1},
                               .casters = &now.placed.casterRuns});
        }
        RAWFRAME_TRY(cast(now, now.shadowPass, squares));
        squares.clear();
        for (std::size_t at = 0; at < now.slotViews.size(); ++at) {
            squares.push_back(
                {.view = now.slotViews[at], .viewport = now.slotViewports[at], .casters = &now.placed.slotRuns[at]});
        }
        return cast(now, now.lightShadowPass, squares);
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
        /// The punctual lights' shadow atlas, its squares as the shaders read
        /// them, each square's view and where it lies, and the pass drawing
        /// their casters (D292).
        mrhiResourceId lightShadowMap{};
        std::vector<SlotBlock> slots;
        mrhiResourceId slotsResource{};
        std::vector<Matrix4> slotMatrices;
        std::vector<mrhiResourceId> slotViews;
        std::vector<mrhiViewport> slotViewports;
        mrhiPassId lightShadowPass{};
        /// The frame's lights, each cluster's first index and count, and
        /// the indices (D290), as written and as declared.
        std::vector<LightBlock> lights;
        std::vector<std::uint32_t> ranges;
        std::vector<std::uint32_t> indices;
        mrhiResourceId lightsResource{};
        mrhiResourceId rangesResource{};
        mrhiResourceId indicesResource{};
        /// The camera's grade and tonemapper, as the picture's pass reads
        /// them (D294, D295).
        PictureBlock picture;
        mrhiResourceId pictureResource{};
        /// The sky's light, as its pass reads it (D293); the target's size.
        SkyBlock sky;
        mrhiResourceId skyResource{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        /// Where each texel's point moved, the temporal pass's input (D291).
        mrhiResourceId motion{};
        /// With FXAA, the tonemapped picture it reads, and its pass (D296).
        bool smoothed = false;
        mrhiResourceId display{};
        mrhiPassId fxaaPass{};
        bool draws = false;
        bool casters = false;
    };

    result::Status declare(render::Frame& open) {
        declared.reset();
        if (frame == nullptr) {
            return {};
        }
        device->pump();
        RAWFRAME_TRY_ASSIGN(const bool kReady, pipelines.ready());
        if (!kReady) {
            ++statistics.framesWaiting;
            return {};
        }
        Declared now;
        // The placements and lights come first in the frame's uploads; the
        // meshes share what is left.
        const std::uint64_t kPlacementBytes =
            (std::uint64_t{frame->draws.size()} * kInstanceBytes) +
            (std::uint64_t{frame->lights3d.size()} * sizeof(LightBlock)) +
            (std::uint64_t{frame->lightShadows.slots.size()} * (sizeof(SlotBlock) + sizeof(Matrix4))) +
            ((std::uint64_t{frame->clusters.ranges.size()} + frame->clusters.indices.size()) * sizeof(std::uint32_t));
        const std::uint64_t kBudget =
            kPlacementBytes < limits.uploadBytesPerFrame ? limits.uploadBytesPerFrame - kPlacementBytes : 0;
        const std::map<std::uint64_t, Held*> kUsable = meshesOf(*frame, meshes, kBudget, now.uploads);
        now.placed = place(*frame, kUsable);
        now.block = blockOf(*frame, open.width, open.height);
        now.lights = lightsOf(*frame);
        now.ranges = now.block.clusterGrid[3] > 0 ? frame->clusters.ranges : std::vector<std::uint32_t>{0, 0};
        now.indices = now.block.clusterGrid[3] > 0 && !frame->clusters.indices.empty() ? frame->clusters.indices
                                                                                       : std::vector<std::uint32_t>{0};
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
        now.casters = !now.placed.casterRuns.empty() || std::ranges::any_of(now.placed.slotRuns, [](const Runs& runs) {
            return !runs.empty();
        });
        if (now.draws || now.casters) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = now.placed.instances.size() * sizeof(float);
            if (mrhiDeclareBuffer(native, &def, &now.instances) != mrhi_success) {
                return failed("the frame's placements could not be declared", mrhi_errorCapacity);
            }
        }
        now.width = open.width;
        now.height = open.height;
        now.sky.light = now.block.sky;
        now.picture = pictureOf(*frame);
        mrhiBufferDef pictureBlockDef = mrhiDefaultBufferDef();
        pictureBlockDef.size = sizeof(PictureBlock);
        if (mrhiDeclareBuffer(native, &pictureBlockDef, &now.pictureResource) != mrhi_success) {
            return failed("the picture's grade could not be declared", mrhi_errorCapacity);
        }
        mrhiBufferDef skyDef = mrhiDefaultBufferDef();
        skyDef.size = sizeof(SkyBlock);
        if (mrhiDeclareBuffer(native, &skyDef, &now.skyResource) != mrhi_success) {
            return failed("the sky's light could not be declared", mrhi_errorCapacity);
        }
        mrhiBufferDef blockDef = mrhiDefaultBufferDef();
        blockDef.size = sizeof(FrameBlock);
        if (mrhiDeclareBuffer(native, &blockDef, &now.blockResource) != mrhi_success) {
            return failed("the frame's view could not be declared", mrhi_errorCapacity);
        }
        for (const auto& [kBytes, kMade] :
             {std::pair{now.lights.size() * sizeof(LightBlock), &now.lightsResource},
              std::pair{now.ranges.size() * sizeof(std::uint32_t), &now.rangesResource},
              std::pair{now.indices.size() * sizeof(std::uint32_t), &now.indicesResource}}) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = kBytes;
            if (mrhiDeclareBuffer(native, &def, kMade) != mrhi_success) {
                return failed("the frame's lights could not be declared", mrhi_errorCapacity);
            }
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
        // The punctual lights' atlas: their squares, or a texel nothing reads
        // the depth of when there are none; each square's view.
        const render_scene::SceneLightShadows& kLightShadows = frame->lightShadows;
        now.slots = slotsOf(*frame);
        mrhiTextureDef atlasDef = mrhiDefaultTextureDef();
        atlasDef.format = kShadowFormat;
        atlasDef.width = std::max<std::uint32_t>(kLightShadows.side, 1);
        atlasDef.height = atlasDef.width;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &atlasDef, &now.lightShadowMap);
            kDeclared != mrhi_success) {
            return failed("the lights' shadow atlas could not be declared", kDeclared);
        }
        mrhiBufferDef slotsDef = mrhiDefaultBufferDef();
        slotsDef.size = now.slots.size() * sizeof(SlotBlock);
        if (mrhiDeclareBuffer(native, &slotsDef, &now.slotsResource) != mrhi_success) {
            return failed("the lights' shadow squares could not be declared", mrhi_errorCapacity);
        }
        for (const render_scene::ShadowSlot& slot : kLightShadows.slots) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = sizeof(Matrix4);
            if (mrhiDeclareBuffer(native, &def, &now.slotViews.emplace_back()) != mrhi_success) {
                return failed("a shadow square's view could not be declared", mrhi_errorCapacity);
            }
            now.slotMatrices.push_back(slot.viewProjection);
            now.slotViewports.push_back({.x = static_cast<float>(slot.x),
                                         .y = static_cast<float>(slot.y),
                                         .width = static_cast<float>(slot.side),
                                         .height = static_cast<float>(slot.side),
                                         .minDepth = 0,
                                         .maxDepth = 1});
        }
        mrhiResourceId depthTarget{};
        // The models' pipeline writes the motion whether or not it is read.
        for (const auto& [kFormat, kMade] : {std::pair{kSceneFormat, &now.scene},
                                             std::pair{kMotionFormat, &now.motion},
                                             std::pair{kDepthFormat, &depthTarget}}) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kFormat;
            def.width = open.width;
            def.height = open.height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, kMade); kDeclared != mrhi_success) {
                return failed("a target could not be declared", kDeclared);
            }
        }
        now.smoothed = frame->fxaa;
        if (now.smoothed) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kPictureFormat;
            def.width = open.width;
            def.height = open.height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, &now.display);
                kDeclared != mrhi_success) {
                return failed("a target could not be declared", kDeclared);
            }
        }

        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes = {wholeOf(now.blockResource, mrhi_accessCopyDestination),
                                          wholeOf(now.lightsResource, mrhi_accessCopyDestination),
                                          wholeOf(now.rangesResource, mrhi_accessCopyDestination),
                                          wholeOf(now.indicesResource, mrhi_accessCopyDestination)};
        if (now.draws || now.casters) {
            writes.push_back(wholeOf(now.instances, mrhi_accessCopyDestination));
        }
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            writes.push_back(wholeOf(now.cascades[at], mrhi_accessCopyDestination));
        }
        // Antialiased over time, the temporal pass blends the frame with the
        // picture before into the other kept picture (D291).
        RAWFRAME_TRY(temporal->declare(*frame, open.width, open.height, writes));
        writes.push_back(wholeOf(now.slotsResource, mrhi_accessCopyDestination));
        writes.push_back(wholeOf(now.skyResource, mrhi_accessCopyDestination));
        writes.push_back(wholeOf(now.pictureResource, mrhi_accessCopyDestination));
        RAWFRAME_TRY(metering->declare(*frame, writes));
        for (const mrhiResourceId kView : now.slotViews) {
            writes.push_back(wholeOf(kView, mrhi_accessCopyDestination));
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
        std::vector<mrhiAccess> slotReads = meshReads;
        for (const mrhiResourceId kView : now.slotViews) {
            slotReads.push_back(wholeOf(kView, mrhi_accessUniform));
        }
        mrhiPassDef atlasPassDef = shadowDef2;
        atlasPassDef.accesses = slotReads.data();
        atlasPassDef.accessCount = static_cast<std::uint32_t>(slotReads.size());
        atlasPassDef.depthTarget.resource = now.lightShadowMap;
        if (const mrhiResult kAdded = mrhiAddPass(native, &atlasPassDef, &now.lightShadowPass);
            kAdded != mrhi_success) {
            return failed("the lights' shadow pass could not be added", kAdded);
        }
        // The models' passes read the shadow map too: the scene's table
        // holds it for both.
        std::vector<mrhiAccess> reads = meshReads;
        reads.push_back(wholeOf(now.blockResource, mrhi_accessUniform));
        reads.push_back(wholeOf(now.skyResource, mrhi_accessUniform));
        reads.push_back(wholeOf(metering->exposure(), mrhi_accessStorageRead));
        for (const mrhiResourceId kLights :
             {now.lightsResource, now.rangesResource, now.indicesResource, now.slotsResource}) {
            reads.push_back(wholeOf(kLights, mrhi_accessStorageRead));
        }
        reads.push_back(mrhiAccess{.resource = now.lightShadowMap,
                                   .kind = mrhi_accessSampled,
                                   .range = {.baseMip = 0,
                                             .mipCount = MRHI_REMAINING,
                                             .baseLayer = 0,
                                             .layerCount = 1,
                                             .aspect = mrhi_aspectDepthOnly}});
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
        // The models, then the sky where none lies, drawn with the exposure
        // the device holds (D293).
        mrhiPassDef litDef = depthDef;
        litDef.colorTargets[0].resource = now.scene;
        litDef.colorTargets[0].load = mrhi_loadClear;
        litDef.colorTargets[0].store = mrhi_storeKeep;
        litDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        litDef.colorTargets[1].resource = now.motion;
        litDef.colorTargets[1].load = mrhi_loadClear;
        litDef.colorTargets[1].store = mrhi_storeKeep;
        litDef.colorTargets[1].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 0};
        litDef.colorTargetCount = 2;
        litDef.depthTarget.depthLoad = mrhi_loadKeep;
        litDef.depthTarget.depthStore = mrhi_storeKeep;
        litDef.depthTarget.readOnly = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &litDef, &now.litPass); kAdded != mrhi_success) {
            return failed("the models' pass could not be added", kAdded);
        }
        RAWFRAME_TRY(metering->addPasses(now.scene));
        RAWFRAME_TRY(temporal->addPass(now.scene, now.motion));
        // The picture: every pixel of it written, over whatever was there;
        // with FXAA, the tonemapped picture first, then FXAA over it into
        // the frame's (D296).
        const mrhiAccess kScene = wholeOf(temporal->shown(now.scene), mrhi_accessSampled);
        mrhiPassDef pictureDef = mrhiDefaultPassDef();
        pictureDef.colorTargets[0].resource = resourceOf(open.picture);
        pictureDef.colorTargets[0].load = open.clearsPicture() ? mrhi_loadClear : mrhi_loadKeep;
        pictureDef.colorTargets[0].store = mrhi_storeKeep;
        pictureDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        pictureDef.colorTargetCount = 1;
        pictureDef.neverCull = true;
        mrhiPassDef tonemapDef = pictureDef;
        if (now.smoothed) {
            tonemapDef.colorTargets[0].resource = now.display;
            tonemapDef.colorTargets[0].load = mrhi_loadDiscard;
        }
        const std::array<mrhiAccess, 2> kPictureReads = {kScene, wholeOf(now.pictureResource, mrhi_accessUniform)};
        tonemapDef.accesses = kPictureReads.data();
        tonemapDef.accessCount = static_cast<std::uint32_t>(kPictureReads.size());
        if (const mrhiResult kAdded = mrhiAddPass(native, &tonemapDef, &now.picturePass); kAdded != mrhi_success) {
            return failed("the picture's pass could not be added", kAdded);
        }
        const mrhiAccess kDisplay = wholeOf(now.display, mrhi_accessSampled);
        pictureDef.accesses = &kDisplay;
        pictureDef.accessCount = 1;
        if (now.smoothed) {
            if (const mrhiResult kAdded = mrhiAddPass(native, &pictureDef, &now.fxaaPass); kAdded != mrhi_success) {
                return failed("the FXAA pass could not be added", kAdded);
            }
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
        if (mrhiWriteBuffer(
                native, now.upload, now.lightsResource, 0, now.lights.data(), now.lights.size() * sizeof(LightBlock)) !=
                mrhi_success ||
            mrhiWriteBuffer(native,
                            now.upload,
                            now.rangesResource,
                            0,
                            now.ranges.data(),
                            now.ranges.size() * sizeof(std::uint32_t)) != mrhi_success ||
            mrhiWriteBuffer(native,
                            now.upload,
                            now.indicesResource,
                            0,
                            now.indices.data(),
                            now.indices.size() * sizeof(std::uint32_t)) != mrhi_success) {
            return failed("the frame's lights could not be written", mrhi_errorCapacity);
        }
        if (mrhiWriteBuffer(native, now.upload, now.skyResource, 0, &now.sky, sizeof(SkyBlock)) != mrhi_success ||
            mrhiWriteBuffer(native, now.upload, now.pictureResource, 0, &now.picture, sizeof(PictureBlock)) !=
                mrhi_success) {
            return failed("the sky's light could not be written", mrhi_errorCapacity);
        }
        RAWFRAME_TRY(metering->write(now.upload));
        RAWFRAME_TRY(temporal->write(now.upload));
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            if (mrhiWriteBuffer(native, now.upload, now.cascades[at], 0, &now.block.cascades[at], sizeof(Matrix4)) !=
                mrhi_success) {
                return failed("a cascade's view could not be written", mrhi_errorCapacity);
            }
        }
        if (mrhiWriteBuffer(
                native, now.upload, now.slotsResource, 0, now.slots.data(), now.slots.size() * sizeof(SlotBlock)) !=
            mrhi_success) {
            return failed("the lights' shadow squares could not be written", mrhi_errorCapacity);
        }
        for (std::size_t at = 0; at < now.slotViews.size(); ++at) {
            if (mrhiWriteBuffer(native, now.upload, now.slotViews[at], 0, &now.slotMatrices[at], sizeof(Matrix4)) !=
                mrhi_success) {
                return failed("a shadow square's view could not be written", mrhi_errorCapacity);
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
        const auto kStored = [](std::uint32_t slot, mrhiResourceId resource, std::uint64_t bytes) {
            return mrhiBinding{.slot = slot,
                               .resource = resource,
                               .offset = 0,
                               .size = bytes,
                               .viewKind = mrhi_texture2d,
                               .viewFormat = mrhi_formatNone,
                               .range = {},
                               .sampler = {}};
        };
        const std::array<mrhiBinding, 9> kFrameBinding = {
            mrhiBinding{.slot = 0,
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
                        .sampler = pipelines.shadowSampler},
            kStored(3, now.lightsResource, now.lights.size() * sizeof(LightBlock)),
            kStored(4, now.rangesResource, now.ranges.size() * 4),
            kStored(5, now.indicesResource, now.indices.size() * 4),
            mrhiBinding{.slot = 6,
                        .resource = now.lightShadowMap,
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
            kStored(7, now.slotsResource, now.slots.size() * sizeof(SlotBlock)),
            kStored(8, metering->exposure(), sizeof(ExposureBlock))};
        const std::array<mrhiBinding, 2> kSkyBinding = {kStored(0, now.skyResource, sizeof(SkyBlock)),
                                                        kStored(1, metering->exposure(), sizeof(ExposureBlock))};
        for (const auto& [kPass, kPipeline, kLit] : {std::tuple{now.depthPass, pipelines.depth.pipeline, false},
                                                     std::tuple{now.litPass, pipelines.lit.pipeline, true}}) {
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
            if (kLit && (mrhiSetGraphicsPipeline(native, kPass, pipelines.sky.pipeline) != mrhi_success ||
                         mrhiSetBindings(native, kPass, 0, kSkyBinding.data(), kSkyBinding.size()) != mrhi_success ||
                         mrhiDraw(native, kPass, 3, 1, 0, 0) != mrhi_success)) {
                return failed("the sky could not be drawn", mrhi_errorState);
            }
            if (mrhiEndPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not end", mrhi_errorState);
            }
        }
        RAWFRAME_TRY(metering->record(pipelines, now.scene, now.width, now.height));
        RAWFRAME_TRY(temporal->record(pipelines, now.scene, now.motion));
        const std::array<mrhiBinding, 2> kSceneBinding = {
            mrhiBinding{
                .slot = 0,
                .resource = temporal->shown(now.scene),
                .offset = 0,
                .size = 0,
                .viewKind = mrhi_texture2d,
                .viewFormat = mrhi_formatNone,
                .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
                .sampler = {}},
            mrhiBinding{.slot = 1,
                        .resource = now.pictureResource,
                        .offset = 0,
                        .size = sizeof(PictureBlock),
                        .viewKind = mrhi_texture2d,
                        .viewFormat = mrhi_formatNone,
                        .range = {},
                        .sampler = {}}};
        if (mrhiBeginPass(native, now.picturePass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native, now.picturePass, pipelines.tonemap.pipeline) != mrhi_success ||
            mrhiSetBindings(native, now.picturePass, 0, kSceneBinding.data(), kSceneBinding.size()) != mrhi_success ||
            mrhiDraw(native, now.picturePass, 3, 1, 0, 0) != mrhi_success ||
            mrhiEndPass(native, now.picturePass) != mrhi_success) {
            return failed("the picture could not be drawn", mrhi_errorState);
        }
        if (!now.smoothed) {
            return {};
        }
        const std::array<mrhiBinding, 2> kDisplayBinding = {mrhiBinding{.slot = 0,
                                                                        .resource = now.display,
                                                                        .offset = 0,
                                                                        .size = 0,
                                                                        .viewKind = mrhi_texture2d,
                                                                        .viewFormat = mrhi_formatNone,
                                                                        .range = kSceneBinding[0].range,
                                                                        .sampler = {}},
                                                            mrhiBinding{.slot = 1,
                                                                        .resource = {},
                                                                        .offset = 0,
                                                                        .size = 0,
                                                                        .viewKind = mrhi_texture2d,
                                                                        .viewFormat = mrhi_formatNone,
                                                                        .range = {},
                                                                        .sampler = pipelines.filteredSampler}};
        if (mrhiBeginPass(native, now.fxaaPass) != mrhi_success ||
            mrhiSetGraphicsPipeline(native, now.fxaaPass, pipelines.fxaa.pipeline) != mrhi_success ||
            mrhiSetBindings(native, now.fxaaPass, 0, kDisplayBinding.data(), kDisplayBinding.size()) != mrhi_success ||
            mrhiDraw(native, now.fxaaPass, 3, 1, 0, 0) != mrhi_success ||
            mrhiEndPass(native, now.fxaaPass) != mrhi_success) {
            return failed("the picture could not be antialiased", mrhi_errorState);
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
            statistics.framesSmoothed += declared->smoothed ? 1 : 0;
            if (temporal->enabled()) {
                ++statistics.framesResolved;
                statistics.historyReused += temporal->reused() ? 1 : 0;
            }
            statistics.models += declared->placed.instances.size() * sizeof(float) / kInstanceBytes;
            statistics.drawCalls += declared->placed.runs.size();
        }
        temporal->ended(submitted);
        if (submitted && metering->metered()) {
            ++statistics.framesMetered;
        }
        metering->ended(submitted);
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
    state->pipelines.device = &device;
    state->pipelines.native = device.native();
    RAWFRAME_TRY(state->pipelines.make());
    state->metering.emplace(device.native());
    state->temporal.emplace(device.native());
    RAWFRAME_TRY(state->metering->make());
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
