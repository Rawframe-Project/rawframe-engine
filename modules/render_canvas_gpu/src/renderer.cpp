#include "rawframe/render_canvas_gpu/renderer.h"

#include "generated/sprite_container.h"
#include "rawframe/particles_gpu/particles.h"
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
/// What the canvas tells a frame it cleared, as the views' ground (D369).
constexpr char kCanvasGround = 0;

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

/// A canvas material as the particles draw it (D357): its color and
/// emission in the same terms, its one texture for both, its texture's
/// alpha shaping it where that adds to its alpha, and its blend.
particles_gpu::Material particleMaterialOf(const material::CanvasMaterial& made) noexcept {
    const bool kMultiplies = made.blend == material::CanvasBlend::Multiply;
    const std::array<float, 4> kMap = {
        made.sampled.scale[0], made.sampled.scale[1], made.sampled.offset[0], made.sampled.offset[1]};
    return particles_gpu::Material{
        .block = {.color = made.color,
                  .colorTexture = made.colorTexture,
                  .emission = {made.emission[0], made.emission[1], made.emission[2], 0},
                  .emissionTexture = {made.emissionTexture[0], made.emissionTexture[1], made.emissionTexture[2], 0},
                  .baseMap = kMap,
                  .emissionMap = kMap,
                  .flags = {(made.colorTexture[3] != 0 ? particles_gpu::kShapedByTexture : 0U) |
                                (kMultiplies ? particles_gpu::kMultiplies : 0U),
                            0,
                            0,
                            0}},
        .blend = made.blend == material::CanvasBlend::Additive ? particles_gpu::Blend::Add
                 : kMultiplies                                 ? particles_gpu::Blend::Multiply
                                                               : particles_gpu::Blend::Over};
}

/// The canvas as the particles see it (D357): flat, `extent` meters each
/// way from its middle, its depth a constant the clip keeps; the particle
/// clock `clock`.
particles_gpu::ViewBlock particleViewOf(const std::array<float, 2>& extent, float clock) noexcept {
    particles_gpu::ViewBlock view{
        .right = {1, 0, 0, clock}, .up = {0, 1, 0, particles::kClockPeriod}, .lens = {0, 1, 0, 0}};
    view.viewProjection[0] = extent[0] > 0 ? 1 / extent[0] : 0;
    view.viewProjection[5] = extent[1] > 0 ? 1 / extent[1] : 0;
    view.viewProjection[14] = 0.5F;
    view.viewProjection[15] = 1;
    return view;
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
    /// The particles, trails, and beams (D357): the device half the scene's
    /// share.
    std::unique_ptr<particles_gpu::Particles> particles;
    /// The open frame's materials' blocks.
    std::vector<std::uint8_t> blocks;
    mrhiResourceId blocksResource{};
    std::unique_ptr<render::DeviceTextures> held;
    /// What the next frame draws.
    const render_canvas::CanvasFrame* frame = nullptr;
    TextureSource textures;
    std::optional<std::array<std::uint32_t, 4>> region;
    /// What the picture is cleared to, 8-bit sRGB (D369).
    std::array<std::uint8_t, 3> bars{};
    /// What the open frame declared, until it is recorded and ends: and,
    /// where the canvas is the ground of a region between bars (D369), the
    /// passes clearing the picture to the bars, a picture the region's size
    /// to black, and copying that into the region.
    bool declared = false;
    std::optional<mrhiPassId> barsClearing;
    std::optional<mrhiPassId> groundClearing;
    std::optional<mrhiPassId> groundCopying;
    mrhiResourceId ground{};
    mrhiResourceId groundOf{};
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
        // The particles' and ribbons' materials' textures (D357).
        std::vector<std::uint32_t> shown;
        for (const particles::EmitterDraw& kEmitter : canvas.particles.emitters) {
            shown.push_back(kEmitter.material);
        }
        for (const particles::Ribbon& kRibbon : canvas.particles.ribbons) {
            shown.push_back(kRibbon.material);
        }
        for (const std::uint32_t kMaterial : shown) {
            const std::uint64_t kId = kMaterial < canvas.materials.size() ? canvas.materials[kMaterial].sampled.id : 0;
            if (kId != 0) {
                static_cast<void>(held->choose(kId, source ? source(kId) : nullptr));
            }
        }
    }

    /// The frame's materials as the particles draw them (D357), none where
    /// it holds none: each one's texture as held this frame, white for none
    /// and for one not held.
    [[nodiscard]] std::vector<particles_gpu::Material> particleMaterials() const {
        std::vector<particles_gpu::Material> made;
        const std::span<const material::CanvasMaterial> kMaterials =
            frame->materials.empty() ? std::span<const material::CanvasMaterial>{&kPlain, 1} : frame->materials;
        made.reserve(kMaterials.size());
        for (const material::CanvasMaterial& kMaterial : kMaterials) {
            const std::uint64_t kHeld = held->resource(kMaterial.sampled.id);
            const mrhiSamplerId kSampler =
                materialSamplers.at(samplerOf(kMaterial.sampled.filter, kMaterial.sampled.address));
            const particles_gpu::BoundTexture kBound{.texture = kHeld != 0 ? kHeld : held->resource(0),
                                                     .sampler =
                                                         render::requestKey(kSampler.index1, kSampler.generation)};
            particles_gpu::Material each = particleMaterialOf(kMaterial);
            each.color = kBound;
            each.emission = kBound;
            made.push_back(each);
        }
        return made;
    }

    result::Status declare(render::Frame& open) {
        declared = false;
        if (frame == nullptr) {
            return {};
        }
        // A region as far as the frame reaches: the view's size follows the
        // frame's a frame late, so a region may pass it once (D364).
        if (region.has_value()) {
            auto& [x, y, width, height] = *region;
            x = std::min(x, open.width);
            y = std::min(y, open.height);
            width = std::min(width, open.width - x);
            height = std::min(height, open.height - y);
            if (width == 0 || height == 0) {
                return {};
            }
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
        const bool kClears = open.clearsPicture();
        if (kClears) {
            open.ground = &kCanvasGround;
        }
        RAWFRAME_TRY(declareGround(open, kClears));
        mrhiPassDef drawDef = mrhiDefaultPassDef();
        drawDef.colorTargets[0].resource = resourceOf(open.picture);
        drawDef.colorTargets[0].load = kClears && !barsClearing.has_value() ? mrhi_loadClear : mrhi_loadKeep;
        drawDef.colorTargets[0].store = mrhi_storeKeep;
        drawDef.colorTargets[0].clear = mrhiClearColor{.red = render::linearOf(bars[0]),
                                                       .green = render::linearOf(bars[1]),
                                                       .blue = render::linearOf(bars[2]),
                                                       .alpha = 1};
        drawDef.colorTargetCount = 1;
        drawDef.accesses = reads.data();
        drawDef.accessCount = static_cast<std::uint32_t>(reads.size());
        drawDef.neverCull = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &drawDef, &drawing); kAdded != mrhi_success) {
            return failed("the drawing pass could not be added", kAdded);
        }
        // The particles, trails, and beams over every sprite (D357).
        RAWFRAME_TRY(particles->declare(
            frame->particles,
            particleMaterials(),
            particleViewOf(frame->extent, frame->particles.clock),
            {.picture = open.picture, .exposure = std::nullopt, .depth = std::nullopt, .region = region}));
        declared = true;
        return {};
    }

    /// A canvas region of a picture whose ground the canvas is, between
    /// bars of a color (D369): the picture cleared to the bars first if
    /// this clears it, then the region cleared black, through a picture of
    /// its own (a pass clears a whole target), as the whole picture is
    /// without bars.
    result::Status declareGround(render::Frame& open, bool clears) {
        barsClearing.reset();
        groundClearing.reset();
        groundCopying.reset();
        if (!region.has_value() || open.ground != &kCanvasGround || bars == std::array<std::uint8_t, 3>{}) {
            return {};
        }
        if (clears) {
            mrhiPassDef def = mrhiDefaultPassDef();
            def.colorTargets[0].resource = resourceOf(open.picture);
            def.colorTargets[0].load = mrhi_loadClear;
            def.colorTargets[0].store = mrhi_storeKeep;
            def.colorTargets[0].clear = mrhiClearColor{.red = render::linearOf(bars[0]),
                                                       .green = render::linearOf(bars[1]),
                                                       .blue = render::linearOf(bars[2]),
                                                       .alpha = 1};
            def.colorTargetCount = 1;
            def.neverCull = true;
            mrhiPassId made{};
            if (const mrhiResult kAdded = mrhiAddPass(native, &def, &made); kAdded != mrhi_success) {
                return failed("the bars could not be cleared", kAdded);
            }
            barsClearing = made;
        }
        const auto& [kX, kY, kWidth, kHeight] = *region;
        mrhiTextureDef groundDef = mrhiDefaultTextureDef();
        groundDef.format = mrhi_formatRgba8UnormSrgb;
        groundDef.width = kWidth;
        groundDef.height = kHeight;
        if (const mrhiResult kDeclared = mrhiDeclareTexture(native, &groundDef, &ground); kDeclared != mrhi_success) {
            return failed("a region's ground could not be declared", kDeclared);
        }
        mrhiPassDef clearDef = mrhiDefaultPassDef();
        clearDef.colorTargets[0].resource = ground;
        clearDef.colorTargets[0].load = mrhi_loadClear;
        clearDef.colorTargets[0].store = mrhi_storeKeep;
        clearDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        clearDef.colorTargetCount = 1;
        mrhiPassId cleared{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &clearDef, &cleared); kAdded != mrhi_success) {
            return failed("a region's ground could not be cleared", kAdded);
        }
        groundClearing = cleared;
        const std::array<mrhiAccess, 2> kAccesses = {wholeOf(ground, mrhi_accessCopySource),
                                                     wholeOf(resourceOf(open.picture), mrhi_accessCopyDestination)};
        mrhiPassDef copyDef = mrhiDefaultPassDef();
        copyDef.passClass = mrhi_passTransfer;
        copyDef.accesses = kAccesses.data();
        copyDef.accessCount = static_cast<std::uint32_t>(kAccesses.size());
        copyDef.neverCull = true;
        mrhiPassId copied{};
        if (const mrhiResult kAdded = mrhiAddPass(native, &copyDef, &copied); kAdded != mrhi_success) {
            return failed("a region's ground could not be laid", kAdded);
        }
        groundCopying = copied;
        groundOf = resourceOf(open.picture);
        return {};
    }

    result::Status recordGround() {
        for (const std::optional<mrhiPassId>& kClearing : {barsClearing, groundClearing}) {
            if (kClearing.has_value() && (mrhiBeginPass(native, *kClearing) != mrhi_success ||
                                          mrhiEndPass(native, *kClearing) != mrhi_success)) {
                return failed("the bars or a region's ground could not be cleared", mrhi_errorState);
            }
        }
        if (groundCopying.has_value()) {
            const auto& [kX, kY, kWidth, kHeight] = *region;
            const mrhiTextureCopy kFrom{.resource = ground};
            const mrhiTextureCopy kTo{.resource = groundOf, .x = kX, .y = kY};
            const mrhiExtent3d kExtent{.width = kWidth, .height = kHeight, .depthOrLayers = 1};
            if (mrhiBeginPass(native, *groundCopying) != mrhi_success ||
                mrhiCopyTexture(native, *groundCopying, &kFrom, &kTo, &kExtent) != mrhi_success ||
                mrhiEndPass(native, *groundCopying) != mrhi_success) {
                return failed("a region's ground could not be laid", mrhi_errorState);
            }
        }
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
        RAWFRAME_TRY(recordGround());
        if (mrhiBeginPass(native, drawing) != mrhi_success) {
            return failed("the drawing pass could not begin", mrhi_errorState);
        }
        // A local player's region in split-screen (D364): its draws land
        // there and nowhere else.
        if (region.has_value()) {
            const auto& [kX, kY, kWidth, kHeight] = *region;
            const mrhiViewport kViewport{.x = static_cast<float>(kX),
                                         .y = static_cast<float>(kY),
                                         .width = static_cast<float>(kWidth),
                                         .height = static_cast<float>(kHeight),
                                         .minDepth = 0,
                                         .maxDepth = 1};
            const mrhiScissorRect kScissor{.x = kX, .y = kY, .width = kWidth, .height = kHeight};
            if (mrhiSetViewport(native, drawing, &kViewport) != mrhi_success ||
                mrhiSetScissor(native, drawing, &kScissor) != mrhi_success) {
                return failed("the drawing's region could not be set", mrhi_errorState);
            }
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
        RAWFRAME_TRY(particles->record());
        leftOutNow = leftOut;
        return {};
    }

    void ended(bool submitted) noexcept {
        if (!std::exchange(declared, false)) {
            return;
        }
        held->ended(submitted);
        particles->ended(submitted);
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
        statistics.emittersDrawn += particles->emittersDrawn();
        statistics.emittersLeftOut += particles->emittersLeftOut();
        statistics.particlesSpawned += particles->spawned();
        statistics.ribbonsDrawn += particles->ribbonsDrawn();
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
    RAWFRAME_TRY_ASSIGN(
        state->particles,
        particles_gpu::Particles::create(device, particles_gpu::Target::Picture, limits.maximumParticles));
    return std::unique_ptr<CanvasRenderer>{new CanvasRenderer{std::move(state)}};
}

void CanvasRenderer::prepare(const render_canvas::CanvasFrame* frame,
                             TextureSource textures,
                             std::optional<std::array<std::uint32_t, 4>> region,
                             std::array<std::uint8_t, 3> bars) {
    state_->frame = frame;
    state_->textures = std::move(textures);
    state_->region = region;
    state_->bars = bars;
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
