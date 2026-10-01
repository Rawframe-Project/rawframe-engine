#include "rawframe/render_canvas_gpu/renderer.h"

#include "generated/sprite_container.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_canvas_gpu/errors.h"

#include <algorithm>
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

namespace rawframe::render_canvas_gpu {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, CanvasGpuError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kCanvasGpuDomain, code(error), why).error()};
}

/// Maul RHI's refusal, named.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kCanvasGpuDomain, code(CanvasGpuError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

constexpr std::uint32_t kVertexBytes = sizeof(render_canvas::CanvasVertex);
static_assert(kVertexBytes == 20, "the sprite pipeline reads a corner as 20 bytes");

/// A resource of the open frame from the key `render` names it by.
mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

/// A material as the sprite shader reads it (std140, D356): its color and
/// what multiplies its texture's; its emission, and one when it multiplies;
/// what multiplies its texture's color in its emission; and its texture's
/// scale and offset.
struct MaterialBlock {
    std::array<float, 4> color{};
    std::array<float, 4> colorTexture{};
    std::array<float, 4> emission{};
    std::array<float, 4> emissionTexture{};
    std::array<float, 4> map{};
};
static_assert(sizeof(MaterialBlock) == 80, "the sprite shader reads a material as 80 bytes");

/// The stride between materials' blocks: no device asks uniform offsets
/// aligned past 256 bytes.
constexpr std::uint64_t kBlockStride = 256;

/// None: white, over what is behind.
constexpr material::CanvasMaterial kPlain{.shading = material::Shading::Unlit};

/// A draw's material: its place among the frame's, or none where the frame
/// holds no such place.
const material::CanvasMaterial& materialOf(const render_canvas::CanvasFrame& frame,
                                           const render_canvas::CanvasDraw& draw) noexcept {
    return draw.material < frame.materials.size() ? frame.materials[draw.material] : kPlain;
}

MaterialBlock blockOf(const material::CanvasMaterial& made) noexcept {
    const bool kMultiplies = made.blend == material::CanvasBlend::Multiply;
    return MaterialBlock{
        .color = made.color,
        .colorTexture = made.colorTexture,
        .emission = {made.emission[0], made.emission[1], made.emission[2], kMultiplies ? 1.0F : 0.0F},
        .emissionTexture = {made.emissionTexture[0], made.emissionTexture[1], made.emissionTexture[2], 0},
        .map = {made.sampled.scale[0], made.sampled.scale[1], made.sampled.offset[0], made.sampled.offset[1]}};
}

/// A texture's binding, its every level.
mrhiBinding textureAt(std::uint32_t slot, mrhiResourceId resource) noexcept {
    return mrhiBinding{
        .slot = slot,
        .resource = resource,
        .offset = 0,
        .size = 0,
        .viewKind = mrhi_texture2d,
        .viewFormat = mrhi_formatNone,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
        .sampler = {}};
}

mrhiBinding samplerAt(std::uint32_t slot, mrhiSamplerId sampler) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = {},
                       .offset = 0,
                       .size = 0,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = sampler};
}

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

} // namespace

struct CanvasRenderer::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    RendererLimits limits;
    RendererStatistics statistics;
    mrhiShaderId shader{};
    /// A pipeline for each of the canvas blends (D356): over what is
    /// behind, added to it, and multiplying it.
    std::array<mrhiGraphicsPipelineId, 3> pipelines{};
    std::array<std::uint64_t, 3> pipelineRequests{};
    bool pipelineReady = false;
    /// Exact textures (pixel art, interface images) are sampled nearest;
    /// block-compressed ones linearly, across their levels.
    mrhiSamplerId nearest{};
    mrhiSamplerId linear{};
    /// A material's texture's, by its declared filter and address (D356).
    std::array<mrhiSamplerId, 4> materialSamplers{};
    /// What a sprite with no texture, or a material with none, samples.
    std::shared_ptr<const texture::Texture> white;
    /// The open frame's materials' blocks.
    std::vector<std::uint8_t> blocks;
    mrhiResourceId blocksResource{};
    std::unique_ptr<render::DeviceTextures> held;
    /// What the next frame draws.
    const render_canvas::CanvasFrame* frame = nullptr;
    TextureSource textures;
    /// What the open frame declared, until it is recorded and ends.
    bool declared = false;
    std::optional<mrhiPassId> upload;
    mrhiPassId drawing{};
    std::uint64_t drawn = 0;
    /// This frame's corners and indices.
    mrhiResourceId corners_{};
    mrhiResourceId indices_{};

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        for (const mrhiSamplerId kSampler : {nearest, linear}) {
            static_cast<void>(mrhiDestroySampler(native, kSampler));
        }
        for (const mrhiSamplerId kSampler : materialSamplers) {
            static_cast<void>(mrhiDestroySampler(native, kSampler));
        }
        for (const mrhiGraphicsPipelineId kPipeline : pipelines) {
            static_cast<void>(mrhiDestroyGraphicsPipeline(native, kPipeline));
        }
        static_cast<void>(mrhiDestroyShader(native, shader));
    }

    result::Status makePipeline() {
        mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
        shaderDef.bytes = kSpriteContainer.data();
        shaderDef.byteCount = kSpriteContainer.size();
        if (const mrhiResult kMade = mrhiCreateShader(native, &shaderDef, &shader); kMade != mrhi_success) {
            return failed("the sprite shader could not be made", kMade);
        }
        constexpr std::array<mrhiVertexBufferLayout, 1> kBuffers = {
            mrhiVertexBufferLayout{.stride = kVertexBytes, .stepMode = mrhi_stepVertex}};
        constexpr std::array<mrhiVertexAttribute, 3> kAttributes = {
            mrhiVertexAttribute{.buffer = 0, .location = 0, .format = mrhi_vertexFloat32x2, .offset = 0},
            mrhiVertexAttribute{.buffer = 0, .location = 1, .format = mrhi_vertexFloat32x2, .offset = 8},
            mrhiVertexAttribute{.buffer = 0, .location = 2, .format = mrhi_vertexUnorm8x4, .offset = 16}};
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.canvas.sprites";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = shader;
        def.vertexEntry = "vs";
        def.vertexEntryLength = 2;
        def.fragmentEntry = "fs";
        def.fragmentEntryLength = 2;
        def.vertexBuffers = kBuffers.data();
        def.vertexBufferCount = static_cast<std::uint32_t>(kBuffers.size());
        def.vertexAttributes = kAttributes.data();
        def.vertexAttributeCount = static_cast<std::uint32_t>(kAttributes.size());
        def.colorTargetCount = 1;
        // Premultiplied, in linear light: over what is behind, added to it,
        // and multiplying it, its alpha left as it was (D356).
        def.colorTargets[0].format = mrhi_formatRgba8UnormSrgb;
        def.colorTargets[0].blend = true;
        const std::array<std::array<mrhiBlendComponent, 2>, 3> kBlends = {{
            {mrhiBlendComponent{
                 .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd},
             mrhiBlendComponent{
                 .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd}},
            {mrhiBlendComponent{.srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd},
             mrhiBlendComponent{.srcFactor = mrhi_blendZero, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd}},
            {mrhiBlendComponent{.srcFactor = mrhi_blendDst, .dstFactor = mrhi_blendZero, .operation = mrhi_blendAdd},
             mrhiBlendComponent{.srcFactor = mrhi_blendZero, .dstFactor = mrhi_blendOne, .operation = mrhi_blendAdd}},
        }};
        for (std::size_t blend = 0; blend < kBlends.size(); ++blend) {
            def.colorTargets[0].color = kBlends.at(blend)[0];
            def.colorTargets[0].alpha = kBlends.at(blend)[1];
            mrhiRequestId request{};
            if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &pipelines.at(blend), &request);
                kMade != mrhi_success) {
                return failed("a sprite pipeline could not be asked for", kMade);
            }
            pipelineRequests.at(blend) = render::requestKey(request.index1, request.generation);
        }
        for (auto [filter, sampler] :
             {std::pair{mrhi_filterNearest, &nearest}, std::pair{mrhi_filterLinear, &linear}}) {
            mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
            samplerDef.magFilter = filter;
            samplerDef.minFilter = filter;
            samplerDef.mipFilter = filter;
            samplerDef.addressU = mrhi_addressClampToEdge;
            samplerDef.addressV = mrhi_addressClampToEdge;
            samplerDef.addressW = mrhi_addressClampToEdge;
            if (const mrhiResult kMade = mrhiCreateSampler(native, &samplerDef, sampler); kMade != mrhi_success) {
                return failed("a sampler could not be made", kMade);
            }
        }
        for (const material::Filter kFilter : {material::Filter::Linear, material::Filter::Nearest}) {
            for (const material::Address kAddress : {material::Address::Repeat, material::Address::Clamp}) {
                mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
                const mrhiFilter kHow = kFilter == material::Filter::Linear ? mrhi_filterLinear : mrhi_filterNearest;
                samplerDef.magFilter = kHow;
                samplerDef.minFilter = kHow;
                samplerDef.mipFilter = kHow;
                const mrhiAddressMode kAt =
                    kAddress == material::Address::Repeat ? mrhi_addressRepeat : mrhi_addressClampToEdge;
                samplerDef.addressU = kAt;
                samplerDef.addressV = kAt;
                samplerDef.addressW = kAt;
                if (const mrhiResult kMade =
                        mrhiCreateSampler(native, &samplerDef, &materialSamplers.at(samplerOf(kFilter, kAddress)));
                    kMade != mrhi_success) {
                    return failed("a material's sampler could not be made", kMade);
                }
            }
        }
        return {};
    }

    /// Where a material's filter and address put its sampler.
    static std::size_t samplerOf(material::Filter filter, material::Address address) noexcept {
        return (static_cast<std::size_t>(filter) * 2) + static_cast<std::size_t>(address);
    }

    /// Chooses the textures this frame draws from; a draw whose texture
    /// is not chosen is left out.
    void texturesOf(const render_canvas::CanvasFrame& canvas, const TextureSource& source) {
        held->begin(limits.uploadBytesPerFrame);
        static_cast<void>(held->choose(0, white));
        for (const render_canvas::CanvasDraw& draw : canvas.draws) {
            for (const std::uint64_t kId : {draw.texture, materialOf(canvas, draw).sampled.id}) {
                if (kId != 0) {
                    static_cast<void>(held->choose(kId, source ? source(kId) : nullptr));
                }
            }
        }
    }

    result::Status declare(render::Frame& open) {
        declared = false;
        if (frame == nullptr) {
            return {};
        }
        device->pump();
        if (!pipelineReady) {
            bool all = true;
            for (std::uint64_t& request : pipelineRequests) {
                if (request == 0) {
                    continue;
                }
                if (const auto kAnswer = device->answer(request)) {
                    if (!kAnswer->has_value()) {
                        return std::unexpected<result::Error>{kAnswer->error().clone()};
                    }
                    request = 0;
                } else {
                    all = false;
                }
            }
            pipelineReady = all;
        }
        if (!pipelineReady) {
            ++statistics.framesWaiting;
            return {};
        }
        texturesOf(*frame, textures);
        // Everything this frame uses: the textures it draws from, imported,
        // and this frame's corners and indices.
        RAWFRAME_TRY(held->import());
        const bool kQuads = !frame->indices.empty();
        if (kQuads) {
            mrhiBufferDef cornersDef = mrhiDefaultBufferDef();
            cornersDef.size = frame->vertices.size() * kVertexBytes;
            mrhiBufferDef indicesDef = mrhiDefaultBufferDef();
            indicesDef.size = frame->indices.size() * sizeof(std::uint32_t);
            if (mrhiDeclareBuffer(native, &cornersDef, &corners_) != mrhi_success ||
                mrhiDeclareBuffer(native, &indicesDef, &indices_) != mrhi_success) {
                return failed("the frame's corners could not be declared", mrhi_errorCapacity);
            }
            // Each of the game's materials' blocks, at a stride every
            // device's uniform offsets allow (D356).
            const std::size_t kMaterials = std::max<std::size_t>(frame->materials.size(), 1);
            blocks.assign(kMaterials * kBlockStride, 0);
            for (std::size_t at = 0; at < kMaterials; ++at) {
                const MaterialBlock kBlock = blockOf(at < frame->materials.size() ? frame->materials[at] : kPlain);
                std::memcpy(blocks.data() + (at * kBlockStride), &kBlock, sizeof(kBlock));
            }
            mrhiBufferDef blocksDef = mrhiDefaultBufferDef();
            blocksDef.size = blocks.size();
            if (mrhiDeclareBuffer(native, &blocksDef, &blocksResource) != mrhi_success) {
                return failed("the frame's materials could not be declared", mrhi_errorCapacity);
            }
        }
        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes;
        if (kQuads) {
            writes.push_back(wholeOf(corners_, mrhi_accessCopyDestination));
            writes.push_back(wholeOf(indices_, mrhi_accessCopyDestination));
            writes.push_back(wholeOf(blocksResource, mrhi_accessCopyDestination));
        }
        for (const std::uint64_t kTexture : held->uploading()) {
            writes.push_back(wholeOf(resourceOf(kTexture), mrhi_accessCopyDestination));
        }
        upload.reset();
        if (!writes.empty()) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.passClass = mrhi_passTransfer;
            def.accesses = writes.data();
            def.accessCount = static_cast<std::uint32_t>(writes.size());
            mrhiPassId made{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &made); kAdded != mrhi_success) {
                return failed("the upload pass could not be added", kAdded);
            }
            upload = made;
        }
        std::vector<mrhiAccess> reads;
        if (kQuads) {
            reads.push_back(wholeOf(corners_, mrhi_accessVertex));
            reads.push_back(wholeOf(indices_, mrhi_accessIndex));
            reads.push_back(wholeOf(blocksResource, mrhi_accessUniform));
        }
        for (const std::uint64_t kTexture : held->chosen()) {
            reads.push_back(wholeOf(resourceOf(kTexture), mrhi_accessSampled));
        }
        mrhiPassDef drawDef = mrhiDefaultPassDef();
        drawDef.colorTargets[0].resource = resourceOf(open.picture);
        drawDef.colorTargets[0].load = open.clearsPicture() ? mrhi_loadClear : mrhi_loadKeep;
        drawDef.colorTargets[0].store = mrhi_storeKeep;
        drawDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        drawDef.colorTargetCount = 1;
        drawDef.accesses = reads.data();
        drawDef.accessCount = static_cast<std::uint32_t>(reads.size());
        drawDef.neverCull = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &drawDef, &drawing); kAdded != mrhi_success) {
            return failed("the drawing pass could not be added", kAdded);
        }
        declared = true;
        return {};
    }

    result::Status record() {
        if (!declared) {
            return {};
        }
        const bool kQuads = !frame->indices.empty();
        if (upload.has_value()) {
            if (mrhiBeginPass(native, *upload) != mrhi_success) {
                return failed("the upload pass could not begin", mrhi_errorState);
            }
            if (kQuads &&
                (mrhiWriteBuffer(
                     native, *upload, corners_, 0, frame->vertices.data(), frame->vertices.size() * kVertexBytes) !=
                     mrhi_success ||
                 mrhiWriteBuffer(native,
                                 *upload,
                                 indices_,
                                 0,
                                 frame->indices.data(),
                                 frame->indices.size() * sizeof(std::uint32_t)) != mrhi_success ||
                 mrhiWriteBuffer(native, *upload, blocksResource, 0, blocks.data(), blocks.size()) != mrhi_success)) {
                return failed("the frame's corners could not be written", mrhi_errorCapacity);
            }
            RAWFRAME_TRY(held->write(render::requestKey(upload->index1, upload->generation)));
            if (mrhiEndPass(native, *upload) != mrhi_success) {
                return failed("the upload pass could not end", mrhi_errorState);
            }
        }
        if (mrhiBeginPass(native, drawing) != mrhi_success) {
            return failed("the drawing pass could not begin", mrhi_errorState);
        }
        drawn = 0;
        std::uint64_t leftOut = 0;
        if (kQuads) {
            if (mrhiSetVertexBuffer(native, drawing, 0, corners_, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                mrhiSetIndexBuffer(native, drawing, indices_, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                return failed("the drawing could not be set up", mrhi_errorState);
            }
            std::optional<material::CanvasBlend> blending;
            for (const render_canvas::CanvasDraw& draw : frame->draws) {
                const material::CanvasMaterial& kMaterial = materialOf(*frame, draw);
                const std::uint64_t kTexture = held->resource(draw.texture);
                const std::uint64_t kSampled = held->resource(kMaterial.sampled.id);
                if (kTexture == 0 || kSampled == 0) {
                    ++leftOut;
                    continue;
                }
                if (blending != kMaterial.blend) {
                    if (mrhiSetGraphicsPipeline(
                            native, drawing, pipelines.at(static_cast<std::size_t>(kMaterial.blend))) != mrhi_success) {
                        return failed("the drawing could not be set up", mrhi_errorState);
                    }
                    blending = kMaterial.blend;
                }
                mrhiBinding block = samplerAt(2, {});
                block.resource = blocksResource;
                block.offset = draw.material < frame->materials.size() ? draw.material * kBlockStride : 0;
                block.size = sizeof(MaterialBlock);
                const std::array<mrhiBinding, 5> kBindings = {
                    textureAt(0, resourceOf(kTexture)),
                    samplerAt(1, held->compressed(draw.texture) ? linear : nearest),
                    block,
                    textureAt(3, resourceOf(kSampled)),
                    samplerAt(4, materialSamplers.at(samplerOf(kMaterial.sampled.filter, kMaterial.sampled.address)))};
                if (mrhiSetBindings(native, drawing, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
                    mrhiDrawIndexed(native, drawing, draw.indexCount, 1, draw.firstIndex, 0, 0) != mrhi_success) {
                    return failed("a draw could not be recorded", mrhi_errorState);
                }
                ++drawn;
            }
        }
        if (mrhiEndPass(native, drawing) != mrhi_success) {
            return failed("the drawing pass could not end", mrhi_errorState);
        }
        leftOutNow = leftOut;
        return {};
    }

    void ended(bool submitted) noexcept {
        if (!std::exchange(declared, false)) {
            return;
        }
        held->ended(submitted);
        const render::TextureStatistics& kHeld = held->statistics();
        statistics.texturesUploaded = kHeld.texturesUploaded;
        statistics.uploadBytes = kHeld.uploadBytes;
        statistics.uploadsDeferred = kHeld.uploadsDeferred;
        statistics.texturesReplaced = kHeld.texturesReplaced;
        if (!submitted) {
            return;
        }
        ++statistics.frames;
        statistics.draws += drawn;
        statistics.drawsLeftOut += leftOutNow;
    }

    std::uint64_t leftOutNow = 0;
};

CanvasRenderer::CanvasRenderer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

CanvasRenderer::~CanvasRenderer() = default;

result::Result<std::unique_ptr<CanvasRenderer>> CanvasRenderer::create(render::Device& device, RendererLimits limits) {
    if (device.native() == nullptr) {
        return refuse(
            result::ErrorClass::FailedPrecondition, CanvasGpuError::State, "a canvas renderer needs a ready device");
    }
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    state->limits = limits;
    RAWFRAME_TRY_ASSIGN(state->held,
                        render::DeviceTextures::create(device, {.maximumTextures = limits.maximumTextures + 1}));
    texture::Texture white{.format = texture::Format::Rgba8Srgb};
    white.levels.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{0xFF})});
    state->white = std::make_shared<const texture::Texture>(std::move(white));
    RAWFRAME_TRY(state->makePipeline());
    return std::unique_ptr<CanvasRenderer>{new CanvasRenderer{std::move(state)}};
}

void CanvasRenderer::prepare(const render_canvas::CanvasFrame* frame, TextureSource textures) {
    state_->frame = frame;
    state_->textures = std::move(textures);
}

result::Status CanvasRenderer::declare(render::Frame& frame) {
    return state_->declare(frame);
}

result::Status CanvasRenderer::record(render::Frame& /*frame*/) {
    return state_->record();
}

void CanvasRenderer::ended(bool submitted) noexcept {
    state_->ended(submitted);
}

const RendererStatistics& CanvasRenderer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render_canvas_gpu
