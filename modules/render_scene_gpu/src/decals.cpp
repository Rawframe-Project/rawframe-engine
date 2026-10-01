#include "decals.h"

#include "tables.h"

#include <algorithm>
#include <maul-rhi/encoder.h>

namespace rawframe::render_scene_gpu {

namespace {

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

DecalAtlas::DecalAtlas(mrhiDevice* native) noexcept : native_(native) {
}

DecalAtlas::~DecalAtlas() {
    // Maul RHI retires what a frame still uses once the frame is done.
    if (native_ != nullptr && texture_.index1 != 0) {
        static_cast<void>(mrhiDestroyTexture(native_, texture_));
    }
}

result::Status DecalAtlas::make() {
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = kPictureFormat;
    def.width = kSide;
    def.height = kSide;
    def.kind = mrhi_texture2dArray;
    def.depthOrLayers = kLayers;
    def.mipLevels = kLevels;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget;
    if (const mrhiResult kMade = mrhiCreateTexture(native_, &def, &texture_); kMade != mrhi_success) {
        return failed("the decals' atlas could not be made", kMade);
    }
    return {};
}

result::Status DecalAtlas::declare(const render_scene::SceneFrame& frame,
                                   bool made,
                                   const render::DeviceTextures& textures,
                                   std::vector<mrhiAccess>& writes) {
    ++frame_;
    fills_.clear();
    blocks_.clear();
    drawn_ = 0;
    atlas_ = {};
    // The atlas joins only a frame with decals to draw: a frame without
    // leaves its every layer and mip out of the frame's tracking.
    if (made && !frame.decals.empty()) {
        if (const mrhiResult kImported = mrhiImportTexture(native_, texture_, &atlas_); kImported != mrhi_success) {
            return failed("the decals' atlas could not join the frame", kImported);
        }
    }
    for (const render_scene::SceneDecal& kDecal : frame.decals) {
        DecalBlock& block = blocks_.emplace_back(DecalBlock{.toBox = kDecal.toBox, .color = kDecal.color});
        block.layer = {-1, 0, 0, 0};
        const std::uint64_t kSource = textures.resource(kDecal.texture);
        if (!made) {
            continue;
        }
        auto held = std::ranges::find(layers_, kDecal.texture, &Layer::texture);
        if (held == layers_.end()) {
            if (kSource == 0 || textures.cube(kDecal.texture) || fills_.size() == kMostFills) {
                continue;
            }
            // A free layer, else the least lately drawn not drawn this frame.
            held = std::ranges::find(layers_, std::uint64_t{0}, &Layer::texture);
            if (held == layers_.end()) {
                held = std::ranges::min_element(layers_, {}, &Layer::used);
                if (held->used == frame_) {
                    continue;
                }
            }
            *held = Layer{.texture = kDecal.texture, .used = frame_};
            fills_.push_back(
                Fill{.layer = static_cast<std::uint32_t>(held - layers_.begin()), .source = resourceOf(kSource)});
        }
        held->used = frame_;
        block.layer[0] = static_cast<float>(held - layers_.begin());
        ++drawn_;
    }
    if (blocks_.empty()) {
        blocks_.push_back(DecalBlock{.layer = {-1, 0, 0, 0}});
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = blocks_.size() * sizeof(DecalBlock);
    if (mrhiDeclareBuffer(native_, &def, &blocksResource_) != mrhi_success) {
        return failed("the decals could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blocksResource_, mrhi_accessCopyDestination));
    return {};
}

mrhiResourceId DecalAtlas::atlas() const noexcept {
    return atlas_;
}

mrhiResourceId DecalAtlas::blocks() const noexcept {
    return blocksResource_;
}

std::uint64_t DecalAtlas::blockBytes() const noexcept {
    return blocks_.size() * sizeof(DecalBlock);
}

std::size_t DecalAtlas::drawn() const noexcept {
    return drawn_;
}

result::Status DecalAtlas::addPasses() {
    for (Fill& fill : fills_) {
        const mrhiAccess kSource = wholeOf(fill.source, mrhi_accessSampled);
        for (std::uint32_t level = 0; level < kLevels; ++level) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = atlas_;
            def.colorTargets[0].mip = level;
            def.colorTargets[0].layer = fill.layer;
            def.colorTargets[0].load = mrhi_loadDiscard;
            def.colorTargets[0].store = mrhi_storeKeep;
            def.colorTargetCount = 1;
            def.accesses = &kSource;
            def.accessCount = 1;
            if (const mrhiResult kAdded = mrhiAddPass(native_, &def, &fill.passes[level]); kAdded != mrhi_success) {
                return failed("the decals' atlas could not be drawn into", kAdded);
            }
        }
    }
    return {};
}

result::Status DecalAtlas::write(mrhiPassId upload) {
    if (mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blockBytes()) != mrhi_success) {
        return failed("the decals could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status DecalAtlas::record(const Pipelines& pipelines) {
    for (const Fill& kFill : fills_) {
        const std::array<mrhiBinding, 2> kBindings = {
            textureAt(0, kFill.source),
            samplerAt(1, pipelines.materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)])};
        for (const mrhiPassId kPass : kFill.passes) {
            if (mrhiBeginPass(native_, kPass) != mrhi_success ||
                mrhiSetGraphicsPipeline(native_, kPass, pipelines.decalFill.pipeline) != mrhi_success ||
                mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                mrhiDraw(native_, kPass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
                return failed("a decal's texture could not be drawn into the atlas", mrhi_errorState);
            }
        }
    }
    return {};
}

void DecalAtlas::ended(bool submitted) noexcept {
    if (!submitted) {
        for (const Fill& kFill : fills_) {
            layers_[kFill.layer] = Layer{};
        }
    }
    fills_.clear();
}

} // namespace rawframe::render_scene_gpu
