#include "rawframe/render_scene_gpu/renderer.h"

#include "blocks.h"
#include "environment.h"
#include "meshes.h"
#include "metering.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene_gpu/errors.h"
#include "runs.h"
#include "tables.h"
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
        .range = {
            .baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = MRHI_REMAINING, .aspect = {}}};
}

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
    std::optional<DeviceMeshes> held;
    /// The materials' textures (D309), and the white one a material
    /// sampling none samples, held as texture nought.
    std::unique_ptr<render::DeviceTextures> textures;
    std::shared_ptr<const texture::Texture> white;
    /// The sky's picture (D322): the black cube bound where there is
    /// none, the picture last seen, and its irradiance.
    std::shared_ptr<const texture::Texture> dark;
    std::shared_ptr<const texture::Texture> environment;
    std::array<std::array<float, 4>, 9> irradiance{};
    /// What the next frame draws.
    const render_scene::SceneFrame* frame = nullptr;
    MeshSource meshes;
    TextureSource sampled;

    /// The meshes this frame draws, chosen within the frame's upload
    /// budget. A draw whose mesh is not chosen is left out.
    std::map<std::uint64_t, const HeldMesh*>
    meshesOf(const render_scene::SceneFrame& scene, const MeshSource& given, std::uint64_t budget) {
        held->begin(budget);
        std::map<std::uint64_t, const HeldMesh*> usable;
        for (const std::vector<render_scene::SceneDraw>* kList :
             {&scene.draws, &scene.shadows.casters, &scene.lightShadows.casters}) {
            for (const render_scene::SceneDraw& draw : *kList) {
                if (!usable.contains(draw.mesh)) {
                    if (const HeldMesh* kMesh = held->choose(draw.mesh, given, statistics); kMesh != nullptr) {
                        usable.emplace(draw.mesh, kMesh);
                    }
                }
            }
        }
        return usable;
    }

    /// A texture and its sampler into two slots of a table: the texture
    /// held this frame, or white for none and for one not held.
    void bindTexture(mrhiBinding& image, mrhiBinding& filter, const render_scene::SceneTexture& texture) const {
        const std::uint64_t kHeld = textures->resource(texture.id);
        image.resource = resourceOf(kHeld != 0 ? kHeld : textures->resource(0));
        filter.sampler = pipelines.materialSamplers[samplerOf(texture.filter, texture.address)];
    }

    /// The textures this frame's materials sample, white first, chosen
    /// within `budget`; a material whose texture is not chosen samples
    /// white.
    void texturesOf(const render_scene::SceneFrame& scene, std::uint64_t budget) {
        textures->begin(budget);
        static_cast<void>(textures->choose(0, white));
        static_cast<void>(textures->choose(kNoEnvironment, dark));
        // The sky's picture, if it is one; its irradiance taken again only
        // for another picture.
        if (const std::uint64_t kSky = scene.lights.environment; kSky != 0 && sampled) {
            const std::shared_ptr<const texture::Texture> kPicture = sampled(kSky);
            if (kPicture != nullptr && isEnvironment(*kPicture)) {
                static_cast<void>(textures->choose(kSky, kPicture));
                if (kPicture != environment) {
                    irradiance = irradianceOf(*kPicture);
                    environment = kPicture;
                }
            }
        }
        for (const std::vector<render_scene::SceneDraw>* kList :
             {&scene.draws, &scene.shadows.casters, &scene.lightShadows.casters}) {
            for (const render_scene::SceneDraw& draw : *kList) {
                if (draw.material >= scene.textures.size()) {
                    continue;
                }
                const render_scene::SceneTextures& kTextures = scene.textures[draw.material];
                for (const std::uint64_t kId :
                     {kTextures.base.id, kTextures.packed.id, kTextures.emission.id, kTextures.normal.id}) {
                    if (kId != 0) {
                        static_cast<void>(textures->choose(kId, sampled ? sampled(kId) : nullptr));
                    }
                }
            }
        }
    }

    struct Declared;

    /// A square of a shadow map: its view, where it lies, and its casters.
    struct Square {
        mrhiResourceId view{};
        mrhiViewport viewport{};
        const Casters* casters = nullptr;
    };

    /// A shadow pass recorded: each square drawn from its view, its solid
    /// casters in their runs, then its masked ones cut by their material's
    /// texture (D310).
    result::Status cast(const Declared& now, mrhiPassId pass, std::span<const Square> squares) {
        if (mrhiBeginPass(native, pass) != mrhi_success) {
            return failed("a shadow pass could not begin", mrhi_errorState);
        }
        const auto kDraw = [this, pass](const Run& run) -> result::Status {
            RAWFRAME_TRY(held->bind(pass, *run.mesh));
            if (mrhiDrawIndexed(native, pass, run.indexCount, run.count, run.firstIndex, 0, run.first) !=
                mrhi_success) {
                return failed("a caster could not be drawn", mrhi_errorState);
            }
            return {};
        };
        for (const Square& kSquare : squares) {
            if (kSquare.casters->empty()) {
                continue;
            }
            std::array<mrhiBinding, 4> binding = {
                bufferAt(0, kSquare.view, sizeof(Matrix4)),
                bufferAt(1, now.materialsResource, now.materials.size() * sizeof(render_scene::MaterialBlob)),
                textureAt(2, resourceOf(textures->resource(0))),
                samplerAt(3, pipelines.materialSamplers[0])};
            if (mrhiSetViewport(native, pass, &kSquare.viewport) != mrhi_success) {
                return failed("a shadow square could not be set up", mrhi_errorState);
            }
            if (!kSquare.casters->solid.empty()) {
                if (mrhiSetGraphicsPipeline(native, pass, pipelines.casting.pipeline) != mrhi_success ||
                    mrhiSetVertexBuffer(native, pass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
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
                mrhiSetVertexBuffer(native, pass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                return failed("the masked casters could not be set up", mrhi_errorState);
            }
            std::optional<render_scene::SceneTexture> bound;
            for (const Run& run : kSquare.casters->masked) {
                if (bound != run.texture.base) {
                    bindTexture(binding[2], binding[3], run.texture.base);
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
                               .casters = &now.placed.cascadeRuns[at]});
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
        Placed placed;
        FrameBlock block;
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
        /// The frame's materials' blobs (D303).
        std::vector<render_scene::MaterialBlob> materials;
        mrhiResourceId materialsResource{};
        mrhiResourceId rangesResource{};
        mrhiResourceId indicesResource{};
        /// The camera's grade and tonemapper, as the picture's pass reads
        /// them (D294, D295).
        PictureBlock picture;
        mrhiResourceId pictureResource{};
        /// The sky's light, as its pass reads it (D293); the target's size.
        SkyBlock sky;
        mrhiResourceId skyResource{};
        /// The sky's picture bound this frame, or the dark cube (D322).
        std::uint64_t environment = kNoEnvironment;
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
        // White's four bytes and the dark cube's 48 are kept aside, so they
        // are always there.
        const std::uint64_t kWhite = std::min<std::uint64_t>(kBudget, 4 + 48);
        const std::map<std::uint64_t, const HeldMesh*> kUsable = meshesOf(*frame, meshes, kBudget - kWhite);
        texturesOf(*frame, held->left() + kWhite);
        now.placed = placeDraws(*frame, kUsable, statistics.modelsLeftOut);
        now.block = blockOf(*frame, open.width, open.height);
        now.lights = lightsOf(*frame);
        now.ranges = now.block.clusterGrid[3] > 0 ? frame->clusters.ranges : std::vector<std::uint32_t>{0, 0};
        now.indices = now.block.clusterGrid[3] > 0 && !frame->clusters.indices.empty() ? frame->clusters.indices
                                                                                       : std::vector<std::uint32_t>{0};
        // Everything this frame uses: the meshes it draws, imported; its
        // placements and view; and its targets.
        std::vector<mrhiAccess> meshWrites;
        std::vector<mrhiAccess> meshReads;
        RAWFRAME_TRY(held->import(meshWrites, meshReads));
        RAWFRAME_TRY(textures->import());
        for (const std::uint64_t kTexture : textures->uploading()) {
            meshWrites.push_back(wholeOf(resourceOf(kTexture), mrhi_accessCopyDestination));
        }
        now.draws = !now.placed.runs.empty() || !now.placed.maskedRuns.empty() || !now.placed.translucentRuns.empty();
        const auto kAny = [](const Casters& casters) {
            return !casters.empty();
        };
        now.casters =
            std::ranges::any_of(now.placed.cascadeRuns, kAny) || std::ranges::any_of(now.placed.slotRuns, kAny);
        if (now.draws || now.casters) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = now.placed.instances.size() * sizeof(float);
            if (mrhiDeclareBuffer(native, &def, &now.instances) != mrhi_success) {
                return failed("the frame's placements could not be declared", mrhi_errorCapacity);
            }
        }
        now.width = open.width;
        now.height = open.height;
        // The sky's picture where it is held this frame: its levels, and
        // the irradiance taken from it.
        const std::uint64_t kSky = frame->lights.environment;
        now.environment = kSky != 0 && environment != nullptr && textures->cube(kSky) && textures->resource(kSky) != 0
                              ? kSky
                              : kNoEnvironment;
        if (now.environment != kNoEnvironment) {
            now.block.environment = {0, 0, 0, static_cast<float>(environment->levels.size())};
            now.block.irradiance = irradiance;
        }
        now.sky = SkyBlock{.light = now.block.sky,
                           .environment = now.block.environment,
                           .toDirection = inverseOf(now.block.viewProjection),
                           .unjittered = now.block.unjittered,
                           .previous = now.block.previous};
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
        now.materials = frame->materials.empty() ? std::vector{render_scene::noMaterial()} : frame->materials;
        for (const auto& [kBytes, kMade] :
             {std::pair{now.materials.size() * sizeof(render_scene::MaterialBlob), &now.materialsResource},
              std::pair{now.lights.size() * sizeof(LightBlock), &now.lightsResource},
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
                                          wholeOf(now.materialsResource, mrhi_accessCopyDestination),
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
        writes.insert(writes.end(), meshWrites.begin(), meshWrites.end());
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = writes.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(writes.size());
        if (const mrhiResult kAdded = mrhiAddPass(native, &uploadDef, &now.upload); kAdded != mrhi_success) {
            return failed("the upload pass could not be added", kAdded);
        }
        // The shadow map first: its casters from each cascade's view.
        if (now.draws || now.casters) {
            meshReads.push_back(wholeOf(now.instances, mrhi_accessVertex));
        }
        // Every pass drawing models reads the materials and their textures:
        // the shadow passes to cut masked casters (D310).
        meshReads.push_back(wholeOf(now.materialsResource, mrhi_accessStorageRead));
        for (const std::uint64_t kTexture : textures->chosen()) {
            meshReads.push_back(wholeOf(resourceOf(kTexture), mrhi_accessSampled));
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
                            now.materialsResource,
                            0,
                            now.materials.data(),
                            now.materials.size() * sizeof(render_scene::MaterialBlob)) != mrhi_success ||
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
        RAWFRAME_TRY(held->write(now.upload));
        RAWFRAME_TRY(textures->write(render::requestKey(now.upload.index1, now.upload.generation)));
        if (mrhiEndPass(native, now.upload) != mrhi_success) {
            return failed("the upload pass could not end", mrhi_errorState);
        }
        RAWFRAME_TRY(castShadows(now));
        // The scene's table: slots 10 to 17 are each run's textures; 18 and
        // 19 the sky's picture (D322).
        const mrhiBinding kPicture = cubeAt(18, resourceOf(textures->resource(now.environment)));
        const mrhiBinding kPictureSampler =
            samplerAt(19, pipelines.materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)]);
        const std::array<mrhiBinding, 20> kFrameBinding = {
            bufferAt(0, now.blockResource, sizeof(FrameBlock)),
            depthAt(1, now.shadowMap),
            samplerAt(2, pipelines.shadowSampler),
            bufferAt(3, now.lightsResource, now.lights.size() * sizeof(LightBlock)),
            bufferAt(4, now.rangesResource, now.ranges.size() * 4),
            bufferAt(5, now.indicesResource, now.indices.size() * 4),
            depthAt(6, now.lightShadowMap),
            bufferAt(7, now.slotsResource, now.slots.size() * sizeof(SlotBlock)),
            bufferAt(8, metering->exposure(), sizeof(ExposureBlock)),
            bufferAt(9, now.materialsResource, now.materials.size() * sizeof(render_scene::MaterialBlob)),
            textureAt(10, {}),
            samplerAt(11, {}),
            textureAt(12, {}),
            samplerAt(13, {}),
            textureAt(14, {}),
            samplerAt(15, {}),
            textureAt(16, {}),
            samplerAt(17, {}),
            kPicture,
            kPictureSampler};
        std::array<mrhiBinding, 4> skyBinding = {bufferAt(0, now.skyResource, sizeof(SkyBlock)),
                                                 bufferAt(1, metering->exposure(), sizeof(ExposureBlock)),
                                                 kPicture,
                                                 kPictureSampler};
        skyBinding[2].slot = 2;
        skyBinding[3].slot = 3;
        // Runs of models drawn with `pipeline` in `pass`.
        // Runs of models drawn with `pipeline` in `pass`, the table set
        // again where a run samples another texture than the one before
        // (D309): white for none, and for one not held this frame.
        const auto kDrawRuns = [this, &now, &kFrameBinding](mrhiPassId pass,
                                                            mrhiGraphicsPipelineId pipeline,
                                                            const Runs& runs) -> result::Status {
            if (runs.empty()) {
                return {};
            }
            if (mrhiSetGraphicsPipeline(native, pass, pipeline) != mrhi_success ||
                mrhiSetVertexBuffer(native, pass, 1, now.instances, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                return failed("the models could not be set up", mrhi_errorState);
            }
            std::array<mrhiBinding, 20> binding = kFrameBinding;
            std::optional<render_scene::SceneTextures> bound;
            for (const Run& run : runs) {
                if (bound != run.texture) {
                    bindTexture(binding[10], binding[11], run.texture.base);
                    bindTexture(binding[12], binding[13], run.texture.packed);
                    bindTexture(binding[14], binding[15], run.texture.emission);
                    bindTexture(binding[16], binding[17], run.texture.normal);
                    if (mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                        return failed("a material's texture could not be bound", mrhi_errorState);
                    }
                    bound = run.texture;
                }
                RAWFRAME_TRY(held->bind(pass, *run.mesh));
                if (mrhiDrawIndexed(native, pass, run.indexCount, run.count, run.firstIndex, 0, run.first) !=
                    mrhi_success) {
                    return failed("a model could not be drawn", mrhi_errorState);
                }
            }
            return {};
        };
        // The opaque models, their depth first, the masked cut there
        // (D310); then, lit, the sky where none lies; then the translucent
        // over both (D305).
        for (const auto& [kPass, kPipeline, kLit] : {std::tuple{now.depthPass, pipelines.depth.pipeline, false},
                                                     std::tuple{now.litPass, pipelines.lit.pipeline, true}}) {
            if (mrhiBeginPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not begin", mrhi_errorState);
            }
            RAWFRAME_TRY(kDrawRuns(kPass, kPipeline, now.placed.runs));
            RAWFRAME_TRY(kDrawRuns(
                kPass, kLit ? pipelines.maskedLit.pipeline : pipelines.cutout.pipeline, now.placed.maskedRuns));
            if (kLit && (mrhiSetGraphicsPipeline(native, kPass, pipelines.sky.pipeline) != mrhi_success ||
                         mrhiSetBindings(native, kPass, 0, skyBinding.data(), skyBinding.size()) != mrhi_success ||
                         mrhiDraw(native, kPass, 3, 1, 0, 0) != mrhi_success)) {
                return failed("the sky could not be drawn", mrhi_errorState);
            }
            if (kLit) {
                RAWFRAME_TRY(kDrawRuns(kPass, pipelines.glass.pipeline, now.placed.translucentRuns));
            }
            if (mrhiEndPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not end", mrhi_errorState);
            }
        }
        RAWFRAME_TRY(metering->record(pipelines, now.scene, now.width, now.height));
        RAWFRAME_TRY(temporal->record(pipelines, now.scene, now.motion));
        const std::array<mrhiBinding, 2> kSceneBinding = {textureAt(0, temporal->shown(now.scene)),
                                                          bufferAt(1, now.pictureResource, sizeof(PictureBlock))};
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
        const std::array<mrhiBinding, 2> kDisplayBinding = {textureAt(0, now.display),
                                                            samplerAt(1, pipelines.filteredSampler)};
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
        held->ended(submitted, statistics);
        const std::uint64_t kUploaded = textures->statistics().texturesUploaded;
        const std::uint64_t kBytes = textures->statistics().uploadBytes;
        textures->ended(submitted);
        statistics.texturesUploaded += textures->statistics().texturesUploaded - kUploaded;
        statistics.textureBytes += textures->statistics().uploadBytes - kBytes;
        if (submitted) {
            ++statistics.frames;
            statistics.framesSmoothed += declared->smoothed ? 1 : 0;
            if (temporal->enabled()) {
                ++statistics.framesResolved;
                statistics.historyReused += temporal->reused() ? 1 : 0;
            }
            statistics.models += declared->placed.instances.size() * sizeof(float) / kInstanceBytes;
            statistics.drawCalls += declared->placed.runs.size() + declared->placed.maskedRuns.size() +
                                    declared->placed.translucentRuns.size();
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
    state->held.emplace(device.native(), limits.maximumMeshes);
    RAWFRAME_TRY_ASSIGN(state->textures,
                        render::DeviceTextures::create(device, {.maximumTextures = limits.maximumTextures + 1}));
    texture::Texture white{.format = texture::Format::Rgba8Srgb};
    white.levels.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{0xFF})});
    state->white = std::make_shared<const texture::Texture>(std::move(white));
    state->dark = darkCube();
    state->pipelines.device = &device;
    state->pipelines.native = device.native();
    RAWFRAME_TRY(state->pipelines.make());
    state->metering.emplace(device.native());
    state->temporal.emplace(device.native());
    RAWFRAME_TRY(state->metering->make());
    return std::unique_ptr<SceneRenderer>{new SceneRenderer{std::move(state)}};
}

void SceneRenderer::prepare(const render_scene::SceneFrame* frame, MeshSource meshes, TextureSource textures) {
    state_->frame = frame;
    state_->meshes = std::move(meshes);
    state_->sampled = std::move(textures);
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
