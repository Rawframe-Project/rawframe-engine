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
    for (const Layers* kHeld : {&colors_, &normals_}) {
        if (native_ != nullptr && kHeld->texture.index1 != 0) {
            static_cast<void>(mrhiDestroyTexture(native_, kHeld->texture));
        }
    }
}

result::Status DecalAtlas::make(Layers& layers, const char* why) {
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = layers.format;
    def.width = kSide;
    def.height = kSide;
    def.kind = mrhi_texture2dArray;
    def.depthOrLayers = kLayers;
    def.mipLevels = kLevels;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget;
    if (const mrhiResult kMade = mrhiCreateTexture(native_, &def, &layers.texture); kMade != mrhi_success) {
        return failed(why, kMade);
    }
    return {};
}

result::Status DecalAtlas::make() {
    RAWFRAME_TRY(make(colors_, "the decals' atlas could not be made"));
    return make(normals_, "the decals' normals' atlas could not be made");
}

result::Status DecalAtlas::import(Layers& layers, const char* why) {
    if (const mrhiResult kImported = mrhiImportTexture(native_, layers.texture, &layers.atlas);
        kImported != mrhi_success) {
        return failed(why, kImported);
    }
    return {};
}

float DecalAtlas::layerOf(Layers& layers, std::uint64_t texture, const render::DeviceTextures& textures) {
    auto held = std::ranges::find(layers.held, texture, &Layer::texture);
    if (held == layers.held.end()) {
        const std::uint64_t kSource = textures.resource(texture);
        if (kSource == 0 || textures.cube(texture) || layers.fills.size() == kMostFills) {
            return -1;
        }
        // A free layer, else the least lately drawn not drawn this frame.
        held = std::ranges::find(layers.held, std::uint64_t{0}, &Layer::texture);
        if (held == layers.held.end()) {
            held = std::ranges::min_element(layers.held, {}, &Layer::used);
            if (held->used == frame_) {
                return -1;
            }
        }
        *held = Layer{.texture = texture, .used = frame_};
        layers.fills.push_back(
            Fill{.layer = static_cast<std::uint32_t>(held - layers.held.begin()), .source = resourceOf(kSource)});
    }
    held->used = frame_;
    return static_cast<float>(held - layers.held.begin());
}

result::Status DecalAtlas::declare(const render_scene::SceneFrame& frame,
                                   bool made,
                                   const render::DeviceTextures& textures,
                                   std::vector<mrhiAccess>& writes) {
    ++frame_;
    blocks_.clear();
    drawn_ = 0;
    bent_ = 0;
    for (Layers* layers : {&colors_, &normals_}) {
        layers->fills.clear();
        layers->atlas = {};
    }
    // Each atlas joins only a frame with decals to draw into it: a frame
    // without leaves its every layer and mip out of the frame's tracking.
    if (made && !frame.decals.empty()) {
        RAWFRAME_TRY(import(colors_, "the decals' atlas could not join the frame"));
    }
    if (made && std::ranges::any_of(frame.decals, [](const render_scene::SceneDecal& decal) {
            return decal.normal != 0;
        })) {
        RAWFRAME_TRY(import(normals_, "the decals' normals' atlas could not join the frame"));
    }
    for (const render_scene::SceneDecal& kDecal : frame.decals) {
        DecalBlock& block = blocks_.emplace_back(DecalBlock{.toBox = kDecal.toBox, .color = kDecal.color});
        block.layer = {-1, -1, kDecal.roughness, 0};
        if (!made) {
            continue;
        }
        block.layer[0] = layerOf(colors_, kDecal.texture, textures);
        if (block.layer[0] < 0) {
            continue;
        }
        ++drawn_;
        if (kDecal.normal != 0) {
            block.layer[1] = layerOf(normals_, kDecal.normal, textures);
            bent_ += block.layer[1] < 0 ? 0 : 1;
        }
    }
    if (blocks_.empty()) {
        blocks_.push_back(DecalBlock{.layer = {-1, -1, 0, 0}});
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = blocks_.size() * sizeof(DecalBlock);
    if (mrhiDeclareBuffer(native_, &def, &blocksResource_) != mrhi_success) {
        return failed("the decals could not be declared", mrhi_errorCapacity);
    }
    writes.push_back(wholeOf(blocksResource_, mrhi_accessCopyDestination));
    return {};
}

mrhiResourceId DecalAtlas::colors() const noexcept {
    return colors_.atlas;
}

mrhiResourceId DecalAtlas::normals() const noexcept {
    return normals_.atlas;
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

std::size_t DecalAtlas::bent() const noexcept {
    return bent_;
}

result::Status DecalAtlas::addPasses(Layers& layers) {
    for (Fill& fill : layers.fills) {
        const mrhiAccess kSource = wholeOf(fill.source, mrhi_accessSampled);
        for (std::uint32_t level = 0; level < kLevels; ++level) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = layers.atlas;
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

result::Status DecalAtlas::addPasses() {
    RAWFRAME_TRY(addPasses(colors_));
    return addPasses(normals_);
}

result::Status DecalAtlas::write(mrhiPassId upload) {
    if (mrhiWriteBuffer(native_, upload, blocksResource_, 0, blocks_.data(), blockBytes()) != mrhi_success) {
        return failed("the decals could not be written", mrhi_errorCapacity);
    }
    return {};
}

result::Status DecalAtlas::record(const Layers& layers, const Asked& fill, const Pipelines& pipelines) {
    for (const Fill& kFill : layers.fills) {
        const std::array<mrhiBinding, 2> kBindings = {
            textureAt(0, kFill.source),
            samplerAt(1, pipelines.materialSamplers[samplerOf(material::Filter::Linear, material::Address::Clamp)])};
        for (const mrhiPassId kPass : kFill.passes) {
            if (mrhiBeginPass(native_, kPass) != mrhi_success ||
                mrhiSetGraphicsPipeline(native_, kPass, fill.pipeline) != mrhi_success ||
                mrhiSetBindings(native_, kPass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                mrhiDraw(native_, kPass, 3, 1, 0, 0) != mrhi_success || mrhiEndPass(native_, kPass) != mrhi_success) {
                return failed("a decal's texture could not be drawn into the atlas", mrhi_errorState);
            }
        }
    }
    return {};
}

result::Status DecalAtlas::record(const Pipelines& pipelines) {
    RAWFRAME_TRY(record(colors_, pipelines.decalFill, pipelines));
    return record(normals_, pipelines.decalNormalFill, pipelines);
}

void DecalAtlas::ended(bool submitted) noexcept {
    for (Layers* layers : {&colors_, &normals_}) {
        if (!submitted) {
            for (const Fill& kFill : layers->fills) {
                layers->held[kFill.layer] = Layer{};
            }
        }
        layers->fills.clear();
    }
}

} // namespace rawframe::render_scene_gpu
