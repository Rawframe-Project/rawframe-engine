#include "rawframe/render_canvas_gpu/renderer.h"

#include "generated/sprite_container.h"
#include "rawframe/render/textures.h"
#include "rawframe/render_canvas_gpu/errors.h"

#include <algorithm>
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
    RendererStatistics statistics;
    mrhiShaderId shader{};
    mrhiGraphicsPipelineId pipeline{};
    std::uint64_t pipelineRequest = 0;
    bool pipelineReady = false;
    /// Exact textures (pixel art, interface images) are sampled nearest;
    /// block-compressed ones linearly, across their levels.
    mrhiSamplerId nearest{};
    mrhiSamplerId linear{};
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
        static_cast<void>(mrhiDestroySampler(native, nearest));
        static_cast<void>(mrhiDestroySampler(native, linear));
        static_cast<void>(mrhiDestroyGraphicsPipeline(native, pipeline));
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
        // Straight alpha over what is behind, in linear light.
        def.colorTargets[0].format = mrhi_formatRgba8UnormSrgb;
        def.colorTargets[0].blend = true;
        def.colorTargets[0].color = {
            .srcFactor = mrhi_blendSrcAlpha, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
        def.colorTargets[0].alpha = {
            .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
        mrhiRequestId request{};
        if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &pipeline, &request);
            kMade != mrhi_success) {
            return failed("the sprite pipeline could not be asked for", kMade);
        }
        pipelineRequest = render::requestKey(request.index1, request.generation);
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
        return {};
    }

    /// Chooses the textures this frame draws from; a draw whose texture
    /// is not chosen is left out.
    void texturesOf(const render_canvas::CanvasFrame& canvas, const TextureSource& source) {
        held->begin();
        for (const render_canvas::CanvasDraw& draw : canvas.draws) {
            static_cast<void>(held->choose(draw.texture, source ? source(draw.texture) : nullptr));
        }
    }

    result::Status declare(render::Frame& open) {
        declared = false;
        if (frame == nullptr) {
            return {};
        }
        device->pump();
        if (!pipelineReady) {
            if (const auto kAnswer = device->answer(pipelineRequest)) {
                if (!kAnswer->has_value()) {
                    return std::unexpected<result::Error>{kAnswer->error().clone()};
                }
                pipelineReady = true;
            }
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
        }
        // The upload pass writes what the drawing reads.
        std::vector<mrhiAccess> writes;
        if (kQuads) {
            writes.push_back(wholeOf(corners_, mrhi_accessCopyDestination));
            writes.push_back(wholeOf(indices_, mrhi_accessCopyDestination));
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
                                 frame->indices.size() * sizeof(std::uint32_t)) != mrhi_success)) {
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
            if (mrhiSetGraphicsPipeline(native, drawing, pipeline) != mrhi_success ||
                mrhiSetVertexBuffer(native, drawing, 0, corners_, 0, MRHI_WHOLE_SIZE) != mrhi_success ||
                mrhiSetIndexBuffer(native, drawing, indices_, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) != mrhi_success) {
                return failed("the drawing could not be set up", mrhi_errorState);
            }
            for (const render_canvas::CanvasDraw& draw : frame->draws) {
                const std::uint64_t kTexture = held->resource(draw.texture);
                if (kTexture == 0) {
                    ++leftOut;
                    continue;
                }
                const std::array<mrhiBinding, 2> kBindings = {
                    mrhiBinding{
                        .slot = 0,
                        .resource = resourceOf(kTexture),
                        .offset = 0,
                        .size = 0,
                        .viewKind = mrhi_texture2d,
                        .viewFormat = mrhi_formatNone,
                        .range =
                            {.baseMip = 0, .mipCount = MRHI_REMAINING, .baseLayer = 0, .layerCount = 1, .aspect = {}},
                        .sampler = {}},
                    mrhiBinding{.slot = 1,
                                .resource = {},
                                .offset = 0,
                                .size = 0,
                                .viewKind = mrhi_texture2d,
                                .viewFormat = mrhi_formatNone,
                                .range = {},
                                .sampler = held->compressed(draw.texture) ? linear : nearest}};
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
    RAWFRAME_TRY_ASSIGN(state->held, render::DeviceTextures::create(device, limits));
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
