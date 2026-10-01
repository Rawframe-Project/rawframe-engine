#include "rawframe/particles_gpu/particles.h"

#include "generated/particle_container.h"
#include "generated/spawn_container.h"
#include "rawframe/particles_gpu/errors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rawframe::particles_gpu {

namespace {

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kParticlesGpuDomain, code(ParticlesGpuError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t bytes) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = resource,
                       .offset = 0,
                       .size = bytes,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = {}};
}

mrhiBinding depthAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    return mrhiBinding{
        .slot = slot,
        .resource = resource,
        .offset = 0,
        .size = 0,
        .viewKind = mrhi_texture2d,
        .viewFormat = mrhi_formatNone,
        .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = mrhi_aspectDepthOnly},
        .sampler = {}};
}

mrhiBinding textureAt(std::uint32_t slot) noexcept {
    return mrhiBinding{
        .slot = slot,
        .resource = {},
        .offset = 0,
        .size = 0,
        .viewKind = mrhi_texture2d,
        .viewFormat = mrhi_formatNone,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
        .sampler = {}};
}

mrhiBinding samplerAt(std::uint32_t slot) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = {},
                       .offset = 0,
                       .size = 0,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = {}};
}

/// The births' workgroup, as spawn.comp declares it.
constexpr std::uint32_t kSpawnGroup = 64;

/// The stride between emitters', ribbons', or materials' blocks: no device
/// asks uniform offsets aligned past 256 bytes (Vulkan's and WebGPU's
/// bound).
constexpr std::uint64_t kBlockStride = 256;

/// The bytes one particle takes in the pool (D353): where it started and
/// when it was born, its velocity and life, and the scale on its size.
constexpr std::uint64_t kParticleBytes = 48;

/// An emitter as the shaders read it (std140, D353): its anchor relative
/// to the eye; where it spawns relative to its anchor, and the sphere's
/// radius; its way, and the cosine of the cone's half angle; its
/// particles' speed, life, drag, and variation; their acceleration, and
/// when on the particle clock the frame began; their size at birth and
/// death, the step between births, and the clock now; their color at birth
/// and death; its ring's first slot in the pool, its size, where this
/// spawn starts in it, and how many it spawns; and how many of them are
/// steady, its seed, and its material's place.
struct EmitterBlock {
    std::array<float, 4> anchor{};
    std::array<float, 4> origin{};
    std::array<float, 4> direction{};
    std::array<float, 4> motion{};
    std::array<float, 4> acceleration{};
    std::array<float, 4> sizes{};
    std::array<float, 4> colorStart{};
    std::array<float, 4> colorEnd{};
    std::array<std::uint32_t, 4> ring{};
    std::array<std::uint32_t, 4> more{};
};
static_assert(sizeof(EmitterBlock) == 160, "the particles' shaders read an emitter as 160 bytes");

/// A ribbon's point as the ribbons' shader reads it (std430, D354): where
/// it is relative to the eye and its width; its color; and its texture's
/// coordinate along the ribbon.
constexpr std::uint64_t kPointBytes = 48;

/// An emitter as its shaders read it, its ring at `offset`.
EmitterBlock blockOf(const particles::EmitterDraw& emitter, std::uint32_t offset, float now) noexcept {
    return EmitterBlock{
        .anchor = {emitter.anchor[0], emitter.anchor[1], emitter.anchor[2], 0},
        .origin = {emitter.origin[0], emitter.origin[1], emitter.origin[2], emitter.radius},
        .direction = {emitter.direction[0], emitter.direction[1], emitter.direction[2], std::cos(emitter.spread)},
        .motion = {emitter.speed, emitter.lifetime, emitter.drag, emitter.variation},
        .acceleration = {emitter.acceleration[0], emitter.acceleration[1], emitter.acceleration[2], emitter.born},
        .sizes = {emitter.sizeStart, emitter.sizeEnd, emitter.step, now},
        .colorStart = emitter.colorStart,
        .colorEnd = emitter.colorEnd,
        .ring = {offset, emitter.capacity, emitter.first, emitter.spawned},
        .more = {emitter.steady, emitter.seed, emitter.material, 0}};
}

/// A resource or sampler of the open frame from the key the host names it
/// by.
mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

mrhiSamplerId samplerOf(std::uint64_t key) noexcept {
    return mrhiSamplerId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                         .generation = static_cast<std::uint32_t>(key)};
}

/// A pipeline asked of the device: the request's key until it is answered,
/// and whether it was asked for and made.
struct Asked {
    std::uint64_t request = 0;
    bool asked = false;
    bool made = false;
};

/// A ring of the pool: its first slot and its size, and the ring of its
/// emitter it holds.
struct Ring {
    std::uint32_t offset = 0;
    std::uint32_t capacity = 0;
    std::uint32_t generation = 0;
};

/// An emitter the open frame draws: its ring, what it spawns, and its
/// material's place.
struct Drawn {
    std::uint32_t offset = 0;
    std::uint32_t capacity = 0;
    std::uint32_t spawned = 0;
    bool fresh = false;
    std::uint32_t material = 0;
};

} // namespace

struct Particles::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    Target target = Target::Light;
    std::uint32_t capacity = 0;
    /// The births' and the drawing's shaders, and their pipelines: the
    /// clearing, the births, and the particles' and the ribbons' by blend.
    mrhiShaderId spawnShader{};
    mrhiShaderId drawShader{};
    mrhiComputePipelineId clear{};
    mrhiComputePipelineId spawn{};
    std::array<mrhiGraphicsPipelineId, kBlends> particles{};
    std::array<mrhiGraphicsPipelineId, kBlends> ribbons{};
    Asked clearAsked;
    Asked spawnAsked;
    std::array<Asked, kBlends> particlesAsked{};
    std::array<Asked, kBlends> ribbonsAsked{};
    mrhiBufferId buffer{};
    /// Each drawn emitter's ring, by its key.
    std::map<std::uint64_t, Ring> rings;
    /// The open frame's: whether it draws particles, and ribbons; its
    /// emitters' blocks, at a stride every device's uniform offsets allow;
    /// the rings it clears, by key; its ribbons' points and blocks; its
    /// materials; its view; and what it declared.
    bool emitting = false;
    bool ribboned = false;
    std::vector<std::uint8_t> blocks;
    std::vector<Drawn> drawing;
    std::vector<std::uint64_t> cleared;
    std::vector<std::array<float, 12>> points;
    std::vector<std::uint8_t> ribbonBlocks;
    std::vector<std::uint32_t> ribbonCounts;
    std::vector<std::uint32_t> ribbonMaterials;
    std::vector<Material> materials;
    std::vector<std::uint8_t> materialBlocks;
    ViewBlock view;
    std::size_t leftOut = 0;
    std::uint64_t spawned = 0;
    Drawing with;
    mrhiResourceId pool{};
    mrhiResourceId blocksResource{};
    mrhiResourceId pointsResource{};
    mrhiResourceId ribbonsResource{};
    mrhiResourceId materialsResource{};
    mrhiResourceId viewResource{};
    mrhiResourceId depth{};
    mrhiResourceId exposure{};
    std::optional<mrhiPassId> uploadPass;
    std::optional<mrhiPassId> clearPass;
    std::optional<mrhiPassId> spawnPass;
    std::optional<mrhiPassId> depthPass;
    std::optional<mrhiPassId> drawPass;

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        if (buffer.index1 != 0) {
            static_cast<void>(mrhiDestroyBuffer(native, buffer));
        }
        for (const auto& kPipelines : {particles, ribbons}) {
            for (const mrhiGraphicsPipelineId kPipeline : kPipelines) {
                static_cast<void>(mrhiDestroyGraphicsPipeline(native, kPipeline));
            }
        }
        for (const mrhiComputePipelineId kPipeline : {clear, spawn}) {
            static_cast<void>(mrhiDestroyComputePipeline(native, kPipeline));
        }
        for (const mrhiShaderId kShader : {spawnShader, drawShader}) {
            static_cast<void>(mrhiDestroyShader(native, kShader));
        }
    }

    /// The picture's format.
    [[nodiscard]] mrhiFormat format() const noexcept {
        return target == Target::Light ? mrhi_formatRgba16Float : mrhi_formatRgba8UnormSrgb;
    }

    result::Status makeShaders() {
        for (const auto& [kContainer, kShader] :
             {std::pair{std::span<const std::uint8_t>{kSpawnContainer}, &spawnShader},
              std::pair{std::span<const std::uint8_t>{kParticleContainer}, &drawShader}}) {
            mrhiShaderDef def = mrhiDefaultShaderDef();
            def.bytes = kContainer.data();
            def.byteCount = kContainer.size();
            if (const mrhiResult kMade = mrhiCreateShader(native, &def, kShader); kMade != mrhi_success) {
                return failed("a particles' shader could not be made", kMade);
            }
        }
        return {};
    }

    /// The clearing's or the births' pipeline asked for.
    result::Status askBirths(bool clearing) {
        constexpr std::string_view kSpawn = "spawn";
        constexpr std::string_view kClear = "clear";
        mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
        def.shader = spawnShader;
        def.entry = clearing ? kClear.data() : kSpawn.data();
        def.entryLength = clearing ? kClear.size() : kSpawn.size();
        Asked& asked = clearing ? clearAsked : spawnAsked;
        mrhiRequestId request{};
        if (const mrhiResult kMade = mrhiCreateComputePipeline(native, &def, clearing ? &clear : &spawn, &request);
            kMade != mrhi_success) {
            return failed("the particles' births could not be asked for", kMade);
        }
        asked = Asked{.request = render::requestKey(request.index1, request.generation), .asked = true};
        return {};
    }

    /// The particles' or the ribbons' pipeline for `blend` asked for:
    /// premultiplied, over what is behind, its coverage hiding it; added to
    /// it; or multiplying it; the last two leaving its alpha as it was.
    result::Status askDrawing(Blend blend, bool ribbon) {
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        const std::string_view kLabel = target == Target::Light
                                            ? (ribbon ? "rawframe.scene.ribbons" : "rawframe.scene.particles")
                                            : (ribbon ? "rawframe.canvas.ribbons" : "rawframe.canvas.particles");
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = drawShader;
        constexpr std::string_view kParticles = "vs";
        constexpr std::string_view kRibbons = "ribbon";
        constexpr std::string_view kFragments = "fs";
        def.vertexEntry = ribbon ? kRibbons.data() : kParticles.data();
        def.vertexEntryLength = ribbon ? kRibbons.size() : kParticles.size();
        def.fragmentEntry = kFragments.data();
        def.fragmentEntryLength = kFragments.size();
        def.cullMode = mrhi_cullNone;
        def.colorTargetCount = 1;
        def.colorTargets[0].format = format();
        def.colorTargets[0].blend = true;
        switch (blend) {
        case Blend::Over:
            def.colorTargets[0].color = {
                .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
            def.colorTargets[0].alpha = {
                .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
            break;
        case Blend::Add:
            def.colorTargets[0].color = {
                .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd};
            def.colorTargets[0].alpha = {
                .srcFactor = mrhi_blendZero, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd};
            break;
        case Blend::Multiply:
            def.colorTargets[0].color = {
                .srcFactor = mrhi_blendDst, .dstFactor = mrhi_blendZero, .operation = mrhi_blendAdd};
            def.colorTargets[0].alpha = {
                .srcFactor = mrhi_blendZero, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd};
            break;
        }
        const auto kAt = static_cast<std::size_t>(blend);
        Asked& asked = ribbon ? ribbonsAsked.at(kAt) : particlesAsked.at(kAt);
        mrhiRequestId request{};
        if (const mrhiResult kMade =
                mrhiCreateGraphicsPipeline(native, &def, ribbon ? &ribbons.at(kAt) : &particles.at(kAt), &request);
            kMade != mrhi_success) {
            return failed("a particles' pipeline could not be asked for", kMade);
        }
        asked = Asked{.request = render::requestKey(request.index1, request.generation), .asked = true};
        return {};
    }

    /// Whether a pipeline asked for is made, looking for its answer.
    result::Result<bool> answered(Asked& asked) {
        if (asked.made || !asked.asked) {
            return asked.made;
        }
        if (const auto kAnswer = device->answer(asked.request)) {
            if (!kAnswer->has_value()) {
                return std::unexpected<result::Error>{kAnswer->error().clone()};
            }
            asked.made = true;
        }
        return asked.made;
    }

    /// Whether every pipeline the frame draws with is made: asked for the
    /// first time one is wanted, the births' with the first of any.
    result::Result<bool> ready(const std::array<bool, kBlends>& wanted) {
        if (!clearAsked.asked) {
            RAWFRAME_TRY(makeShaders());
            RAWFRAME_TRY(askBirths(true));
            RAWFRAME_TRY(askBirths(false));
        }
        bool all = true;
        for (std::size_t blend = 0; blend < kBlends; ++blend) {
            if (!wanted.at(blend)) {
                continue;
            }
            for (const bool kRibbon : {false, true}) {
                if (!(kRibbon ? ribbonsAsked : particlesAsked).at(blend).asked) {
                    RAWFRAME_TRY(askDrawing(static_cast<Blend>(blend), kRibbon));
                }
                RAWFRAME_TRY_ASSIGN(const bool kMade, answered((kRibbon ? ribbonsAsked : particlesAsked).at(blend)));
                all = all && kMade;
            }
        }
        for (Asked* asked : {&clearAsked, &spawnAsked}) {
            RAWFRAME_TRY_ASSIGN(const bool kMade, answered(*asked));
            all = all && kMade;
        }
        return all;
    }

    /// Where the pool has room for `slots`, the first that does.
    [[nodiscard]] std::optional<std::uint32_t> roomFor(std::uint32_t slots) const {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> taken;
        taken.reserve(rings.size());
        for (const auto& [kKey, kRing] : rings) {
            taken.emplace_back(kRing.offset, kRing.capacity);
        }
        std::ranges::sort(taken);
        std::uint32_t free = 0;
        for (const auto& [kOffset, kCapacity] : taken) {
            if (kOffset - free >= slots) {
                return free;
            }
            free = kOffset + kCapacity;
        }
        if (capacity - free >= slots) {
            return free;
        }
        return std::nullopt;
    }

    /// Where a material's place puts it among the open frame's.
    [[nodiscard]] std::uint32_t placeOf(std::uint32_t material) const noexcept {
        return material < materials.size() ? material : 0;
    }

    result::Status declareEmitters(const particles::Frame& frame) {
        if (frame.emitters.empty()) {
            return {};
        }
        if (buffer.index1 == 0) {
            mrhiBufferDef def = mrhiDefaultBufferDef();
            def.size = std::uint64_t{capacity} * kParticleBytes;
            def.usage = mrhi_bufferStorage;
            if (const mrhiResult kMade = mrhiCreateBuffer(native, &def, &buffer); kMade != mrhi_success) {
                return failed("the particles' pool could not be made", kMade);
            }
        }
        // Rings let go: their emitter not drawn, or its ring started anew
        // or changed its size.
        std::map<std::uint64_t, const particles::EmitterDraw*> wanted;
        for (const particles::EmitterDraw& kEmitter : frame.emitters) {
            wanted.emplace(kEmitter.key, &kEmitter);
        }
        std::erase_if(rings, [&wanted](const auto& each) {
            const auto kFound = wanted.find(each.first);
            return kFound == wanted.end() || kFound->second->capacity != each.second.capacity ||
                   kFound->second->ring != each.second.generation;
        });
        // Each emitter its ring, the farthest first as the frame orders
        // them; one the pool has no room for is left out.
        for (const particles::EmitterDraw& kEmitter : frame.emitters) {
            auto ring = rings.find(kEmitter.key);
            const bool kFresh = ring == rings.end();
            if (kFresh) {
                const std::optional<std::uint32_t> kRoom =
                    kEmitter.capacity > 0 ? roomFor(kEmitter.capacity) : std::nullopt;
                if (!kRoom.has_value()) {
                    ++leftOut;
                    continue;
                }
                ring = rings
                           .emplace(kEmitter.key,
                                    Ring{.offset = *kRoom, .capacity = kEmitter.capacity, .generation = kEmitter.ring})
                           .first;
                cleared.push_back(kEmitter.key);
            }
            const std::size_t kAt = blocks.size();
            blocks.resize(kAt + kBlockStride);
            const EmitterBlock kBlock = blockOf(kEmitter, ring->second.offset, frame.clock);
            std::memcpy(blocks.data() + kAt, &kBlock, sizeof(kBlock));
            drawing.push_back(Drawn{.offset = ring->second.offset,
                                    .capacity = kEmitter.capacity,
                                    .spawned = kEmitter.spawned,
                                    .fresh = kFresh,
                                    .material = placeOf(kEmitter.material)});
            spawned += kEmitter.spawned;
        }
        emitting = !drawing.empty();
        if (emitting) {
            if (const mrhiResult kImported = mrhiImportBuffer(native, buffer, &pool); kImported != mrhi_success) {
                return failed("the particles' pool could not join the frame", kImported);
            }
        }
        return {};
    }

    void declareRibbons(const particles::Frame& frame) {
        ribboned = !frame.ribbons.empty();
        if (!ribboned) {
            return;
        }
        points.reserve(frame.ribbonPoints.size());
        for (const particles::RibbonPoint& kPoint : frame.ribbonPoints) {
            points.push_back({kPoint.place[0],
                              kPoint.place[1],
                              kPoint.place[2],
                              kPoint.width,
                              kPoint.color[0],
                              kPoint.color[1],
                              kPoint.color[2],
                              kPoint.color[3],
                              kPoint.along,
                              0,
                              0,
                              0});
        }
        for (const particles::Ribbon& kRibbon : frame.ribbons) {
            const std::size_t kAt = ribbonBlocks.size();
            ribbonBlocks.resize(kAt + kBlockStride);
            const std::array<std::uint32_t, 4> kRange = {kRibbon.first, kRibbon.count, 0, 0};
            std::memcpy(ribbonBlocks.data() + kAt, kRange.data(), sizeof(kRange));
            ribbonCounts.push_back(kRibbon.count);
            ribbonMaterials.push_back(placeOf(kRibbon.material));
        }
    }

    /// A buffer of `bytes` the open frame makes, written by the upload pass.
    result::Status declareBuffer(std::uint64_t bytes, mrhiResourceId& made, std::vector<mrhiAccess>& writes) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = bytes;
        if (const mrhiResult kDeclared = mrhiDeclareBuffer(native, &def, &made); kDeclared != mrhi_success) {
            return failed("the particles' buffers could not be declared", kDeclared);
        }
        writes.push_back(wholeOf(made, mrhi_accessCopyDestination));
        return {};
    }

    result::Status addPass(const mrhiPassDef& def, std::optional<mrhiPassId>& made) {
        mrhiPassId added{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &def, &added); kAdded != mrhi_success) {
            return failed("a particles' pass could not be added", kAdded);
        }
        made = added;
        return {};
    }

    result::Status declare(const particles::Frame& frame,
                           std::span<const Material> given,
                           const ViewBlock& seen,
                           const Drawing& drawingWith) {
        emitting = false;
        ribboned = false;
        drawing.clear();
        cleared.clear();
        blocks.clear();
        points.clear();
        ribbonBlocks.clear();
        ribbonCounts.clear();
        ribbonMaterials.clear();
        leftOut = 0;
        spawned = 0;
        uploadPass.reset();
        clearPass.reset();
        spawnPass.reset();
        depthPass.reset();
        drawPass.reset();
        // A frame drawing no emitter lets every ring go.
        if (frame.emitters.empty()) {
            rings.clear();
        }
        if (frame.emitters.empty() && frame.ribbons.empty()) {
            return {};
        }
        // The materials as the draws find them by place: none is a plain
        // one.
        materials.assign(given.begin(), given.end());
        if (materials.empty()) {
            materials.emplace_back();
        }
        // The pipelines of the blends the frame draws with: until they are
        // made, the frame draws none, and its rings wait.
        std::array<bool, kBlends> wanted{};
        for (const particles::EmitterDraw& kEmitter : frame.emitters) {
            wanted.at(static_cast<std::size_t>(materials[placeOf(kEmitter.material)].blend)) = true;
        }
        for (const particles::Ribbon& kRibbon : frame.ribbons) {
            wanted.at(static_cast<std::size_t>(materials[placeOf(kRibbon.material)].blend)) = true;
        }
        RAWFRAME_TRY_ASSIGN(const bool kReady, ready(wanted));
        if (!kReady) {
            return {};
        }
        RAWFRAME_TRY(declareEmitters(frame));
        declareRibbons(frame);
        if (!emitting && !ribboned) {
            return {};
        }
        view = seen;
        with = drawingWith;
        materialBlocks.assign(materials.size() * kBlockStride, 0);
        for (std::size_t at = 0; at < materials.size(); ++at) {
            std::memcpy(materialBlocks.data() + (at * kBlockStride), &materials[at].block, sizeof(MaterialBlock));
        }
        // The upload pass writes what the others read.
        std::vector<mrhiAccess> writes;
        RAWFRAME_TRY(declareBuffer(materialBlocks.size(), materialsResource, writes));
        RAWFRAME_TRY(declareBuffer(sizeof(ViewBlock), viewResource, writes));
        if (emitting) {
            RAWFRAME_TRY(declareBuffer(blocks.size(), blocksResource, writes));
        }
        if (ribboned) {
            RAWFRAME_TRY(declareBuffer(points.size() * kPointBytes, pointsResource, writes));
            RAWFRAME_TRY(declareBuffer(ribbonBlocks.size(), ribbonsResource, writes));
        }
        // An exposure of one, and a depth of nought, where the host has
        // none.
        if (!with.exposure.has_value()) {
            RAWFRAME_TRY(declareBuffer(16, exposure, writes));
        } else {
            exposure = resourceOf(*with.exposure);
        }
        if (!with.depth.has_value()) {
            mrhiTextureDef def = mrhiDefaultTextureDef();
            def.format = mrhi_formatDepth32Float;
            def.width = 1;
            def.height = 1;
            if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &def, &depth); kDeclared != mrhi_success) {
                return failed("the particles' depth could not be declared", kDeclared);
            }
        } else {
            depth = resourceOf(*with.depth);
        }
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = writes.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(writes.size());
        RAWFRAME_TRY(addPass(uploadDef, uploadPass));
        // The new rings cleared, then the births, where any emitter
        // spawns: passes of their own, so the births come after the
        // clearing.
        if (emitting) {
            const std::array<mrhiAccess, 2> kSpawning = {wholeOf(blocksResource, mrhi_accessUniform),
                                                         wholeOf(pool, mrhi_accessStorageReadWrite)};
            mrhiPassDef def = mrhiDefaultPassDef();
            def.accesses = kSpawning.data();
            def.accessCount = static_cast<std::uint32_t>(kSpawning.size());
            if (!cleared.empty()) {
                RAWFRAME_TRY(addPass(def, clearPass));
            }
            if (spawned > 0) {
                RAWFRAME_TRY(addPass(def, spawnPass));
            }
        }
        if (!with.depth.has_value()) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.depthTarget.resource = depth;
            def.depthTarget.depthLoad = mrhi_loadClear;
            def.depthTarget.depthStore = mrhi_storeKeep;
            def.depthTarget.clearDepth = 0;
            RAWFRAME_TRY(addPass(def, depthPass));
        }
        // The drawing, over the host's picture.
        std::vector<mrhiAccess> reads = {wholeOf(viewResource, mrhi_accessUniform),
                                         wholeOf(materialsResource, mrhi_accessUniform),
                                         wholeOf(exposure, mrhi_accessStorageRead)};
        mrhiAccess behind = wholeOf(depth, mrhi_accessSampled);
        behind.range.aspect = mrhi_aspectDepthOnly;
        reads.push_back(behind);
        if (emitting) {
            reads.push_back(wholeOf(blocksResource, mrhi_accessUniform));
            reads.push_back(wholeOf(pool, mrhi_accessStorageRead));
        }
        if (ribboned) {
            reads.push_back(wholeOf(pointsResource, mrhi_accessStorageRead));
            reads.push_back(wholeOf(ribbonsResource, mrhi_accessUniform));
        }
        // Each texture a drawn material samples, once.
        std::set<std::uint64_t> textures;
        for (const std::uint32_t kMaterial : ribbonMaterials) {
            textures.insert(materials[kMaterial].color.texture);
            textures.insert(materials[kMaterial].emission.texture);
        }
        for (const Drawn& kDrawn : drawing) {
            textures.insert(materials[kDrawn.material].color.texture);
            textures.insert(materials[kDrawn.material].emission.texture);
        }
        for (const std::uint64_t kTexture : textures) {
            if (kTexture != 0) {
                reads.push_back(wholeOf(resourceOf(kTexture), mrhi_accessSampled));
            }
        }
        mrhiPassDef drawDef = mrhiDefaultPassDef();
        drawDef.colorTargets[0].resource = resourceOf(with.picture);
        drawDef.colorTargets[0].load = mrhi_loadKeep;
        drawDef.colorTargets[0].store = mrhi_storeKeep;
        drawDef.colorTargetCount = 1;
        drawDef.accesses = reads.data();
        drawDef.accessCount = static_cast<std::uint32_t>(reads.size());
        return addPass(drawDef, drawPass);
    }

    result::Status recordUpload() {
        constexpr std::array<float, 4> kExposure = {0, 1, 0, 0};
        const mrhiPassId kPass = *uploadPass;
        if (mrhiBeginPass(native, kPass) != mrhi_success) {
            return failed("the particles' upload could not begin", mrhi_errorState);
        }
        if (mrhiWriteBuffer(native, kPass, viewResource, 0, &view, sizeof(view)) != mrhi_success ||
            mrhiWriteBuffer(native, kPass, materialsResource, 0, materialBlocks.data(), materialBlocks.size()) !=
                mrhi_success ||
            (!with.exposure.has_value() &&
             mrhiWriteBuffer(native, kPass, exposure, 0, kExposure.data(), sizeof(kExposure)) != mrhi_success) ||
            (emitting &&
             mrhiWriteBuffer(native, kPass, blocksResource, 0, blocks.data(), blocks.size()) != mrhi_success) ||
            (ribboned &&
             (mrhiWriteBuffer(native, kPass, pointsResource, 0, points.data(), points.size() * kPointBytes) !=
                  mrhi_success ||
              mrhiWriteBuffer(native, kPass, ribbonsResource, 0, ribbonBlocks.data(), ribbonBlocks.size()) !=
                  mrhi_success))) {
            return failed("the particles could not be written", mrhi_errorCapacity);
        }
        if (mrhiEndPass(native, kPass) != mrhi_success) {
            return failed("the particles' upload could not end", mrhi_errorState);
        }
        return {};
    }

    result::Status recordBirths() {
        const std::uint64_t kPoolBytes = std::uint64_t{capacity} * kParticleBytes;
        // A new ring's every slot cleared, a slot each invocation; then
        // each emitter's births, a particle each.
        std::array<mrhiBinding, 2> spawning = {bufferAt(0, blocksResource, sizeof(EmitterBlock)),
                                               bufferAt(1, pool, kPoolBytes)};
        for (const auto& [kPass, kPipeline, kClearing] :
             {std::tuple{clearPass, clear, true}, std::tuple{spawnPass, spawn, false}}) {
            if (!kPass.has_value()) {
                continue;
            }
            if (mrhiBeginPass(native, *kPass) != mrhi_success ||
                mrhiSetComputePipeline(native, *kPass, kPipeline) != mrhi_success) {
                return failed("the particles' births could not begin", mrhi_errorState);
            }
            for (std::size_t at = 0; at < drawing.size(); ++at) {
                const std::uint32_t kCount =
                    kClearing ? (drawing[at].fresh ? drawing[at].capacity : 0) : drawing[at].spawned;
                if (kCount == 0) {
                    continue;
                }
                spawning[0].offset = at * kBlockStride;
                if (mrhiSetBindings(native, *kPass, 0, spawning.data(), spawning.size()) != mrhi_success ||
                    mrhiDispatch(native, *kPass, (kCount + kSpawnGroup - 1) / kSpawnGroup, 1, 1) != mrhi_success) {
                    return failed("an emitter's births could not be recorded", mrhi_errorState);
                }
            }
            if (mrhiEndPass(native, *kPass) != mrhi_success) {
                return failed("the particles' births could not end", mrhi_errorState);
            }
        }
        return {};
    }

    /// A draw's material bound: its block, and its textures.
    static void bindMaterial(std::array<mrhiBinding, 10>& table, const Material& material, std::uint32_t place) {
        table[4].offset = place * kBlockStride;
        table[6].resource = resourceOf(material.color.texture);
        table[7].sampler = samplerOf(material.color.sampler);
        table[8].resource = resourceOf(material.emission.texture);
        table[9].sampler = samplerOf(material.emission.sampler);
    }

    result::Status recordDraws() {
        const mrhiPassId kPass = *drawPass;
        if (mrhiBeginPass(native, kPass) != mrhi_success) {
            return failed("the particles' drawing could not begin", mrhi_errorState);
        }
        std::array<mrhiBinding, 10> table = {bufferAt(0, viewResource, sizeof(ViewBlock)),
                                             bufferAt(1, {}, sizeof(EmitterBlock)),
                                             bufferAt(2, {}, 0),
                                             bufferAt(3, exposure, 16),
                                             bufferAt(4, materialsResource, sizeof(MaterialBlock)),
                                             depthAt(5, depth),
                                             textureAt(6),
                                             samplerAt(7),
                                             textureAt(8),
                                             samplerAt(9)};
        if (ribboned) {
            table[1].resource = ribbonsResource;
            table[2] = bufferAt(2, pointsResource, points.size() * kPointBytes);
            std::optional<Blend> blending;
            // Each ribbon, two triangles between each point and the next.
            for (std::size_t at = 0; at < ribbonCounts.size(); ++at) {
                const Material& kMaterial = materials[ribbonMaterials[at]];
                if (blending != kMaterial.blend) {
                    blending = kMaterial.blend;
                    if (mrhiSetGraphicsPipeline(native, kPass, ribbons.at(static_cast<std::size_t>(*blending))) !=
                        mrhi_success) {
                        return failed("the ribbons could not begin", mrhi_errorState);
                    }
                }
                table[1].offset = at * kBlockStride;
                bindMaterial(table, kMaterial, ribbonMaterials[at]);
                if (mrhiSetBindings(native, kPass, 0, table.data(), table.size()) != mrhi_success ||
                    mrhiDraw(native, kPass, (ribbonCounts[at] - 1) * 6, 1, 0, 0) != mrhi_success) {
                    return failed("a ribbon could not be drawn", mrhi_errorState);
                }
            }
        }
        if (emitting) {
            table[1].resource = blocksResource;
            table[2] = bufferAt(2, pool, std::uint64_t{capacity} * kParticleBytes);
            std::optional<Blend> blending;
            // Each emitter's ring, every slot an instance: the dead draw
            // nothing.
            for (std::size_t at = 0; at < drawing.size(); ++at) {
                const Drawn& kDrawn = drawing[at];
                const Material& kMaterial = materials[kDrawn.material];
                if (blending != kMaterial.blend) {
                    blending = kMaterial.blend;
                    if (mrhiSetGraphicsPipeline(native, kPass, particles.at(static_cast<std::size_t>(*blending))) !=
                        mrhi_success) {
                        return failed("the particles could not begin", mrhi_errorState);
                    }
                }
                table[1].offset = at * kBlockStride;
                bindMaterial(table, kMaterial, kDrawn.material);
                if (mrhiSetBindings(native, kPass, 0, table.data(), table.size()) != mrhi_success ||
                    mrhiDraw(native, kPass, 6, kDrawn.capacity, 0, kDrawn.offset) != mrhi_success) {
                    return failed("an emitter's particles could not be drawn", mrhi_errorState);
                }
            }
        }
        if (mrhiEndPass(native, kPass) != mrhi_success) {
            return failed("the particles' drawing could not end", mrhi_errorState);
        }
        return {};
    }

    result::Status record() {
        if (!drawPass.has_value()) {
            return {};
        }
        RAWFRAME_TRY(recordUpload());
        RAWFRAME_TRY(recordBirths());
        if (depthPass.has_value() &&
            (mrhiBeginPass(native, *depthPass) != mrhi_success || mrhiEndPass(native, *depthPass) != mrhi_success)) {
            return failed("the particles' depth could not be cleared", mrhi_errorState);
        }
        return recordDraws();
    }
};

Particles::Particles(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Particles::~Particles() = default;

result::Result<std::unique_ptr<Particles>>
Particles::create(render::Device& device, Target target, std::uint32_t capacity) {
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    state->target = target;
    state->capacity = capacity;
    return std::unique_ptr<Particles>{new Particles{std::move(state)}};
}

result::Status Particles::declare(const particles::Frame& frame,
                                  std::span<const Material> materials,
                                  const ViewBlock& view,
                                  const Drawing& with) {
    return state_->declare(frame, materials, view, with);
}

result::Status Particles::record() {
    return state_->record();
}

bool Particles::enabled() const noexcept {
    return state_->drawPass.has_value();
}

void Particles::ended(bool submitted) noexcept {
    if (!submitted) {
        for (const std::uint64_t kKey : state_->cleared) {
            state_->rings.erase(kKey);
        }
    }
    state_->cleared.clear();
}

std::size_t Particles::emittersDrawn() const noexcept {
    return state_->emitting ? state_->drawing.size() : 0;
}

std::size_t Particles::emittersLeftOut() const noexcept {
    return state_->leftOut;
}

std::uint64_t Particles::spawned() const noexcept {
    return state_->emitting ? state_->spawned : 0;
}

std::size_t Particles::ribbonsDrawn() const noexcept {
    return state_->ribboned ? state_->ribbonCounts.size() : 0;
}

} // namespace rawframe::particles_gpu
