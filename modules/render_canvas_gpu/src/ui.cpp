#include "rawframe/render_canvas_gpu/ui.h"

#include "generated/ui_container.h"
#include "rawframe/render/errors.h"

#include <array>
#include <cstring>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::render_canvas_gpu {

namespace {

std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, render::kRenderDomain, code(render::RenderError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

mrhiResourceId resourceOf(std::uint64_t key) noexcept {
    return mrhiResourceId{.index1 = static_cast<std::uint32_t>(key >> 32U),
                          .generation = static_cast<std::uint32_t>(key)};
}

mrhiAccess wholeOf(mrhiResourceId resource, mrhiAccessKind kind) noexcept {
    return mrhiAccess{.resource = resource,
                      .kind = kind,
                      .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
}

mrhiBinding bufferAt(std::uint32_t slot, mrhiResourceId resource, std::uint64_t size) noexcept {
    return mrhiBinding{.slot = slot,
                       .resource = resource,
                       .offset = 0,
                       .size = size,
                       .viewKind = mrhi_texture2d,
                       .viewFormat = mrhi_formatNone,
                       .range = {},
                       .sampler = {}};
}

/// A box as the shader reads it (ui.vert): eleven vectors of four.
constexpr std::size_t kBoxFloats = 44;

/// `box`, in the shader's layout and the picture's pixels, its clip
/// resolved from `list`.
std::array<float, kBoxFloats> blockOf(const ui::Box& box, const ui::DrawList& list) noexcept {
    std::array<float, kBoxFloats> block{};
    std::size_t at = 0;
    const auto kPut = [&](const std::array<float, 4>& values, float scale) {
        for (const float kValue : values) {
            block[at++] = kValue * scale;
        }
    };
    const float kScale = list.scale;
    kPut({box.rect.x, box.rect.y, box.rect.width, box.rect.height}, kScale);
    kPut(box.radii, kScale);
    kPut(box.fill, 1);
    kPut(box.borderWidths, kScale);
    for (const std::array<float, 4>& kColor : box.borderColors) {
        kPut(kColor, 1);
    }
    if (box.clip != 0 && box.clip < list.clips.size()) {
        const ui::Clip& kClip = list.clips[box.clip];
        kPut({kClip.rect.x, kClip.rect.y, kClip.rect.width, kClip.rect.height}, kScale);
        kPut(kClip.radii, kScale);
        kPut({1, kClip.invert ? 1.0F : 0.0F, 0, 0}, 1);
    }
    return block;
}

} // namespace

struct UiRenderer::State {
    render::Device* device = nullptr;
    mrhiDevice* native = nullptr;
    mrhiShaderId shader{};
    mrhiGraphicsPipelineId pipeline{};
    std::uint64_t request = 0;
    bool ready = false;
    UiStatistics statistics;
    const ui::DrawList* list = nullptr;
    /// What the open frame declared, until it is recorded and ends.
    bool declared = false;
    std::vector<float> blocks;
    std::array<float, 4> view{};
    mrhiResourceId viewResource{};
    mrhiResourceId boxesResource{};
    mrhiPassId upload{};
    mrhiPassId drawing{};

    ~State() {
        if (native == nullptr) {
            return;
        }
        // Maul RHI retires what a frame still uses once the frame is done.
        static_cast<void>(mrhiDestroyGraphicsPipeline(native, pipeline));
        static_cast<void>(mrhiDestroyShader(native, shader));
    }

    result::Status make() {
        mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
        shaderDef.bytes = kUiContainer.data();
        shaderDef.byteCount = kUiContainer.size();
        if (const mrhiResult kMade = mrhiCreateShader(native, &shaderDef, &shader); kMade != mrhi_success) {
            return failed("the UI's box shader could not be made", kMade);
        }
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        constexpr std::string_view kLabel = "rawframe.ui.boxes";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        def.shader = shader;
        def.vertexEntry = "vs";
        def.vertexEntryLength = 2;
        def.fragmentEntry = "fs";
        def.fragmentEntryLength = 2;
        def.colorTargetCount = 1;
        // Premultiplied, over what is behind, in linear light.
        def.colorTargets[0].format = mrhi_formatRgba8UnormSrgb;
        def.colorTargets[0].blend = true;
        def.colorTargets[0].color = mrhiBlendComponent{
            .srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha, .operation = mrhi_blendAdd};
        def.colorTargets[0].alpha = def.colorTargets[0].color;
        mrhiRequestId asked{};
        if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &pipeline, &asked);
            kMade != mrhi_success) {
            return failed("the UI's box pipeline could not be asked for", kMade);
        }
        request = render::requestKey(asked.index1, asked.generation);
        return {};
    }

    result::Status declare(render::Frame& open) {
        declared = false;
        if (list == nullptr || list->boxes.empty()) {
            return {};
        }
        if (!ready) {
            device->pump();
            if (const auto kAnswer = device->answer(request)) {
                if (!kAnswer->has_value()) {
                    return std::unexpected<result::Error>{kAnswer->error().clone()};
                }
                ready = true;
            }
        }
        if (!ready) {
            return {};
        }
        blocks.clear();
        for (const ui::Box& kBox : list->boxes) {
            const std::array<float, kBoxFloats> kBlock = blockOf(kBox, *list);
            blocks.insert(blocks.end(), kBlock.begin(), kBlock.end());
        }
        view = {static_cast<float>(open.width), static_cast<float>(open.height), 0, 0};
        mrhiBufferDef viewDef = mrhiDefaultBufferDef();
        viewDef.size = sizeof(view);
        mrhiBufferDef boxesDef = mrhiDefaultBufferDef();
        boxesDef.size = blocks.size() * sizeof(float);
        if (mrhiDeclareBuffer(native, &viewDef, &viewResource) != mrhi_success ||
            mrhiDeclareBuffer(native, &boxesDef, &boxesResource) != mrhi_success) {
            return failed("the UI's boxes could not be declared", mrhi_errorCapacity);
        }
        const std::array<mrhiAccess, 2> kWrites = {wholeOf(viewResource, mrhi_accessCopyDestination),
                                                   wholeOf(boxesResource, mrhi_accessCopyDestination)};
        mrhiPassDef uploadDef = mrhiDefaultPassDef();
        uploadDef.passClass = mrhi_passTransfer;
        uploadDef.accesses = kWrites.data();
        uploadDef.accessCount = static_cast<std::uint32_t>(kWrites.size());
        if (const mrhiResult kAdded = mrhiAddPass(native, &uploadDef, &upload); kAdded != mrhi_success) {
            return failed("the UI's upload could not be added", kAdded);
        }
        const std::array<mrhiAccess, 2> kReads = {wholeOf(viewResource, mrhi_accessUniform),
                                                  wholeOf(boxesResource, mrhi_accessStorageRead)};
        mrhiPassDef drawDef = mrhiDefaultPassDef();
        drawDef.colorTargets[0].resource = resourceOf(open.picture);
        drawDef.colorTargets[0].load = open.clearsPicture() ? mrhi_loadClear : mrhi_loadKeep;
        drawDef.colorTargets[0].store = mrhi_storeKeep;
        drawDef.colorTargets[0].clear = mrhiClearColor{.red = 0, .green = 0, .blue = 0, .alpha = 1};
        drawDef.colorTargetCount = 1;
        drawDef.accesses = kReads.data();
        drawDef.accessCount = static_cast<std::uint32_t>(kReads.size());
        drawDef.neverCull = true;
        if (const mrhiResult kAdded = mrhiAddPass(native, &drawDef, &drawing); kAdded != mrhi_success) {
            return failed("the UI's drawing could not be added", kAdded);
        }
        declared = true;
        return {};
    }

    result::Status record() {
        if (!declared) {
            return {};
        }
        if (mrhiBeginPass(native, upload) != mrhi_success ||
            mrhiWriteBuffer(native, upload, viewResource, 0, view.data(), sizeof(view)) != mrhi_success ||
            mrhiWriteBuffer(native, upload, boxesResource, 0, blocks.data(), blocks.size() * sizeof(float)) !=
                mrhi_success ||
            mrhiEndPass(native, upload) != mrhi_success) {
            return failed("the UI's boxes could not be written", mrhi_errorCapacity);
        }
        const std::array<mrhiBinding, 2> kBindings = {bufferAt(0, viewResource, sizeof(view)),
                                                      bufferAt(1, boxesResource, blocks.size() * sizeof(float))};
        const auto kCount = static_cast<std::uint32_t>(blocks.size() / kBoxFloats);
        if (mrhiBeginPass(native, drawing) != mrhi_success ||
            mrhiSetGraphicsPipeline(native, drawing, pipeline) != mrhi_success ||
            mrhiSetBindings(native, drawing, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
            mrhiDraw(native, drawing, 6, kCount, 0, 0) != mrhi_success ||
            mrhiEndPass(native, drawing) != mrhi_success) {
            return failed("the UI's boxes could not be drawn", mrhi_errorState);
        }
        ++statistics.frames;
        statistics.boxes += kCount;
        return {};
    }
};

UiRenderer::UiRenderer(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

UiRenderer::~UiRenderer() = default;

result::Result<std::unique_ptr<UiRenderer>> UiRenderer::create(render::Device& device) {
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    RAWFRAME_TRY(state->make());
    return std::unique_ptr<UiRenderer>{new UiRenderer{std::move(state)}};
}

void UiRenderer::prepare(const ui::DrawList* list) noexcept {
    state_->list = list;
}

result::Status UiRenderer::declare(render::Frame& frame) {
    return state_->declare(frame);
}

result::Status UiRenderer::record(render::Frame& /*frame*/) {
    return state_->record();
}

void UiRenderer::ended(bool /*submitted*/) noexcept {
    state_->declared = false;
}

const UiStatistics& UiRenderer::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::render_canvas_gpu
