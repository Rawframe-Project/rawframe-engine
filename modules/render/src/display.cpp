#include "rawframe/render/display.h"

#include "generated/display_container.h"
#include "rawframe/render/errors.h"

#include <array>
#include <map>
#include <maul-rhi/encoder.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/pipeline.h>
#include <maul-rhi/resources.h>
#include <maul-rhi/shader.h>
#include <string>
#include <string_view>

namespace rawframe::render {

namespace {

std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kRenderDomain, code(RenderError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

/// The pipeline drawing into one surface format, and whether Maul RHI has
/// made it yet.
struct Pipeline {
    mrhiGraphicsPipelineId pipeline{};
    std::uint64_t request = 0;
    bool ready = false;
};

/// A pass `add` made, until it is recorded.
struct Pending {
    mrhiPassId pass{};
    mrhiResourceId picture{};
    mrhiGraphicsPipelineId pipeline{};
};

} // namespace

struct Display::State {
    Device* device = nullptr;
    mrhiDevice* native = nullptr;
    mrhiShaderId shader{};
    mrhiSamplerId sampler{};
    std::map<std::uint32_t, Pipeline> pipelines;
    std::map<std::uint64_t, Pending> pending;

    ~State() {
        for (const auto& [format, made] : pipelines) {
            static_cast<void>(mrhiDestroyGraphicsPipeline(native, made.pipeline));
        }
        static_cast<void>(mrhiDestroySampler(native, sampler));
        static_cast<void>(mrhiDestroyShader(native, shader));
    }

    /// The pipeline for `format`, asked for the first time it is needed;
    /// none until it is made.
    result::Result<std::optional<mrhiGraphicsPipelineId>> pipelineFor(std::uint32_t format) {
        auto found = pipelines.find(format);
        if (found == pipelines.end()) {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            constexpr std::string_view kLabel = "rawframe.render.display";
            def.label = kLabel.data();
            def.labelLength = kLabel.size();
            def.shader = shader;
            def.vertexEntry = "vs";
            def.vertexEntryLength = 2;
            def.fragmentEntry = "fs";
            def.fragmentEntryLength = 2;
            def.colorTargetCount = 1;
            def.colorTargets[0].format = static_cast<mrhiFormat>(format);
            Pipeline made;
            mrhiRequestId request{};
            if (const mrhiResult kMade = mrhiCreateGraphicsPipeline(native, &def, &made.pipeline, &request);
                kMade != mrhi_success) {
                return failed("the display pipeline could not be asked for", kMade);
            }
            made.request = requestKey(request.index1, request.generation);
            found = pipelines.emplace(format, made).first;
        }
        Pipeline& made = found->second;
        if (!made.ready) {
            device->pump();
            if (const auto kAnswer = device->answer(made.request)) {
                if (!kAnswer->has_value()) {
                    return std::unexpected<result::Error>{kAnswer->error().clone()};
                }
                made.ready = true;
            }
        }
        if (!made.ready) {
            return std::nullopt;
        }
        return made.pipeline;
    }
};

Display::Display(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Display::~Display() = default;

result::Result<std::unique_ptr<Display>> Display::create(Device& device) {
    if (device.native() == nullptr) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                           kRenderDomain,
                                                           code(RenderError::State),
                                                           "a display is made on a ready device")
                                                  .error()};
    }
    auto state = std::make_unique<State>();
    state->device = &device;
    state->native = device.native();
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = kDisplayContainer.data();
    shaderDef.byteCount = kDisplayContainer.size();
    if (const mrhiResult kMade = mrhiCreateShader(state->native, &shaderDef, &state->shader); kMade != mrhi_success) {
        return failed("the display shader could not be made", kMade);
    }
    mrhiSamplerDef samplerDef = mrhiDefaultSamplerDef();
    samplerDef.magFilter = mrhi_filterLinear;
    samplerDef.minFilter = mrhi_filterLinear;
    samplerDef.addressU = mrhi_addressClampToEdge;
    samplerDef.addressV = mrhi_addressClampToEdge;
    samplerDef.addressW = mrhi_addressClampToEdge;
    if (const mrhiResult kMade = mrhiCreateSampler(state->native, &samplerDef, &state->sampler);
        kMade != mrhi_success) {
        return failed("the display's sampler could not be made", kMade);
    }
    return std::unique_ptr<Display>{new Display{std::move(state)}};
}

result::Result<std::optional<std::uint64_t>> Display::add(std::uint64_t surface, std::uint64_t picture) {
    State& state = *state_;
    RAWFRAME_TRY_ASSIGN(const std::optional<mrhiGraphicsPipelineId> kPipeline,
                        state.pipelineFor(state.device->surfaceFormat(surface)));
    if (!kPipeline.has_value()) {
        return std::nullopt;
    }
    RAWFRAME_TRY_ASSIGN(const std::optional<std::uint64_t> kImage, state.device->acquire(surface));
    if (!kImage.has_value()) {
        return std::nullopt;
    }
    const mrhiResourceId kPicture{.index1 = static_cast<std::uint32_t>(picture >> 32U),
                                  .generation = static_cast<std::uint32_t>(picture)};
    const mrhiAccess kRead{.resource = kPicture,
                           .kind = mrhi_accessSampled,
                           .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0].resource = mrhiResourceId{.index1 = static_cast<std::uint32_t>(*kImage >> 32U),
                                                  .generation = static_cast<std::uint32_t>(*kImage)};
    def.colorTargets[0].load = mrhi_loadDiscard;
    def.colorTargets[0].store = mrhi_storeKeep;
    def.colorTargetCount = 1;
    def.accesses = &kRead;
    def.accessCount = 1;
    mrhiPassId pass{};
    if (const mrhiResult kAdded = mrhiAddPass(state.native, &def, &pass); kAdded != mrhi_success) {
        return failed("the display pass could not be added", kAdded);
    }
    const std::uint64_t kKey = requestKey(pass.index1, pass.generation);
    state.pending[kKey] = Pending{.pass = pass, .picture = kPicture, .pipeline = *kPipeline};
    return kKey;
}

result::Status Display::record(std::uint64_t pass) {
    State& state = *state_;
    const auto kFound = state.pending.find(pass);
    if (kFound == state.pending.end()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                           kRenderDomain,
                                                           code(RenderError::State),
                                                           "no such display pass in the frame")
                                                  .error()};
    }
    const Pending kPending = kFound->second;
    state.pending.erase(kFound);
    const std::array<mrhiBinding, 2> kBindings = {
        mrhiBinding{.slot = 0,
                    .resource = kPending.picture,
                    .offset = 0,
                    .size = 0,
                    .viewKind = mrhi_texture2d,
                    // The picture's bytes as they are: its linear twin.
                    .viewFormat = mrhi_formatRgba8Unorm,
                    .range = {.baseMip = 0, .mipCount = 1, .baseLayer = 0, .layerCount = 1, .aspect = {}},
                    .sampler = {}},
        mrhiBinding{.slot = 1,
                    .resource = {},
                    .offset = 0,
                    .size = 0,
                    .viewKind = mrhi_texture2d,
                    .viewFormat = mrhi_formatNone,
                    .range = {},
                    .sampler = state.sampler}};
    if (mrhiBeginPass(state.native, kPending.pass) != mrhi_success ||
        mrhiSetGraphicsPipeline(state.native, kPending.pass, kPending.pipeline) != mrhi_success ||
        mrhiSetBindings(state.native, kPending.pass, 0, kBindings.data(), kBindings.size()) != mrhi_success ||
        mrhiDraw(state.native, kPending.pass, 3, 1, 0, 0) != mrhi_success ||
        mrhiEndPass(state.native, kPending.pass) != mrhi_success) {
        return failed("the display pass could not be recorded", mrhi_errorState);
    }
    return {};
}

} // namespace rawframe::render
