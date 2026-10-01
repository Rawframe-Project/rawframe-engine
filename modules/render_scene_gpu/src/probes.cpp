#include "probes.h"

#include "tables.h"

#include <algorithm>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {
            .baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = MRHI_REMAINING, .aspect = {}}};
}

} // namespace

ProbeAtlas::ProbeAtlas(mrhiDevice* native) noexcept : native_(native) {
}

ProbeAtlas::~ProbeAtlas() {
    // Maul RHI retires what a frame still uses once the frame is done.
    if (native_ != nullptr && texture_.index1 != 0) {
        static_cast<void>(mrhiDestroyTexture(native_, texture_));
    }
}

result::Status ProbeAtlas::make() {
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = kProbeFormat;
    def.width = kSide;
    def.height = kSide;
    def.kind = mrhi_textureCubeArray;
    def.depthOrLayers = 6 * kCubes;
    def.mipLevels = kLevels;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget;
    if (const mrhiResult kMade = mrhiCreateTexture(native_, &def, &texture_); kMade != mrhi_success) {
        return failed("the reflection probes' atlas could not be made", kMade);
    }
    return {};
}

bool ProbeAtlas::holds(std::uint64_t picture) const noexcept {
    return std::ranges::find(cubes_, picture, &Cube::picture) != cubes_.end();
}

result::Status ProbeAtlas::declare(const render_scene::SceneFrame& frame,
                                   bool made,
                                   const render::DeviceTextures& textures,
                                   const std::function<bool(std::uint64_t)>& usable,
                                   std::vector<mrhiAccess>& writes) {
    ++frame_;
    fills_.clear();
    blocks_.clear();
    drawn_ = 0;
    atlas_ = {};
    // The atlas joins only a frame with probes to draw: a frame without
    // leaves its every face and mip out of the frame's tracking.
    if (made && !frame.probes.empty()) {
        if (const mrhiResult kImported = mrhiImportTexture(native_, texture_, &atlas_); kImported != mrhi_success) {
            return failed("the reflection probes' atlas could not join the frame", kImported);
        }
    }
    for (const render_scene::SceneProbe& kProbe : frame.probes) {
        blocks_.push_back(ProbeBlock{.place = {kProbe.position[0], kProbe.position[1], kProbe.position[2], -1},
                                     .extent = {kProbe.half[0], kProbe.half[1], kProbe.half[2], 0},
                                     .light = {kProbe.intensity, kProbe.intensity, kProbe.intensity, 0}});
        if (!made) {
            continue;
        }
        auto held = std::ranges::find(cubes_, kProbe.environment, &Cube::picture);
        if (held == cubes_.end()) {
            const std::uint64_t kSource = textures.resource(kProbe.environment);
            if (kSource == 0 || !textures.cube(kProbe.environment) || !usable(kProbe.environment) ||
                fills_.size() == kMostFills) {
                continue;
            }
            // A free cube, else the least lately drawn not drawn this frame.
            held = std::ranges::find(cubes_, std::uint64_t{0}, &Cube::picture);
            if (held == cubes_.end()) {
                held = std::ranges::min_element(cubes_, {}, &Cube::used);
                if (held->used == frame_) {
                    continue;
                }
            }
            *held = Cube{.picture = kProbe.environment, .used = frame_};
            fills_.push_back(
                Fill{.cube = static_cast<std::uint32_t>(held - cubes_.begin()), .source = resourceOf(kSource)});
        }
        held->used = frame_;
        blocks_.back().place[3] = static_cast<float>(held - cubes_.begin());
        ++drawn_;
    }
    if (blocks_.empty()) {
        blocks_.push_back(ProbeBlock{.place = {0, 0, 0, -1}});
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = blocks_.size() * sizeof(ProbeBlock);
    if (mrhiDeclareBuffer(native_, &def, &blocksResource_) != mrhi_success) {
        return failed("the reflection probes could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blocksResource_, mrhi_accessCopyDestination));
    return {};
}

mrhiResourceId ProbeAtlas::atlas() const noexcept {
    return atlas_;
}

mrhiResourceId ProbeAtlas::blocks() const noexcept {
    return blocksResource_;
}

std::uint64_t ProbeAtlas::blockBytes() const noexcept {
    return blocks_.size() * sizeof(ProbeBlock);
}

std::size_t ProbeAtlas::drawn() const noexcept {
    return drawn_;
}

result::Status ProbeAtlas::addPasses() {
    for (Fill& fill : fills_) {
        const mrhiAccess kSource = wholeOf(fill.source, mrhi_accessSampled);
        for (std::uint32_t face = 0; face < 6; ++face) {
            for (std::uint32_t level = 0; level < kLevels; ++level) {
                mrhiPassDef def = mrhiDefaultPassDef();
                def.colorTargets[0].resource = atlas_;
                def.colorTargets[0].mip = level;
                def.colorTargets[0].layer = (fill.cube * 6) + face;
                def.colorTargets[0].load = mrhi_loadDiscard;
                def.colorTargets[0].store = mrhi_storeKeep;
                def.colorTargetCount = 1;
                def.accesses = &kSource;
                def.accessCount = 1;
                if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &fill.passes[(face * kLevels) + level]);
                    kAdded != mrhi_success) {
                    return failed("the reflection probes' atlas could not be drawn into", kAdded);
                }
            }
        }
    }
    return {};
}

result::Status ProbeAtlas::write(mrhiPassId upload) {
    if (mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blockBytes()) != mrhi_success) {
        return failed("the reflection probes could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status ProbeAtlas::record(const Pipelines& pipelines) {
    for (const Fill& kFill : fills_) {
        const std::array<mrhiBinding, 2> kBindings = {
            cubeAt(0, kFill.source),
            samplerAt(1, pipelines.materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)])};
        // The face and the mip each pass draws, as its first instance.
        for (std::uint32_t at = 0; at < kFill.passes.size(); ++at) {
            const mrhiPassId kPass = kFill.passes[at];
            if (mrhiBeginPass(native_, kPass) != mrhi_success ||
                mrhiSetGraphicsPipeline(native_, kPass, pipelines.probeFill.pipeline) != mrhi_success ||
                mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                mrhiDraw(native_, kPass, 3, 1, 0, at) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
                return failed("a reflection probe's picture could not be drawn into the atlas", mrhi_errorState);
            }
        }
    }
    return {};
}

void ProbeAtlas::ended(bool submitted) noexcept {
    if (!submitted) {
        for (const Fill& kFill : fills_) {
            cubes_[kFill.cube] = Cube{};
        }
    }
    fills_.clear();
}

} // namespace rawframe::render_scene_gpu
