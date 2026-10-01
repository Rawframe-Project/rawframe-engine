#include "particles.h"

#include "blocks.h"
#include "tables.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <maul-rhi/encoder.h>
#include <tuple>
#include <utility>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

/// The spawn's workgroup, as spawn.comp declares it.
constexpr std::uint32_t kSpawnGroup = 64;

/// An emitter as its shaders read it, its ring at `offset`.
EmitterBlock blockOf(const render_scene::SceneEmitter& emitter, std::uint32_t offset, float now) noexcept {
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

} // namespace

ParticleViewBlock viewOf(const render_scene::SceneFrame& frame) noexcept {
    // The eye's right and up: the view's first two rows.
    return ParticleViewBlock{.right = {frame.view[0], frame.view[4], frame.view[8], frame.particleClock},
                             .up = {frame.view[1], frame.view[5], frame.view[9], render_scene::kParticleClockPeriod},
                             .lens = {frame.projection[14], 0, 0, 0}};
}

ParticlePass::ParticlePass(mrhiDevice* native, std::uint32_t capacity) noexcept : native_(native), capacity_(capacity) {
}

ParticlePass::~ParticlePass() {
    if (native_ != nullptr && buffer_.index1 != 0) {
        // Maul RHI retires what a frame still uses once the frame is done.
        static_cast<void>(mrhiDestroyBuffer(native_, buffer_));
    }
}

std::optional<std::uint32_t> ParticlePass::roomFor(std::uint32_t capacity) const {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> taken;
    taken.reserve(rings_.size());
    for (const auto& [kKey, kRing] : rings_) {
        taken.emplace_back(kRing.offset, kRing.capacity);
    }
    std::ranges::sort(taken);
    std::uint32_t free = 0;
    for (const auto& [kOffset, kCapacity] : taken) {
        if (kOffset - free >= capacity) {
            return free;
        }
        free = kOffset + kCapacity;
    }
    if (capacity_ - free >= capacity) {
        return free;
    }
    return std::nullopt;
}

result::Status
ParticlePass::declare(const render_scene::SceneFrame& frame, bool made, std::vector<mrhiAccess>& writes) {
    enabled_ = false;
    drawing_.clear();
    cleared_.clear();
    blocks_.clear();
    leftOut_ = 0;
    spawned_ = 0;
    if (!made || frame.emitters.empty()) {
        // A frame drawing none lets every ring go.
        if (frame.emitters.empty()) {
            rings_.clear();
        }
        return {};
    }
    if (buffer_.index1 == 0) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = std::uint64_t{capacity_} * kParticleBytes;
        def.usage = mrhi_bufferStorage;
        if (const mrhiResult kMade = mrhiCreateBuffer(native_, &def, &buffer_); kMade != mrhi_success) {
            return failed("the particles' pool could not be made", kMade);
        }
    }
    // Rings let go: their emitter not drawn, or its ring started anew or
    // changed its size.
    std::map<std::uint64_t, const render_scene::SceneEmitter*> wanted;
    for (const render_scene::SceneEmitter& kEmitter : frame.emitters) {
        wanted.emplace(kEmitter.key, &kEmitter);
    }
    std::erase_if(rings_, [&wanted](const auto& each) {
        const auto kFound = wanted.find(each.first);
        return kFound == wanted.end() || kFound->second->capacity != each.second.capacity ||
               kFound->second->ring != each.second.generation;
    });
    // Each emitter its ring, the farthest first as the frame orders them;
    // one the pool has no room for is left out.
    for (const render_scene::SceneEmitter& kEmitter : frame.emitters) {
        auto ring = rings_.find(kEmitter.key);
        const bool kFresh = ring == rings_.end();
        if (kFresh) {
            const std::optional<std::uint32_t> kRoom =
                kEmitter.capacity > 0 ? roomFor(kEmitter.capacity) : std::nullopt;
            if (!kRoom.has_value()) {
                ++leftOut_;
                continue;
            }
            ring = rings_
                       .emplace(kEmitter.key,
                                Ring{.offset = *kRoom, .capacity = kEmitter.capacity, .generation = kEmitter.ring})
                       .first;
            cleared_.push_back(kEmitter.key);
        }
        const std::size_t kAt = blocks_.size();
        blocks_.resize(kAt + kBlockStride);
        const EmitterBlock kBlock = blockOf(kEmitter, ring->second.offset, frame.particleClock);
        std::memcpy(blocks_.data() + kAt, &kBlock, sizeof(kBlock));
        render_scene::SceneTextures textures;
        if (kEmitter.material < frame.textures.size()) {
            textures = frame.textures[kEmitter.material];
        }
        drawing_.push_back(Drawn{.offset = ring->second.offset,
                                 .capacity = kEmitter.capacity,
                                 .spawned = kEmitter.spawned,
                                 .fresh = kFresh,
                                 .textures = textures});
        spawned_ += kEmitter.spawned;
    }
    if (drawing_.empty()) {
        return {};
    }
    enabled_ = true;
    view_ = viewOf(frame);
    if (const mrhiResult kImported = mrhiImportBuffer(native_, buffer_, &pool_); kImported != mrhi_success) {
        return failed("the particles' pool could not join the frame", kImported);
    }
    for (const auto& [kBytes, kMade] : {std::pair{std::uint64_t{blocks_.size()}, &blocksResource_},
                                        std::pair{std::uint64_t{sizeof(ParticleViewBlock)}, &viewResource_}}) {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = kBytes;
        if (mrhiDeclareBuffer(native_, &def, kMade) != mrhi_success) {
            return failed("the particles' emitters could not be declared", mrhi_errorCapacity);
        }
        writes.push_back(wholeOf(*kMade, mrhi_accessCopyDestination));
    }
    return {};
}

result::Status ParticlePass::addPasses() {
    if (!enabled_) {
        return {};
    }
    // The new rings cleared, then the births, where any emitter spawns:
    // passes of their own, so the births come after the clearing.
    const std::array<mrhiAccess, 2> kSpawning = {wholeOf(blocksResource_, mrhi_accessUniform),
                                                 wholeOf(pool_, mrhi_accessStorageReadWrite)};
    for (const auto& [kWanted, kPass] :
         {std::pair{!cleared_.empty(), &clearPass_}, std::pair{spawned_ > 0, &spawnPass_}}) {
        if (!kWanted) {
            continue;
        }
        mrhiPassDef def = mrhiDefaultPassDef();
        def.accesses = kSpawning.data();
        def.accessCount = static_cast<std::uint32_t>(kSpawning.size());
        if (const mrhiResult kAdded = mrhiAddPass(native_, &def, kPass); kAdded != mrhi_success) {
            return failed("the particles' births could not be added", kAdded);
        }
    }
    return {};
}

void ParticlePass::drawReads(std::vector<mrhiAccess>& reads) const {
    if (!enabled_) {
        return;
    }
    reads.push_back(wholeOf(blocksResource_, mrhi_accessUniform));
    reads.push_back(wholeOf(viewResource_, mrhi_accessUniform));
    reads.push_back(wholeOf(pool_, mrhi_accessStorageRead));
}

result::Status ParticlePass::write(mrhiPassId upload) {
    if (!enabled_) {
        return {};
    }
    if (mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blocks_.size()) != mrhi_success ||
        mrhiWriteBuffer(native_, upload, viewResource_, 0, &view_, sizeof(view_)) != mrhi_success) {
        return failed("the particles' emitters could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status ParticlePass::record(const Pipelines& pipelines) {
    if (!enabled_) {
        return {};
    }
    const std::uint64_t kPoolBytes = std::uint64_t{capacity_} * kParticleBytes;
    // A new ring's every slot cleared, a slot each invocation; then each
    // emitter's births, a particle each.
    std::array<mrhiBinding, 2> spawning = {bufferAt(0, blocksResource_, sizeof(EmitterBlock)),
                                           bufferAt(1, pool_, kPoolBytes)};
    for (const auto& [kWanted, kPass, kPipeline, kClearing] :
         {std::tuple{!cleared_.empty(), clearPass_, pipelines.clearParticles.compute, true},
          std::tuple{spawned_ > 0, spawnPass_, pipelines.spawn.compute, false}}) {
        if (!kWanted) {
            continue;
        }
        if (mrhiBeginPass(native_, kPass) != mrhi_success ||
            mrhiSetComputePipeline(native_, kPass, kPipeline) != mrhi_success) {
            return failed("the particles' births could not begin", mrhi_errorState);
        }
        for (std::size_t at = 0; at < drawing_.size(); ++at) {
            const std::uint32_t kCount =
                kClearing ? (drawing_[at].fresh ? drawing_[at].capacity : 0) : drawing_[at].spawned;
            if (kCount == 0) {
                continue;
            }
            spawning[0].offset = at * kBlockStride;
            if (mrhiSetBindings(native_, kPass, 0, spawning.data(), spawning.size()) != mrhi_success ||
                mrhiDispatch(native_, kPass, (kCount + kSpawnGroup - 1) / kSpawnGroup, 1, 1) != mrhi_success) {
                return failed("an emitter's births could not be recorded", mrhi_errorState);
            }
        }
        if (mrhiEndPass(native_, kPass) != mrhi_success) {
            return failed("the particles' births could not end", mrhi_errorState);
        }
    }
    return {};
}

result::Status ParticlePass::recordDraws(mrhiPassId pass, const Pipelines& pipelines, const ParticleDrawing& with) {
    if (!enabled_) {
        return {};
    }
    const std::uint64_t kPoolBytes = std::uint64_t{capacity_} * kParticleBytes;
    std::array<mrhiBinding, 11> table = {bufferAt(0, with.block, sizeof(FrameBlock)),
                                         bufferAt(1, viewResource_, sizeof(ParticleViewBlock)),
                                         bufferAt(2, blocksResource_, sizeof(EmitterBlock)),
                                         bufferAt(3, pool_, kPoolBytes),
                                         bufferAt(4, with.exposure, sizeof(ExposureBlock)),
                                         bufferAt(5, with.materials, with.materialsBytes),
                                         depthAt(6, with.depth),
                                         textureAt(7, {}),
                                         samplerAt(8, {}),
                                         textureAt(9, {}),
                                         samplerAt(10, {})};
    if (mrhiSetGraphicsPipeline(native_, pass, pipelines.particles.pipeline) != mrhi_success) {
        return failed("the particles could not begin", mrhi_errorState);
    }
    // Each emitter's ring, every slot an instance: the dead draw nothing.
    for (std::size_t at = 0; at < drawing_.size(); ++at) {
        const Drawn& kDrawn = drawing_[at];
        table[2].offset = at * kBlockStride;
        bindTexture(*with.textures, pipelines, table[7], table[8], kDrawn.textures.base);
        bindTexture(*with.textures, pipelines, table[9], table[10], kDrawn.textures.emission);
        if (mrhiSetBindings(native_, pass, 0, table.data(), table.size()) != mrhi_success ||
            mrhiDraw(native_, pass, 6, kDrawn.capacity, 0, kDrawn.offset) != mrhi_success) {
            return failed("an emitter's particles could not be drawn", mrhi_errorState);
        }
    }
    return {};
}

bool ParticlePass::enabled() const noexcept {
    return enabled_;
}

void ParticlePass::ended(bool submitted) noexcept {
    if (!submitted) {
        for (const std::uint64_t kKey : cleared_) {
            rings_.erase(kKey);
        }
    }
    cleared_.clear();
}

std::size_t ParticlePass::drawn() const noexcept {
    return enabled_ ? drawing_.size() : 0;
}

std::size_t ParticlePass::leftOut() const noexcept {
    return leftOut_;
}

std::uint64_t ParticlePass::spawned() const noexcept {
    return enabled_ ? spawned_ : 0;
}

} // namespace rawframe::render_scene_gpu
