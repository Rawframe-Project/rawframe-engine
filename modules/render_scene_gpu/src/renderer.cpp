#include "rawframe/render_scene_gpu/renderer.h"

#include "blocks.h"
#include "bloom.h"
#include "capture.h"
#include "casting.h"
#include "contact.h"
#include "decals.h"
#include "environment.h"
#include "focus.h"
#include "meshes.h"
#include "metering.h"
#include "motion.h"
#include "occlusion.h"
#include "picture.h"
#include "pipelines.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_scene_gpu/errors.h"
#include "reflection.h"
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
    /// Its light read back for a tool (D326).
    std::optional<LightCapturing> capturing;
    /// Its ambient occlusion, when a view asks (D327).
    std::optional<OcclusionPass> occlusion;
    /// Its screen-space reflections, when a view asks (D331).
    std::optional<ReflectionPass> reflecting;
    /// Its contact shadows, when a view asks (D338).
    std::optional<ContactPass> contact;
    /// The decals' textures (D339).
    std::optional<DecalAtlas> decalAtlas;
    /// Its motion blur, when a view asks (D334).
    std::optional<MotionBlurPass> motionBlur;
    /// Its depth of field, when a view asks (D336).
    std::optional<DepthOfFieldPass> focus;
    /// Its bloom, when a view asks (D328).
    std::optional<BloomPass> bloom;
    /// Its picture, graded and tonemapped, and antialiased by FXAA when a
    /// view asks (D294 to D296).
    std::optional<PicturePass> picture;
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
    /// The reflection probes' pictures this frame asks for, and their
    /// levels (D325).
    std::map<std::uint64_t, std::uint32_t> probeLevels;
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
        // Each reflection probe's picture, if it is one (D325).
        probeLevels.clear();
        for (const render_scene::SceneProbe& kProbe : scene.probes) {
            const std::shared_ptr<const texture::Texture> kPicture = sampled ? sampled(kProbe.environment) : nullptr;
            if (kPicture != nullptr && isEnvironment(*kPicture)) {
                static_cast<void>(textures->choose(kProbe.environment, kPicture));
                probeLevels[kProbe.environment] = static_cast<std::uint32_t>(kPicture->levels.size());
            }
        }
        // Each decal's texture (D339).
        for (const render_scene::SceneDecal& kDecal : scene.decals) {
            static_cast<void>(textures->choose(kDecal.texture, sampled ? sampled(kDecal.texture) : nullptr));
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

    /// What the shadow passes draw with this frame.
    [[nodiscard]] Casting castingOf(const Declared& now) const noexcept {
        return Casting{.native = native,
                       .pipelines = &pipelines,
                       .held = &*held,
                       .textures = textures.get(),
                       .instances = now.instances,
                       .materials = now.materialsResource,
                       .materialsBytes = now.materials.size() * sizeof(render_scene::MaterialBlob)};
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
        RAWFRAME_TRY(cast(castingOf(now), now.shadowPass, squares));
        squares.clear();
        for (std::size_t at = 0; at < now.slotViews.size(); ++at) {
            squares.push_back(
                {.view = now.slotViews[at], .viewport = now.slotViewports[at], .casters = &now.placed.slotRuns[at]});
        }
        return cast(castingOf(now), now.lightShadowPass, squares);
    }

    /// What the open frame declared, until it is recorded and ends.
    struct Declared {
        Placed placed;
        FrameBlock block;
        mrhiResourceId instances{};
        mrhiResourceId blockResource{};
        mrhiResourceId scene{};
        /// The prepass's depth, and each point's surface beside it when a
        /// screen-space effect reads them (D327, D331).
        mrhiResourceId depth{};
        bool surfaced = false;
        mrhiResourceId surfaces{};
        mrhiPassId upload{};
        mrhiPassId depthPass{};
        mrhiPassId litPass{};
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
        /// The sky's light, as its pass reads it (D293); the target's size.
        SkyBlock sky;
        mrhiResourceId skyResource{};
        /// The sky's picture bound this frame, or the dark cube (D322).
        std::uint64_t environment = kNoEnvironment;
        /// What the models reflect (D325).
        Reflections reflections;
        mrhiResourceId probesResource{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        /// Where each texel's point moved, the temporal pass's input (D291).
        mrhiResourceId motion{};
        bool draws = false;
        bool casters = false;
    };

    /// Whether an effect a frame `wants` can be drawn: its pipelines asked
    /// for the first time it is wanted, and made (D337).
    result::Result<bool> made(bool wants, Effect effect) {
        if (!wants) {
            return false;
        }
        return pipelines.wanted(effect);
    }

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
            ((std::uint64_t{frame->clusters.ranges.size()} + frame->clusters.indices.size()) * sizeof(std::uint32_t)) +
            ((std::uint64_t{frame->probes.size()} + 1) * sizeof(ProbeBlock));
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
        // The clusters, where the lights or the decals are clustered (D339).
        const bool kClustered = now.block.clusterDepth[1] > 0;
        now.ranges = kClustered ? frame->clusters.ranges : std::vector<std::uint32_t>{0, 0, 0, 0};
        now.indices =
            kClustered && !frame->clusters.indices.empty() ? frame->clusters.indices : std::vector<std::uint32_t>{0};
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
        // What each model reflects: a probe's picture where it is held this
        // frame, else the sky's (D325).
        now.reflections =
            reflectionsOf(*frame,
                          now.environment,
                          static_cast<std::uint32_t>(now.block.environment[3]),
                          [this](std::uint64_t id) -> std::uint32_t {
                              const auto kFound = probeLevels.find(id);
                              return kFound != probeLevels.end() && textures->cube(id) && textures->resource(id) != 0
                                         ? kFound->second
                                         : 0;
                          });
        now.sky = SkyBlock{.light = now.block.sky,
                           .environment = now.block.environment,
                           .toDirection = inverseOf(now.block.viewProjection),
                           .unjittered = now.block.unjittered,
                           .previous = now.block.previous};
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
              std::pair{now.indices.size() * sizeof(std::uint32_t), &now.indicesResource},
              std::pair{now.reflections.blocks.size() * sizeof(ProbeBlock), &now.probesResource}}) {
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
        // The models' pipeline writes the motion whether or not it is read.
        for (const auto& [kFormat, kMade] : {std::pair{kSceneFormat, &now.scene},
                                             std::pair{kMotionFormat, &now.motion},
                                             std::pair{kDepthFormat, &now.depth}}) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kFormat;
            def.width = open.width;
            def.height = open.height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, kMade); kDeclared != mrhi_success) {
                return failed("a target could not be declared", kDeclared);
            }
        }

        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes = {wholeOf(now.blockResource, mrhi_accessCopyDestination),
                                          wholeOf(now.lightsResource, mrhi_accessCopyDestination),
                                          wholeOf(now.materialsResource, mrhi_accessCopyDestination),
                                          wholeOf(now.rangesResource, mrhi_accessCopyDestination),
                                          wholeOf(now.indicesResource, mrhi_accessCopyDestination),
                                          wholeOf(now.probesResource, mrhi_accessCopyDestination)};
        if (now.draws || now.casters) {
            writes.push_back(wholeOf(now.instances, mrhi_accessCopyDestination));
        }
        for (std::size_t at = 0; at < now.cascadeCount; ++at) {
            writes.push_back(wholeOf(now.cascades[at], mrhi_accessCopyDestination));
        }
        // Antialiased over time, the temporal pass blends the frame with the
        // picture before into the other kept picture (D291).
        RAWFRAME_TRY(temporal->declare(*frame, open.width, open.height, writes));
        // Each effect the view wants, once its pipelines are made (D337).
        RAWFRAME_TRY_ASSIGN(const bool kSurfaces,
                            made(frame->occlusion.enabled || frame->reflections.enabled, Effect::Surfaces));
        RAWFRAME_TRY_ASSIGN(const bool kOccluding, made(frame->occlusion.enabled, Effect::Occlusion));
        RAWFRAME_TRY_ASSIGN(const bool kReflecting, made(frame->reflections.enabled, Effect::Reflections));
        RAWFRAME_TRY_ASSIGN(const bool kBlurring, made(frame->motionBlur.enabled, Effect::MotionBlur));
        RAWFRAME_TRY_ASSIGN(const bool kFocusing, made(frame->depthOfField.enabled, Effect::DepthOfField));
        RAWFRAME_TRY_ASSIGN(const bool kBlooming, made(frame->bloom.enabled, Effect::Bloom));
        RAWFRAME_TRY_ASSIGN(const bool kSmoothing, made(frame->fxaa, Effect::Fxaa));
        RAWFRAME_TRY_ASSIGN(const bool kContacting, made(frame->contactShadows.enabled, Effect::ContactShadows));
        RAWFRAME_TRY_ASSIGN(const bool kDecaling, made(!frame->decals.empty(), Effect::Decals));
        RAWFRAME_TRY(decalAtlas->declare(*frame, kDecaling, *textures, writes));
        now.block.decals = {decalAtlas->drawn() > 0 ? 1.0F : 0.0F, 0, 0, 0};
        RAWFRAME_TRY(occlusion->declare(*frame, kSurfaces && kOccluding, now.block, open.width, open.height, writes));
        // The reflections read the picture before, so only where it is
        // reused (D331).
        RAWFRAME_TRY(
            reflecting->declare(*frame,
                                kSurfaces && kReflecting,
                                now.block,
                                temporal->enabled() && temporal->reused() ? temporal->before() : mrhiResourceId{},
                                open.width,
                                open.height,
                                writes));
        now.block.reflections = {reflecting->enabled() ? 1.0F : 0.0F, 0, 0, 0};
        RAWFRAME_TRY(contact->declare(*frame, kContacting, now.block, open.width, open.height, writes));
        now.block.contact = {contact->enabled() ? 1.0F : 0.0F, 0, 0, 0};
        now.surfaced = occlusion->enabled() || reflecting->enabled();
        if (now.surfaced) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kSurfaceFormat;
            def.width = open.width;
            def.height = open.height;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, &now.surfaces);
                kDeclared != mrhi_success) {
                return failed("the prepass's surfaces could not be declared", kDeclared);
            }
        }
        RAWFRAME_TRY(motionBlur->declare(*frame, kBlurring, open.width, open.height, writes));
        RAWFRAME_TRY(focus->declare(*frame, kFocusing, open.width, open.height, writes));
        RAWFRAME_TRY(bloom->declare(*frame, kBlooming, open.width, open.height));
        RAWFRAME_TRY(picture->declare(
            *frame, kSmoothing, open.width, open.height, bloom->enabled() ? bloom->levels() : 0, writes));
        writes.push_back(wholeOf(now.slotsResource, mrhi_accessCopyDestination));
        writes.push_back(wholeOf(now.skyResource, mrhi_accessCopyDestination));
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
        // New decals' textures into the atlas, after their upload (D339).
        RAWFRAME_TRY(decalAtlas->addPasses());
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
        reads.push_back(wholeOf(decalAtlas->blocks(), mrhi_accessStorageRead));
        if (decalAtlas->drawn() > 0) {
            reads.push_back(wholeOf(decalAtlas->atlas(), mrhi_accessSampled));
        }
        reads.push_back(wholeOf(metering->exposure(), mrhi_accessStorageRead));
        for (const mrhiResourceId kLights :
             {now.lightsResource, now.rangesResource, now.indicesResource, now.slotsResource, now.probesResource}) {
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
        // With the ambient occlusion or the reflections, each point's
        // surface beside its depth (D327, D331).
        if (now.surfaced) {
            depthDef.colorTargets[0].resource = now.surfaces;
            depthDef.colorTargets[0].load = mrhi_loadClear;
            depthDef.colorTargets[0].store = mrhi_storeKeep;
            depthDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 1, .blue = 0, .alpha = 1};
            depthDef.colorTargetCount = 1;
        }
        depthDef.depthTarget = mrhiDepthTarget{.resource = now.depth,
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
        RAWFRAME_TRY(occlusion->addPasses(now.depth, now.surfaces));
        RAWFRAME_TRY(reflecting->addPasses(now.depth, now.surfaces));
        RAWFRAME_TRY(contact->addPasses(now.depth));
        // The models, then the sky where none lies, drawn with the exposure
        // the device holds (D293), reading what the occlusion found and
        // what the reflections met.
        std::vector<mrhiAccess> litReads = reads;
        if (occlusion->enabled()) {
            litReads.push_back(wholeOf(occlusion->reaching(), mrhi_accessSampled));
        }
        if (reflecting->enabled()) {
            litReads.push_back(wholeOf(reflecting->reflected(), mrhi_accessSampled));
        }
        if (contact->enabled()) {
            litReads.push_back(wholeOf(contact->lit(), mrhi_accessSampled));
        }
        mrhiPassDef litDef = depthDef;
        litDef.accesses = litReads.data();
        litDef.accessCount = static_cast<std::uint32_t>(litReads.size());
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
        RAWFRAME_TRY(capturing->declare(now.scene, open.width, open.height, now.block.exposure[0]));
        RAWFRAME_TRY(metering->addPasses(now.scene));
        RAWFRAME_TRY(temporal->addPass(now.scene, now.motion));
        // The post chain in ADR-0051's order: the temporal slot, the motion
        // blur, the depth of field, the bloom, then the picture.
        RAWFRAME_TRY(motionBlur->addPasses(temporal->shown(now.scene), now.motion, now.depth));
        RAWFRAME_TRY(focus->addPasses(motionBlur->shown(temporal->shown(now.scene)), now.depth));
        const mrhiResourceId kShown = focus->shown(motionBlur->shown(temporal->shown(now.scene)));
        RAWFRAME_TRY(bloom->addPasses(kShown));
        RAWFRAME_TRY(picture->addPasses(kShown,
                                        bloom->enabled() ? bloom->spread() : mrhiResourceId{},
                                        resourceOf(open.picture),
                                        open.clearsPicture()));
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
                            now.indices.size() * sizeof(std::uint32_t)) != mrhi_success ||
            mrhiWriteBuffer(native,
                            now.upload,
                            now.probesResource,
                            0,
                            now.reflections.blocks.data(),
                            now.reflections.blocks.size() * sizeof(ProbeBlock)) != mrhi_success) {
            return failed("the frame's lights could not be written", mrhi_errorCapacity);
        }
        if (mrhiWriteBuffer(native, now.upload, now.skyResource, 0, &now.sky, sizeof(SkyBlock)) != mrhi_success) {
            return failed("the sky's light could not be written", mrhi_errorCapacity);
        }
        RAWFRAME_TRY(picture->write(now.upload));
        RAWFRAME_TRY(metering->write(now.upload));
        RAWFRAME_TRY(temporal->write(now.upload));
        RAWFRAME_TRY(occlusion->write(now.upload));
        RAWFRAME_TRY(reflecting->write(now.upload));
        RAWFRAME_TRY(contact->write(now.upload));
        RAWFRAME_TRY(decalAtlas->write(now.upload));
        RAWFRAME_TRY(motionBlur->write(now.upload));
        RAWFRAME_TRY(focus->write(now.upload));
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
        RAWFRAME_TRY(decalAtlas->record(pipelines));
        RAWFRAME_TRY(castShadows(now));
        // The scene's table: slots 10 to 17 are each run's textures; 18 and
        // 19 the sky's picture (D322), or the run's probe's; 20 what each
        // reflects (D325); 21 what the ambient occlusion found, or white
        // (D327); 22 what the screen-space reflections met, or white
        // (D331); 23 what the contact shadows let through, or white (D338);
        // 24 and 25 the decals and their atlas, or white seen as an array
        // where none is drawn (D339).
        const mrhiBinding kPicture = cubeAt(18, resourceOf(textures->resource(now.environment)));
        const mrhiBinding kPictureSampler =
            samplerAt(19, pipelines.materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)]);
        const std::array<mrhiBinding, 26> kFrameBinding = {
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
            kPictureSampler,
            bufferAt(20, now.probesResource, now.reflections.blocks.size() * sizeof(ProbeBlock)),
            textureAt(21, occlusion->enabled() ? occlusion->reaching() : resourceOf(textures->resource(0))),
            textureAt(22, reflecting->enabled() ? reflecting->reflected() : resourceOf(textures->resource(0))),
            textureAt(23, contact->enabled() ? contact->lit() : resourceOf(textures->resource(0))),
            bufferAt(24, decalAtlas->blocks(), decalAtlas->blockBytes()),
            arrayAt(25, decalAtlas->drawn() > 0 ? decalAtlas->atlas() : resourceOf(textures->resource(0)))};
        std::array<mrhiBinding, 4> skyBinding = {bufferAt(0, now.skyResource, sizeof(SkyBlock)),
                                                 bufferAt(1, metering->exposure(), sizeof(ExposureBlock)),
                                                 kPicture,
                                                 kPictureSampler};
        skyBinding[2].slot = 2;
        skyBinding[3].slot = 3;
        // Runs of models drawn with `pipeline` in `pass`, the table set
        // again where a run samples another texture than the one before
        // (D309), white for none and for one not held this frame, or
        // reflects another probe (D325).
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
            std::array<mrhiBinding, 26> binding = kFrameBinding;
            // The prepass, which the occlusion, the reflections, and the
            // contact shadows come after, reads white.
            if (pass.index1 == now.depthPass.index1 && pass.generation == now.depthPass.generation) {
                binding[21] = textureAt(21, resourceOf(textures->resource(0)));
                binding[22] = textureAt(22, resourceOf(textures->resource(0)));
                binding[23] = textureAt(23, resourceOf(textures->resource(0)));
            }
            std::optional<std::pair<render_scene::SceneTextures, std::uint32_t>> bound;
            for (const Run& run : runs) {
                if (bound != std::pair{run.texture, run.probe}) {
                    bindTexture(*textures, pipelines, binding[10], binding[11], run.texture.base);
                    bindTexture(*textures, pipelines, binding[12], binding[13], run.texture.packed);
                    bindTexture(*textures, pipelines, binding[14], binding[15], run.texture.emission);
                    bindTexture(*textures, pipelines, binding[16], binding[17], run.texture.normal);
                    const std::vector<std::uint64_t>& kCubes = now.reflections.cubes;
                    binding[18].resource =
                        resourceOf(textures->resource(run.probe < kCubes.size() ? kCubes[run.probe] : kCubes[0]));
                    if (mrhiSetBindings(native, pass, 0, binding.data(), binding.size()) != mrhi_success) {
                        return failed("a material's texture could not be bound", mrhi_errorState);
                    }
                    bound = std::pair{run.texture, run.probe};
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
        // With the ambient occlusion or the reflections, the prepass
        // leaves each point's surface too, and they are found between the
        // two (D327, D331).
        // With decals to draw, the lit models' twins that lay them (D339).
        const bool kDecaled = decalAtlas->drawn() > 0;
        const bool kSurfaces = now.surfaced;
        for (const auto& [kPass, kPipeline, kLit] :
             {std::tuple{now.depthPass, (kSurfaces ? pipelines.surfaces : pipelines.depth).pipeline, false},
              std::tuple{now.litPass, (kDecaled ? pipelines.litDecaled : pipelines.lit).pipeline, true}}) {
            if (mrhiBeginPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not begin", mrhi_errorState);
            }
            RAWFRAME_TRY(kDrawRuns(kPass, kPipeline, now.placed.runs));
            const Asked& kMasked = kLit        ? (kDecaled ? pipelines.maskedLitDecaled : pipelines.maskedLit)
                                   : kSurfaces ? pipelines.cutSurfaces
                                               : pipelines.cutout;
            RAWFRAME_TRY(kDrawRuns(kPass, kMasked.pipeline, now.placed.maskedRuns));
            if (kLit && (mrhiSetGraphicsPipeline(native, kPass, pipelines.sky.pipeline) != mrhi_success ||
                         mrhiSetBindings(native, kPass, 0, skyBinding.data(), skyBinding.size()) != mrhi_success ||
                         mrhiDraw(native, kPass, 3, 1, 0, 0) != mrhi_success)) {
                return failed("the sky could not be drawn", mrhi_errorState);
            }
            if (kLit) {
                RAWFRAME_TRY(kDrawRuns(
                    kPass, (kDecaled ? pipelines.glassDecaled : pipelines.glass).pipeline, now.placed.translucentRuns));
            }
            if (mrhiEndPass(native, kPass) != mrhi_success) {
                return failed("a scene pass could not end", mrhi_errorState);
            }
            if (!kLit) {
                RAWFRAME_TRY(occlusion->record(pipelines, now.depth));
                RAWFRAME_TRY(reflecting->record(pipelines));
                RAWFRAME_TRY(contact->record(pipelines));
            }
        }
        RAWFRAME_TRY(capturing->record(now.scene));
        RAWFRAME_TRY(metering->record(pipelines, now.scene, now.width, now.height));
        RAWFRAME_TRY(temporal->record(pipelines, now.scene, now.motion));
        RAWFRAME_TRY(motionBlur->record(pipelines));
        RAWFRAME_TRY(focus->record(pipelines));
        RAWFRAME_TRY(bloom->record(pipelines, focus->shown(motionBlur->shown(temporal->shown(now.scene)))));
        return picture->record(pipelines);
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
            statistics.framesSmoothed += picture->smoothed() ? 1 : 0;
            statistics.framesOccluded += occlusion->enabled() ? 1 : 0;
            statistics.framesBloomed += bloom->enabled() ? 1 : 0;
            statistics.framesReflected += reflecting->enabled() ? 1 : 0;
            statistics.framesMotionBlurred += motionBlur->enabled() ? 1 : 0;
            statistics.framesFocused += focus->enabled() ? 1 : 0;
            statistics.framesContactShadowed += contact->enabled() ? 1 : 0;
            statistics.decalsDrawn += decalAtlas->drawn();
            if (temporal->enabled()) {
                ++statistics.framesResolved;
                statistics.historyReused += temporal->reused() ? 1 : 0;
            }
            statistics.models += declared->placed.instances.size() * sizeof(float) / kInstanceBytes;
            statistics.drawCalls += declared->placed.runs.size() + declared->placed.maskedRuns.size() +
                                    declared->placed.translucentRuns.size();
        }
        temporal->ended(submitted);
        decalAtlas->ended(submitted);
        if (submitted && metering->metered()) {
            ++statistics.framesMetered;
        }
        metering->ended(submitted);
        capturing->ended(submitted);
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
    state->capturing.emplace(device);
    state->occlusion.emplace(device.native());
    state->reflecting.emplace(device.native());
    state->contact.emplace(device.native());
    state->decalAtlas.emplace(device.native());
    RAWFRAME_TRY(state->decalAtlas->make());
    state->motionBlur.emplace(device.native());
    state->focus.emplace(device.native());
    state->bloom.emplace(device.native());
    state->picture.emplace(device.native());
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

void SceneRenderer::capture() noexcept {
    state_->capturing->ask();
}

std::optional<LightCapture> SceneRenderer::captured() {
    return state_->capturing->taken();
}

} // namespace rawframe::render_scene_gpu
