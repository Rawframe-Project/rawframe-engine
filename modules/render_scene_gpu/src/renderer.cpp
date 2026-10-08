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
#include "models.h"
#include "motion.h"
#include "occlusion.h"
#include "particles.h"
#include "picture.h"
#include "pipelines.h"
#include "post.h"
#include "probes.h"
#include "rawframe/particles_gpu/particles.h"
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
#include <set>
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
    /// Its shaders, samplers, and pipelines, shared with the renderers made
    /// to share them (D361).
    std::shared_ptr<Pipelines> pipelines;
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
    /// The shadow maps (D289, D292).
    std::optional<ShadowPasses> shadows;
    /// The models' passes and their targets (D284, D343).
    std::optional<ModelPasses> models;
    /// The decals' textures (D339).
    std::optional<DecalAtlas> decalAtlas;
    /// The reflection probes' pictures (D340).
    std::optional<ProbeAtlas> probeAtlas;
    /// Its motion blur, when a view asks (D334).
    std::optional<MotionBlurPass> motionBlur;
    /// Its depth of field, when a view asks (D336).
    std::optional<DepthOfFieldPass> focus;
    /// Its bloom, when a view asks (D328).
    std::optional<BloomPass> bloom;
    /// Its picture, graded and tonemapped, and antialiased by FXAA when a
    /// view asks (D294 to D296).
    std::optional<PicturePass> picture;
    /// Its post processes, where a view's camera has them (D350).
    std::optional<PostProcessPass> post;
    /// Its particles, trails, and beams, where a view draws any (D353,
    /// D354, D357).
    std::unique_ptr<particles_gpu::Particles> particles;
    /// What it draws over the composed picture, after the canvas (D351),
    /// and whether the open frame has that.
    struct Composed final : render::FrameRecorder {
        State* state = nullptr;
        result::Status declare(render::Frame& open) override {
            return state->declareComposed(open);
        }
        result::Status record(render::Frame& /*open*/) override {
            return state->recordComposed();
        }
        void ended(bool /*submitted*/) noexcept override {
        }
    };
    Composed composed;
    bool composing = false;
    std::optional<DeviceMeshes> held;
    /// The materials' textures (D309), and the white one a material
    /// sampling none samples, held as texture nought.
    std::unique_ptr<render::DeviceTextures> textures;
    std::shared_ptr<const texture::Texture> white;
    /// The sky's picture (D322): the black cube bound where there is
    /// none, the picture last seen, and its irradiance.
    std::shared_ptr<const texture::Texture> dark;
    /// The grading table that changes nothing, bound where a frame looks up
    /// none (D344).
    std::shared_ptr<const texture::Texture> plain;
    std::shared_ptr<const texture::Texture> environment;
    std::array<std::array<float, 4>, 9> irradiance{};
    /// The samples a pixel the device renders every target of the models'
    /// passes with, as Maul RHI's mask (D343).
    std::uint8_t sampleCounts = 0;
    /// The reflection probes' pictures this frame holds to draw into the
    /// atlas, each an environment (D325, D340).
    std::set<std::uint64_t> probePictures;
    /// What the next frame draws.
    const render_scene::SceneFrame* frame = nullptr;
    MeshSource meshes;
    TextureSource sampled;
    /// The render textures' views this one samples (D361).
    std::vector<TextureView*> views;

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
        static_cast<void>(textures->choose(kNoTable, plain));
        // The grading table, if the grade has one (D344).
        if (const std::uint64_t kTable = scene.grading.table; kTable != 0 && sampled) {
            static_cast<void>(textures->choose(kTable, sampled(kTable)));
        }
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
        // Each reflection probe's picture, if it is one and the atlas does
        // not hold it already (D325, D340).
        probePictures.clear();
        for (const render_scene::SceneProbe& kProbe : scene.probes) {
            if (probeAtlas->holds(kProbe.environment)) {
                continue;
            }
            const std::shared_ptr<const texture::Texture> kPicture = sampled ? sampled(kProbe.environment) : nullptr;
            if (kPicture != nullptr && isEnvironment(*kPicture)) {
                static_cast<void>(textures->choose(kProbe.environment, kPicture));
                probePictures.insert(kProbe.environment);
            }
        }
        // Each emitter's and ribbon's material's base and emission
        // textures (D353, D354).
        std::vector<std::uint32_t> shown;
        for (const rawframe::particles::EmitterDraw& kEmitter : scene.particles.emitters) {
            shown.push_back(kEmitter.material);
        }
        for (const rawframe::particles::Ribbon& kRibbon : scene.particles.ribbons) {
            shown.push_back(kRibbon.material);
        }
        for (const std::uint32_t kMaterial : shown) {
            if (kMaterial >= scene.textures.size()) {
                continue;
            }
            const render_scene::SceneTextures& kTextures = scene.textures[kMaterial];
            for (const std::uint64_t kId : {kTextures.base.id, kTextures.emission.id}) {
                if (kId != 0) {
                    static_cast<void>(textures->choose(kId, sampled ? sampled(kId) : nullptr));
                }
            }
        }
        // Each post process's texture (D350).
        for (const render_scene::ScenePostProcess& kProcess : scene.postProcesses) {
            if (kProcess.texture.id != 0) {
                static_cast<void>(
                    textures->choose(kProcess.texture.id, sampled ? sampled(kProcess.texture.id) : nullptr));
            }
        }
        // Each decal's texture (D339), and its normals' (D342).
        for (const render_scene::SceneDecal& kDecal : scene.decals) {
            static_cast<void>(textures->choose(kDecal.texture, sampled ? sampled(kDecal.texture) : nullptr));
            if (kDecal.normal != 0) {
                static_cast<void>(textures->choose(kDecal.normal, sampled ? sampled(kDecal.normal) : nullptr));
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

    /// What the shadow passes draw with this frame.
    [[nodiscard]] Casting castingOf(const Declared& now) const noexcept {
        return Casting{.native = native,
                       .pipelines = pipelines.get(),
                       .held = &*held,
                       .textures = textures.get(),
                       .instances = now.instances,
                       .materials = now.materialsResource,
                       .materialsBytes = now.materials.size() * sizeof(render_scene::MaterialBlob)};
    }

    /// What the open frame declared, until it is recorded and ends.
    struct Declared {
        Placed placed;
        FrameBlock block;
        mrhiResourceId instances{};
        mrhiResourceId blockResource{};
        mrhiPassId upload{};
        /// The light the bloom and the picture take (D328, D350).
        mrhiResourceId shown{};
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
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool draws = false;
        bool casters = false;
    };

    /// Whether an effect a frame `wants` can be drawn: its pipelines asked
    /// for the first time it is wanted, and made (D337).
    result::Result<bool> made(bool wants, Effect effect) {
        if (!wants) {
            return false;
        }
        return pipelines->wanted(effect);
    }

    result::Status declare(render::Frame& open) {
        declared.reset();
        if (frame == nullptr) {
            return {};
        }
        device->pump();
        RAWFRAME_TRY_ASSIGN(const bool kReady, pipelines->ready());
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
        // White's four bytes, the dark cube's 48, and the plain table's 64
        // are kept aside, so they are always there.
        const std::uint64_t kWhite = std::min<std::uint64_t>(kBudget, 4 + 48 + 64);
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
        // The render textures' pictures, drawn by the views before this one
        // (D361).
        for (const TextureView* kView : views) {
            if (const std::optional<std::uint64_t> kPicture = kView->picture()) {
                textures->lend(kView->id(), *kPicture);
            }
        }
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
        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes = {wholeOf(now.blockResource, mrhi_accessCopyDestination),
                                          wholeOf(now.lightsResource, mrhi_accessCopyDestination),
                                          wholeOf(now.materialsResource, mrhi_accessCopyDestination),
                                          wholeOf(now.rangesResource, mrhi_accessCopyDestination),
                                          wholeOf(now.indicesResource, mrhi_accessCopyDestination)};
        if (now.draws || now.casters) {
            writes.push_back(wholeOf(now.instances, mrhi_accessCopyDestination));
        }
        RAWFRAME_TRY(shadows->declare(*frame, now.block, writes));
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
        RAWFRAME_TRY_ASSIGN(const bool kProbing, made(!frame->probes.empty(), Effect::Probes));
        RAWFRAME_TRY(probeAtlas->declare(
            *frame,
            kProbing,
            *textures,
            [this](std::uint64_t id) {
                return probePictures.contains(id);
            },
            writes));
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
        // Multisampled where the view asks it, the device renders every
        // target so, and the pipelines, asked for the first count a view
        // asks, are made (D343).
        if (pipelines->samples == 0 && frame->samples > 1 && (sampleCounts & frame->samples) != 0) {
            pipelines->samples = frame->samples;
        }
        RAWFRAME_TRY_ASSIGN(const bool kSampling,
                            made(frame->samples > 1 && frame->samples == pipelines->samples, Effect::Multisampled));
        // With the ambient occlusion or the reflections, each point's
        // surface beside its depth (D327, D331).
        RAWFRAME_TRY(models->declare(
            open.width, open.height, kSampling ? frame->samples : 1, occlusion->enabled() || reflecting->enabled()));
        // Each material's own program's pipelines, as this frame draws its
        // models; until they are made, the engine's own draw them (D485,
        // D487). A model is drawn by its program when every pass it is
        // drawn in that runs a material's code has its program's pipeline.
        const bool kDecaled = decalAtlas->drawn() > 0;
        const bool kSurfaced = occlusion->enabled() || reflecting->enabled();
        pipelines->wantPrograms(frame->programs, kDecaled, kSampling);
        pipelines->tellRefused(statistics);
        for (const Runs* kRuns : {&now.placed.runs, &now.placed.maskedRuns, &now.placed.translucentRuns}) {
            const bool kOpaque = kRuns == &now.placed.runs;
            const bool kMasked = kRuns == &now.placed.maskedRuns;
            const Shade kShade = kOpaque ? Shade::Lit : kMasked ? Shade::Masked : Shade::Glass;
            // The prepass's pipeline running the material's code, if any.
            const std::optional<Shade> kPrepass = kMasked ? std::optional{kSurfaced ? Shade::CutSurfaces : Shade::Cut}
                                                  : kOpaque && kSurfaced ? std::optional{Shade::Surfaces}
                                                                         : std::nullopt;
            for (const Run& kRun : *kRuns) {
                if (kRun.program == nullptr) {
                    continue;
                }
                // A masked model casting shadows is cut by its program there
                // too (D488).
                const bool kOwn =
                    pipelines->programPipeline(kRun.program, kShade, kDecaled, kSampling) != nullptr &&
                    (!kPrepass.has_value() ||
                     pipelines->programPipeline(kRun.program, *kPrepass, false, kSampling) != nullptr) &&
                    (!kMasked || frame->shadows.count == 0 ||
                     pipelines->programPipeline(kRun.program, Shade::CutCasting, false, false) != nullptr);
                (kOwn ? statistics.programModelsDrawn : statistics.programModelsWaiting) += kRun.count;
            }
        }
        RAWFRAME_TRY(motionBlur->declare(*frame, kBlurring, open.width, open.height, writes));
        RAWFRAME_TRY(focus->declare(*frame, kFocusing, open.width, open.height, writes));
        RAWFRAME_TRY(bloom->declare(*frame, kBlooming, open.width, open.height));
        RAWFRAME_TRY_ASSIGN(const bool kPosting, made(!frame->postProcesses.empty(), Effect::PostProcess));
        RAWFRAME_TRY(post->declare(*frame, kPosting, *textures, writes));
        const std::uint64_t kTable = frame->grading.table;
        const bool kTabled = kTable != 0 && textures->volume(kTable) && textures->resource(kTable) != 0;
        RAWFRAME_TRY(picture->declare(*frame,
                                      kSmoothing,
                                      open.width,
                                      open.height,
                                      bloom->enabled() ? bloom->levels() : 0,
                                      resourceOf(textures->resource(kTabled ? kTable : kNoTable)),
                                      kTabled,
                                      post->at(material::Insertion::BeforeTonemap) > 0,
                                      writes));
        writes.push_back(wholeOf(now.skyResource, mrhi_accessCopyDestination));
        RAWFRAME_TRY(metering->declare(*frame, writes));
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
        // New probes' pictures into the atlas (D340).
        RAWFRAME_TRY(probeAtlas->addPasses());
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
        RAWFRAME_TRY(shadows->addPasses(meshReads));
        // The models' passes read the shadow map too: the scene's table
        // holds it for both.
        std::vector<mrhiAccess> reads = meshReads;
        reads.push_back(wholeOf(now.blockResource, mrhi_accessUniform));
        reads.push_back(wholeOf(now.skyResource, mrhi_accessUniform));
        reads.push_back(wholeOf(decalAtlas->blocks(), mrhi_accessStorageRead));
        if (decalAtlas->drawn() > 0) {
            reads.push_back(wholeOf(decalAtlas->colors(), mrhi_accessSampled));
        }
        if (decalAtlas->bent() > 0) {
            reads.push_back(wholeOf(decalAtlas->normals(), mrhi_accessSampled));
        }
        reads.push_back(wholeOf(probeAtlas->blocks(), mrhi_accessStorageRead));
        if (probeAtlas->drawn() > 0) {
            reads.push_back(wholeOf(probeAtlas->atlas(), mrhi_accessSampled));
        }
        reads.push_back(wholeOf(metering->exposure(), mrhi_accessStorageRead));
        for (const mrhiResourceId kLights : {now.lightsResource, now.rangesResource, now.indicesResource}) {
            reads.push_back(wholeOf(kLights, mrhi_accessStorageRead));
        }
        shadows->readBy(reads);
        RAWFRAME_TRY(models->addPrepass(reads));
        RAWFRAME_TRY(occlusion->addPasses(models->depth(), models->surfaces()));
        RAWFRAME_TRY(reflecting->addPasses(models->depth(), models->surfaces()));
        RAWFRAME_TRY(contact->addPasses(models->depth()));
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
        RAWFRAME_TRY(models->addLitPass(litReads));
        // The trails and beams, then the particles, over the models' light,
        // before anything reads it (D353, D354), through the device half the
        // canvas's share (D357).
        const auto kKey = [](mrhiResourceId resource) {
            return render::requestKey(resource.index1, resource.generation);
        };
        RAWFRAME_TRY(particles->declare(frame->particles,
                                        particleMaterialsOf(now.materials, frame->textures, *textures, *pipelines),
                                        particleViewOf(*frame, now.block),
                                        {.picture = kKey(models->scene()),
                                         .exposure = kKey(metering->exposure()),
                                         .depth = kKey(models->depth())}));
        RAWFRAME_TRY(capturing->declare(models->scene(), open.width, open.height, now.block.exposure[0]));
        RAWFRAME_TRY(metering->addPasses(models->scene()));
        RAWFRAME_TRY(temporal->addPass(models->scene(), models->motion()));
        // The post chain in ADR-0051's order: the temporal slot, the post
        // processes after it, the motion blur, the depth of field, the
        // bloom, then the picture (D350).
        RAWFRAME_TRY_ASSIGN(const mrhiResourceId kSteady,
                            post->addStage(material::Insertion::AfterTemporal,
                                           temporal->shown(models->scene()),
                                           open.width,
                                           open.height,
                                           {},
                                           false));
        RAWFRAME_TRY(motionBlur->addPasses(kSteady, models->motion(), models->depth()));
        RAWFRAME_TRY(focus->addPasses(motionBlur->shown(kSteady), models->depth()));
        now.shown = focus->shown(motionBlur->shown(kSteady));
        RAWFRAME_TRY(bloom->addPasses(now.shown));
        RAWFRAME_TRY(addPicture(open, now.shown));
        declared = std::move(now);
        return {};
    }

    /// The picture from the light `shown` (D350): graded, the post
    /// processes before the tonemapper, tonemapped, those after it, FXAA,
    /// and those over the scene's output, each into a picture between
    /// steps but the last, which writes the frame's.
    result::Status addPicture(render::Frame& open, mrhiResourceId shown) {
        const mrhiResourceId kSpread = bloom->enabled() ? bloom->spread() : mrhiResourceId{};
        RAWFRAME_TRY_ASSIGN(const mrhiResourceId kGraded, picture->addGrade(shown, kSpread));
        RAWFRAME_TRY_ASSIGN(
            const mrhiResourceId kLight,
            post->addStage(material::Insertion::BeforeTonemap, kGraded, open.width, open.height, {}, false));
        const mrhiResourceId kPicture = resourceOf(open.picture);
        const bool kClears = open.clearsPicture();
        const std::size_t kAfter = post->at(material::Insertion::AfterTonemap);
        const std::size_t kOutput = post->at(material::Insertion::SceneOutput);
        const bool kSmoothed = picture->smoothed();
        // The frame's picture for the last step, a picture of its own for
        // another.
        const auto kInto = [&](bool last) -> result::Result<mrhiResourceId> {
            if (last) {
                return kPicture;
            }
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = kPictureFormat;
            def.width = open.width;
            def.height = open.height;
            mrhiResourceId made{};
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, &made); kDeclared != mrhi_success) {
                return failed("a picture between steps could not be declared", kDeclared);
            }
            return made;
        };
        RAWFRAME_TRY_ASSIGN(const mrhiResourceId kToned, kInto(kAfter == 0 && !kSmoothed && kOutput == 0));
        RAWFRAME_TRY(picture->addTonemap(kLight, kSpread, kToned, kClears));
        RAWFRAME_TRY_ASSIGN(mrhiResourceId done,
                            post->addStage(material::Insertion::AfterTonemap,
                                           kToned,
                                           open.width,
                                           open.height,
                                           kSmoothed || kOutput > 0 ? mrhiResourceId{} : kPicture,
                                           kClears));
        if (kSmoothed) {
            RAWFRAME_TRY_ASSIGN(const mrhiResourceId kSmooth, kInto(kOutput == 0));
            RAWFRAME_TRY(picture->addFxaa(done, kSmooth, kClears));
            done = kSmooth;
        }
        RAWFRAME_TRY(
            post->addStage(material::Insertion::SceneOutput, done, open.width, open.height, kPicture, kClears));
        return {};
    }

    /// The post processes over the composed picture: each into a picture
    /// of its own, the first reading the frame's, and the last copied back
    /// into it (D351).
    result::Status declareComposed(render::Frame& open) {
        composing = false;
        if (!declared.has_value() || post->at(material::Insertion::FinalOutput) == 0) {
            return {};
        }
        const mrhiResourceId kPicture = resourceOf(open.picture);
        RAWFRAME_TRY_ASSIGN(
            const mrhiResourceId kLast,
            post->addStage(material::Insertion::FinalOutput, kPicture, open.width, open.height, {}, false));
        RAWFRAME_TRY(post->addCopy(kLast, kPicture));
        composing = true;
        return {};
    }

    result::Status recordComposed() {
        if (!composing) {
            return {};
        }
        return post->record(*pipelines, material::Insertion::FinalOutput);
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
        RAWFRAME_TRY(probeAtlas->write(now.upload));
        RAWFRAME_TRY(motionBlur->write(now.upload));
        RAWFRAME_TRY(focus->write(now.upload));
        RAWFRAME_TRY(post->write(now.upload));
        RAWFRAME_TRY(shadows->write(now.upload));
        RAWFRAME_TRY(held->write(now.upload));
        RAWFRAME_TRY(textures->write(render::requestKey(now.upload.index1, now.upload.generation)));
        if (mrhiEndPass(native, now.upload) != mrhi_success) {
            return failed("the upload pass could not end", mrhi_errorState);
        }
        RAWFRAME_TRY(decalAtlas->record(*pipelines));
        RAWFRAME_TRY(probeAtlas->record(*pipelines));
        RAWFRAME_TRY(shadows->record(castingOf(now), now.placed));
        // The scene's table: slots 10 to 17 are each run's textures; 18 and
        // 19 the sky's picture (D322); 20 the reflection probes (D325); 21
        // what the ambient occlusion found, or white (D327); 22 what the
        // screen-space reflections met, or white (D331); 23 what the contact
        // shadows let through, or white (D338); 24 and 25 the decals and
        // their atlas, or white seen as an array where none is drawn
        // (D339); 26 the probes' atlas, or the dark cube seen as an array
        // where none is drawn (D340); 27 the decals' normals' atlas, or
        // white seen as an array where none bends the normals (D342).
        const mrhiBinding kPicture = cubeAt(18, resourceOf(textures->resource(now.environment)));
        const mrhiBinding kPictureSampler =
            samplerAt(19, pipelines->materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)]);
        const std::array<mrhiBinding, kTableSlots> kFrameBinding = {
            bufferAt(0, now.blockResource, sizeof(FrameBlock)),
            depthAt(1, shadows->sunMap()),
            samplerAt(2, pipelines->shadowSampler),
            bufferAt(3, now.lightsResource, now.lights.size() * sizeof(LightBlock)),
            bufferAt(4, now.rangesResource, now.ranges.size() * 4),
            bufferAt(5, now.indicesResource, now.indices.size() * 4),
            depthAt(6, shadows->lightMap()),
            bufferAt(7, shadows->squares(), shadows->squareBytes()),
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
            bufferAt(20, probeAtlas->blocks(), probeAtlas->blockBytes()),
            textureAt(21, occlusion->enabled() ? occlusion->reaching() : resourceOf(textures->resource(0))),
            textureAt(22, reflecting->enabled() ? reflecting->reflected() : resourceOf(textures->resource(0))),
            textureAt(23, contact->enabled() ? contact->lit() : resourceOf(textures->resource(0))),
            bufferAt(24, decalAtlas->blocks(), decalAtlas->blockBytes()),
            arrayAt(25, decalAtlas->drawn() > 0 ? decalAtlas->colors() : resourceOf(textures->resource(0))),
            cubesAt(26, probeAtlas->drawn() > 0 ? probeAtlas->atlas() : resourceOf(textures->resource(kNoEnvironment))),
            arrayAt(27, decalAtlas->bent() > 0 ? decalAtlas->normals() : resourceOf(textures->resource(0)))};
        std::array<mrhiBinding, 4> skyBinding = {bufferAt(0, now.skyResource, sizeof(SkyBlock)),
                                                 bufferAt(1, metering->exposure(), sizeof(ExposureBlock)),
                                                 kPicture,
                                                 kPictureSampler};
        skyBinding[2].slot = 2;
        skyBinding[3].slot = 3;
        // The opaque models, their depth first, the masked cut there
        // (D310); then, lit, the sky where none lies; then the translucent
        // over both (D305). The occlusion, the reflections, and the contact
        // shadows are found between the two (D327, D331, D338).
        const Drawing kDrawing{.pipelines = pipelines.get(),
                               .held = &*held,
                               .textures = textures.get(),
                               .instances = now.instances,
                               .placed = &now.placed,
                               .table = kFrameBinding};
        RAWFRAME_TRY(models->recordPrepass(kDrawing));
        RAWFRAME_TRY(occlusion->record(*pipelines, models->depth()));
        RAWFRAME_TRY(reflecting->record(*pipelines));
        RAWFRAME_TRY(contact->record(*pipelines));
        RAWFRAME_TRY(models->recordLit(kDrawing, decalAtlas->drawn() > 0, skyBinding));
        RAWFRAME_TRY(particles->record());
        RAWFRAME_TRY(capturing->record(models->scene()));
        RAWFRAME_TRY(metering->record(*pipelines, models->scene(), now.width, now.height));
        RAWFRAME_TRY(temporal->record(*pipelines, models->scene(), models->motion()));
        RAWFRAME_TRY(post->record(*pipelines, material::Insertion::AfterTemporal));
        RAWFRAME_TRY(motionBlur->record(*pipelines));
        RAWFRAME_TRY(focus->record(*pipelines));
        RAWFRAME_TRY(bloom->record(*pipelines, now.shown));
        RAWFRAME_TRY(picture->recordGrade(*pipelines));
        RAWFRAME_TRY(post->record(*pipelines, material::Insertion::BeforeTonemap));
        RAWFRAME_TRY(picture->recordTonemap(*pipelines));
        RAWFRAME_TRY(post->record(*pipelines, material::Insertion::AfterTonemap));
        RAWFRAME_TRY(picture->recordFxaa(*pipelines));
        return post->record(*pipelines, material::Insertion::SceneOutput);
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
            statistics.framesMultisampled += models->samples() > 1 ? 1 : 0;
            statistics.probesDrawn += probeAtlas->drawn();
            statistics.postProcessesRun += post->run();
            statistics.postProcessesLeftOut += post->leftOut();
            statistics.emittersDrawn += particles->emittersDrawn();
            statistics.emittersLeftOut += particles->emittersLeftOut();
            statistics.particlesSpawned += particles->spawned();
            statistics.ribbonsDrawn += particles->ribbonsDrawn();
            if (temporal->enabled()) {
                ++statistics.framesResolved;
                statistics.historyReused += temporal->reused() ? 1 : 0;
            }
            statistics.models += declared->placed.instances.size() * sizeof(float) / kInstanceBytes;
            statistics.drawCalls += declared->placed.runs.size() + declared->placed.maskedRuns.size() +
                                    declared->placed.translucentRuns.size();
        }
        temporal->ended(submitted);
        particles->ended(submitted);
        decalAtlas->ended(submitted);
        probeAtlas->ended(submitted);
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
    return made(device, limits, nullptr);
}

result::Result<std::unique_ptr<SceneRenderer>>
SceneRenderer::create(render::Device& device, const SceneRenderer& sharing, RendererLimits limits) {
    return made(device, limits, &sharing);
}

result::Result<std::unique_ptr<SceneRenderer>>
SceneRenderer::made(render::Device& device, RendererLimits limits, const SceneRenderer* sharing) {
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
                        render::DeviceTextures::create(device, {.maximumTextures = limits.maximumTextures + 3}));
    texture::Texture white{.format = texture::Format::Rgba8Srgb};
    white.levels.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{0xFF})});
    state->white = std::make_shared<const texture::Texture>(std::move(white));
    state->dark = darkCube();
    state->plain = plainTable();
    if (sharing != nullptr) {
        state->pipelines = sharing->state_->pipelines;
    } else {
        state->pipelines = std::make_shared<Pipelines>();
        state->pipelines->device = &device;
        state->pipelines->native = device.native();
        RAWFRAME_TRY(state->pipelines->make());
    }
    state->metering.emplace(device.native());
    state->temporal.emplace(device.native());
    state->capturing.emplace(device);
    state->occlusion.emplace(device.native());
    state->reflecting.emplace(device.native());
    state->contact.emplace(device.native());
    state->sampleCounts = 0xFF;
    for (const mrhiFormat kFormat : {kSceneFormat, kMotionFormat, kDepthFormat, kSurfaceFormat}) {
        state->sampleCounts &= device.sampleCounts(static_cast<std::uint32_t>(kFormat));
    }
    state->shadows.emplace(device.native());
    state->models.emplace(device.native());
    state->decalAtlas.emplace(device.native());
    RAWFRAME_TRY(state->decalAtlas->make());
    state->probeAtlas.emplace(device.native());
    RAWFRAME_TRY(state->probeAtlas->make());
    state->motionBlur.emplace(device.native());
    state->focus.emplace(device.native());
    state->bloom.emplace(device.native());
    state->picture.emplace(device.native());
    state->post.emplace(device.native());
    if (sharing != nullptr) {
        RAWFRAME_TRY_ASSIGN(
            state->particles,
            particles_gpu::Particles::create(device, *sharing->state_->particles, limits.maximumParticles));
    } else {
        RAWFRAME_TRY_ASSIGN(
            state->particles,
            particles_gpu::Particles::create(device, particles_gpu::Target::Light, limits.maximumParticles));
    }
    state->composed.state = state.get();
    RAWFRAME_TRY(state->metering->make());
    return std::unique_ptr<SceneRenderer>{new SceneRenderer{std::move(state)}};
}

void SceneRenderer::prepare(const render_scene::SceneFrame* frame,
                            MeshSource meshes,
                            TextureSource textures,
                            std::span<TextureView* const> views) {
    state_->frame = frame;
    state_->meshes = std::move(meshes);
    state_->sampled = std::move(textures);
    state_->views.assign(views.begin(), views.end());
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

render::FrameRecorder& SceneRenderer::composed() noexcept {
    return state_->composed;
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
