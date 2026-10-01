#include "rawframe/render/device.h"

#include "rawframe/render/errors.h"

#include <array>
#include <map>
#include <maul-rhi/capabilities.h>
#include <maul-rhi/device.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/instance.h>
#include <maul-rhi/surface.h>
#include <span>
#include <string_view>
#include <variant>

namespace rawframe::render {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, RenderError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kRenderDomain, code(error), why).error()};
}

/// Maul RHI's refusal, named, as the device layer's failure.
std::unexpected<result::Error> failed(std::string_view why, mrhiResult outcome) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kRenderDomain, code(RenderError::Device), why)
            .error()
            .withContext("outcome", std::string{mrhiResultName(outcome)})};
}

enum class Phase : std::uint8_t {
    /// The instance is made; the adapters are asked for at the first open.
    Asking,
    /// The adapters were asked for.
    Finding,
    /// A device is opening on the adapter chosen.
    Opening,
    Ready,
    Failed,
};

/// 8-bit sRGB in Rec. 709 of standard range: what a surface is configured
/// in, which every surface that presents offers. The frame's picture is
/// written into it as its sRGB bytes.
bool standardColor(const mrhiSurfaceColor& color) noexcept {
    return (color.format == mrhi_formatRgba8Unorm || color.format == mrhi_formatBgra8Unorm) &&
           color.primaries == mrhi_primariesBt709 && color.transfer == mrhi_transferSrgb &&
           color.range == mrhi_rangeStandard;
}

/// A policy's present modes, first choice first; fifo ends every chain.
std::span<const mrhiPresentModes> chainOf(PresentPolicy policy) noexcept {
    static constexpr std::array<mrhiPresentModes, 1> kVsync = {mrhi_presentFifo};
    static constexpr std::array<mrhiPresentModes, 2> kLowLatency = {mrhi_presentMailbox, mrhi_presentFifo};
    static constexpr std::array<mrhiPresentModes, 3> kImmediate = {
        mrhi_presentImmediate, mrhi_presentMailbox, mrhi_presentFifo};
    switch (policy) {
    case PresentPolicy::LowLatencyVsync:
        return kLowLatency;
    case PresentPolicy::Immediate:
        return kImmediate;
    case PresentPolicy::Vsync:
    case PresentPolicy::AdaptiveVsync:
        // Maul RHI has no relaxed fifo: adaptive vsync is vsync.
        break;
    }
    return kVsync;
}

PresentPolicy policyOf(mrhiPresentModes mode) noexcept {
    switch (mode) {
    case mrhi_presentMailbox:
        return PresentPolicy::LowLatencyVsync;
    case mrhi_presentImmediate:
        return PresentPolicy::Immediate;
    default:
        return PresentPolicy::Vsync;
    }
}

/// Maul RHI's source for a window's handles, chained on `def`; the
/// source lives in `storage`.
struct Source {
    std::variant<std::monostate,
                 mrhiSurfaceSourceWin32,
                 mrhiSurfaceSourceWayland,
                 mrhiSurfaceSourceXcb,
                 mrhiSurfaceSourceAndroid,
                 mrhiSurfaceSourceMetalLayer,
                 mrhiSurfaceSourceCanvas>
        storage;
    std::string selector;

    /// The chain's head, or null for the test platform's windows, which
    /// have nothing to present to.
    const mrhiChain* chain(const window::HandleBundle& bundle) {
        if (const auto* win32 = std::get_if<window::Win32Handles>(&bundle.handles)) {
            auto& made = storage.emplace<mrhiSurfaceSourceWin32>();
            made.chain.type = mrhi_structSurfaceSourceWin32;
            made.hwnd = win32->window;
            made.hinstance = win32->instance;
            return &made.chain;
        }
        if (const auto* wayland = std::get_if<window::WaylandHandles>(&bundle.handles)) {
            auto& made = storage.emplace<mrhiSurfaceSourceWayland>();
            made.chain.type = mrhi_structSurfaceSourceWayland;
            made.display = wayland->display;
            made.surface = wayland->surface;
            return &made.chain;
        }
        if (const auto* xcb = std::get_if<window::XcbHandles>(&bundle.handles)) {
            auto& made = storage.emplace<mrhiSurfaceSourceXcb>();
            made.chain.type = mrhi_structSurfaceSourceXcb;
            made.connection = xcb->connection;
            made.window = xcb->window;
            return &made.chain;
        }
        if (const auto* android = std::get_if<window::AndroidHandles>(&bundle.handles)) {
            auto& made = storage.emplace<mrhiSurfaceSourceAndroid>();
            made.chain.type = mrhi_structSurfaceSourceAndroid;
            made.window = android->window;
            return &made.chain;
        }
        if (const auto* apple = std::get_if<window::AppleHandles>(&bundle.handles)) {
            auto& made = storage.emplace<mrhiSurfaceSourceMetalLayer>();
            made.chain.type = mrhi_structSurfaceSourceMetalLayer;
            made.layer = apple->layer;
            return &made.chain;
        }
        if (const auto* canvas = std::get_if<window::CanvasHandles>(&bundle.handles)) {
            selector = canvas->selector;
            auto& made = storage.emplace<mrhiSurfaceSourceCanvas>();
            made.chain.type = mrhi_structSurfaceSourceCanvas;
            made.selector = selector.data();
            made.selectorLength = selector.size();
            return &made.chain;
        }
        return nullptr;
    }
};

/// A window's surface on the device, and how it is configured.
struct Held {
    mrhiSurfaceId id{};
    bool configured = false;
    /// An image said the surface no longer fits its window.
    bool outOfDate = false;
    window::PixelSize size;
    PresentPolicy asked = PresentPolicy::Vsync;
    PresentPolicy used = PresentPolicy::Vsync;
    mrhiFormat format = mrhi_formatNone;
    /// The output modes the surface offers, its display's facts when they
    /// were read, and its HDR capability record (D365).
    std::array<bool, kOutputModes> offered{};
    std::optional<window::DisplayFacts> facts;
    OutputRecord output;
};

/// The output modes a surface's colors offer (ADR-0047's three).
std::array<bool, kOutputModes> offeredBy(const mrhiSurfaceCaps& caps) noexcept {
    std::array<bool, kOutputModes> offered{};
    for (std::uint32_t at = 0; at < caps.colorCount; ++at) {
        const mrhiSurfaceColor& kColor = caps.colors[at];
        if (standardColor(kColor)) {
            offered[static_cast<std::size_t>(OutputMode::SdrSrgb)] = true;
        } else if (kColor.format == mrhi_formatRgba16Float && kColor.primaries == mrhi_primariesBt709 &&
                   kColor.transfer == mrhi_transferLinear && kColor.range == mrhi_rangeExtended) {
            offered[static_cast<std::size_t>(OutputMode::HdrLinearFp16Rec709)] = true;
        } else if (kColor.format == mrhi_formatRgb10a2Unorm && kColor.primaries == mrhi_primariesBt2020 &&
                   kColor.transfer == mrhi_transferPq) {
            offered[static_cast<std::size_t>(OutputMode::Hdr10PqRec2020)] = true;
        }
    }
    return offered;
}

/// The modes the display pass draws: SDR only in generation 1 (D365).
constexpr std::array<OutputMode, 1> kDrawn = {OutputMode::SdrSrgb};

} // namespace

struct Device::State {
    DeviceSettings settings;
    mrhiInstance* instance = nullptr;
    mrhiDevice* device = nullptr;
    Phase phase = Phase::Asking;
    mrhiAdapterId chosen{};
    std::optional<AdapterDescription> adapter;
    std::map<std::uint64_t, Held> surfaces;
    /// The first surface associated before the adapters were asked for.
    std::optional<mrhiSurfaceId> compatible;
    std::optional<result::Error> failure;
    /// Answers not yet taken, by request, with their outcomes; at most the
    /// device's own notification bound, since each asker takes its own.
    std::map<std::uint64_t, mrhiResult> answers;
    bool lost = false;

    ~State() {
        // Surfaces, then the device, then the instance that made both.
        for (const auto& [key, surface] : surfaces) {
            static_cast<void>(mrhiDestroySurface(instance, surface.id));
        }
        mrhiDestroyDevice(device);
        mrhiDestroyInstance(instance);
    }

    result::Status fail(result::Error error) {
        phase = Phase::Failed;
        failure = error.clone();
        return std::unexpected<result::Error>{std::move(error)};
    }

    /// The adapters found: the first the settings allow, best first by
    /// Maul RHI's order, is opened.
    result::Status found(const mrhiInstanceNotification& record) {
        if (record.outcome != mrhi_success) {
            return fail(failed("the adapters could not be listed", record.outcome).error());
        }
        std::size_t count = 0;
        if (mrhiGetAdapters(instance, &chosen, 1, &count) != mrhi_success || count == 0) {
            return fail(refuse(result::ErrorClass::NotFound,
                               RenderError::NoAdapter,
                               settings.allowSoftware ? "no adapter answered"
                                                      : "no adapter answered that is not a software rasterizer")
                            .error());
        }
        mrhiAdapterInfo info{};
        if (const mrhiResult kRead = mrhiGetAdapterInfo(instance, chosen, &info); kRead != mrhi_success) {
            return fail(failed("the adapter could not be read", kRead).error());
        }
        mrhiFeatures features{};
        if (const mrhiResult kRead = mrhiGetAdapterFeatures(instance, chosen, &features); kRead != mrhi_success) {
            return fail(failed("the adapter's features could not be read", kRead).error());
        }
        adapter = AdapterDescription{.name = std::string{info.name, info.nameLength},
                                     .software = info.kind == mrhi_adapterSoftware,
                                     .blockCompression = features.textureCompressionBc};
        mrhiDeviceDef def = mrhiDefaultDeviceDef();
        def.adapter = chosen;
        def.features.textureCompressionBc = features.textureCompressionBc;
        def.deviceLimits.frameUploadBytes = kFrameUploadBytes;
        def.deviceLimits.readbackBytes = kReadbackBytes;
        constexpr std::string_view kLabel = "rawframe.render";
        def.label = kLabel.data();
        def.labelLength = kLabel.size();
        mrhiRequestId request{};
        if (const mrhiResult kMade = mrhiCreateDevice(instance, &def, &device, &request); kMade != mrhi_success) {
            device = nullptr;
            return fail(failed("the device could not be made", kMade).error());
        }
        phase = Phase::Opening;
        return {};
    }
};

Device::Device(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Device::~Device() = default;

result::Result<std::unique_ptr<Device>> Device::request(const DeviceSettings& settings) {
    auto state = std::make_unique<State>();
    state->settings = settings;
    const mrhiInstanceDef kInstance = mrhiDefaultInstanceDef();
    if (const mrhiResult kMade = mrhiCreateInstance(&kInstance, &state->instance); kMade != mrhi_success) {
        state->instance = nullptr;
        return failed("the device layer could not start", kMade);
    }
    return std::unique_ptr<Device>{new Device{std::move(state)}};
}

result::Result<bool> Device::open() {
    State& state = *state_;
    if (state.phase == Phase::Failed) {
        return std::unexpected<result::Error>{state.failure->clone()};
    }
    if (state.phase == Phase::Asking) {
        mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
        def.allowSoftware = state.settings.allowSoftware;
        if (state.compatible.has_value()) {
            def.compatibleSurface = *state.compatible;
        }
        mrhiRequestId request{};
        if (const mrhiResult kAsked = mrhiRequestAdapters(state.instance, &def, &request); kAsked != mrhi_success) {
            RAWFRAME_TRY(state.fail(failed("the adapters could not be asked for", kAsked).error()));
        }
        state.phase = Phase::Finding;
    }
    mrhiInstanceNotification record{};
    while (state.phase != Phase::Ready && mrhiNextInstanceNotification(state.instance, &record) == mrhi_success) {
        if (record.kind == mrhi_instanceAdaptersFound && state.phase == Phase::Finding) {
            RAWFRAME_TRY(state.found(record));
        } else if (record.kind == mrhi_instanceDeviceReady && state.phase == Phase::Opening) {
            if (record.outcome != mrhi_success) {
                RAWFRAME_TRY(state.fail(failed("the device did not open", record.outcome).error()));
            }
            state.phase = Phase::Ready;
        }
    }
    return state.phase == Phase::Ready;
}

const std::optional<AdapterDescription>& Device::adapter() const noexcept {
    return state_->adapter;
}

mrhiDevice* Device::native() const noexcept {
    return state_->phase == Phase::Ready ? state_->device : nullptr;
}

std::uint8_t Device::sampleCounts(std::uint32_t format) const noexcept {
    mrhiFormatCaps caps{};
    if (state_->phase != Phase::Ready ||
        mrhiGetFormatCaps(state_->instance, state_->chosen, static_cast<mrhiFormat>(format), &caps) != mrhi_success) {
        return 0;
    }
    return caps.sampleCounts;
}

void Device::pump() {
    if (state_->phase != Phase::Ready) {
        return;
    }
    mrhiDeviceNotification record{};
    while (mrhiNextDeviceNotification(state_->device, &record) == mrhi_success) {
        if (record.kind == mrhi_deviceLostNotice) {
            state_->lost = true;
            continue;
        }
        state_->answers[requestKey(record.requestId.index1, record.requestId.generation)] = record.outcome;
    }
}

std::optional<result::Status> Device::answer(std::uint64_t request) {
    const auto kFound = state_->answers.find(request);
    if (kFound == state_->answers.end()) {
        return std::nullopt;
    }
    const mrhiResult kOutcome = kFound->second;
    state_->answers.erase(kFound);
    if (kOutcome != mrhi_success) {
        return result::Status{failed("the device's work failed", kOutcome)};
    }
    return result::Status{};
}

bool Device::lost() const noexcept {
    return state_->lost;
}

} // namespace rawframe::render

namespace rawframe::render {

result::Result<std::uint64_t> Device::associate(const window::HandleBundle& bundle) {
    State& state = *state_;
    Source source;
    const mrhiChain* chain = source.chain(bundle);
    if (chain == nullptr) {
        return refuse(result::ErrorClass::Unsupported, RenderError::State, "a test window has nothing to present to");
    }
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = chain;
    mrhiSurfaceId made{};
    if (const mrhiResult kMade = mrhiCreateSurface(state.instance, &def, &made); kMade != mrhi_success) {
        return failed("the window's surface could not be made", kMade);
    }
    if (state.phase == Phase::Asking && !state.compatible.has_value()) {
        state.compatible = made;
    }
    const std::uint64_t kKey = requestKey(made.index1, made.generation);
    state.surfaces.emplace(kKey, Held{.id = made});
    return kKey;
}

void Device::release(std::uint64_t surface) noexcept {
    State& state = *state_;
    const auto kFound = state.surfaces.find(surface);
    if (kFound == state.surfaces.end()) {
        return;
    }
    static_cast<void>(mrhiDestroySurface(state.instance, kFound->second.id));
    state.surfaces.erase(kFound);
}

result::Result<PreparedSurface>
Device::prepare(std::uint64_t surface, const window::SurfaceState& window, PresentPolicy policy, OutputMode output) {
    State& state = *state_;
    const auto kFound = state.surfaces.find(surface);
    if (state.phase != Phase::Ready || kFound == state.surfaces.end()) {
        return refuse(result::ErrorClass::FailedPrecondition, RenderError::State, "no such surface on a ready device");
    }
    Held& held = kFound->second;
    PreparedSurface prepared{.policy = held.used, .size = held.size};
    if (window.generation == 0 || window.occluded || state.lost) {
        return prepared;
    }
    const bool kFits = held.configured && !held.outOfDate && held.asked == policy &&
                       held.size.width == window.pixelSize.width && held.size.height == window.pixelSize.height;
    // What the surface offers, read again when it is configured and when
    // its display's facts change (D365).
    mrhiSurfaceCaps caps{};
    if (!kFits || held.facts != window.display) {
        if (const mrhiResult kRead = mrhiGetSurfaceCaps(state.instance, held.id, state.chosen, &caps);
            kRead != mrhi_success) {
            return failed("the surface's capabilities could not be read", kRead);
        }
        held.offered = offeredBy(caps);
        held.facts = window.display;
    }
    OutputRecord record = resolveOutput(held.offered, window.display, output, kDrawn);
    if (held.output.revision == 0 || !record.sameAs(held.output)) {
        record.revision = held.output.revision + 1;
        held.output = record;
        prepared.outputChanged = true;
    }
    prepared.output = held.output;
    if (!kFits) {
        if (!caps.presentable) {
            return refuse(result::ErrorClass::Unavailable, RenderError::Device, "the adapter cannot present there");
        }
        mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
        config.surface = held.id;
        for (std::uint32_t at = 0; at < caps.colorCount; ++at) {
            if (standardColor(caps.colors[at])) {
                config.color = caps.colors[at];
                break;
            }
        }
        for (const mrhiPresentModes kMode : chainOf(policy)) {
            if ((caps.presentModes & kMode) != 0) {
                config.presentMode = kMode;
                break;
            }
        }
        config.width = window.pixelSize.width;
        config.height = window.pixelSize.height;
        const mrhiResult kConfigured = mrhiConfigureSurface(state.device, &config);
        held.configured = kConfigured == mrhi_success;
        if (kConfigured == mrhi_errorOutOfDate) {
            // The window moved on meanwhile: the next frame tries again.
            return prepared;
        }
        if (kConfigured != mrhi_success) {
            return failed("the surface could not be configured", kConfigured);
        }
        held.outOfDate = false;
        held.asked = policy;
        held.used = policyOf(config.presentMode);
        held.size = window.pixelSize;
        held.format = config.color.format;
        prepared.reconfigured = true;
    }
    prepared.drawable = true;
    prepared.policy = held.used;
    prepared.size = held.size;
    return prepared;
}

result::Result<std::optional<std::uint64_t>> Device::acquire(std::uint64_t surface) {
    State& state = *state_;
    const auto kFound = state.surfaces.find(surface);
    if (kFound == state.surfaces.end() || !kFound->second.configured) {
        return refuse(result::ErrorClass::FailedPrecondition, RenderError::State, "no such configured surface");
    }
    mrhiResourceId image{};
    const mrhiResult kAcquired = mrhiAcquireSurfaceImage(state.device, kFound->second.id, &image);
    switch (kAcquired) {
    case mrhi_success:
        return requestKey(image.index1, image.generation);
    case mrhi_suboptimal:
        kFound->second.outOfDate = true;
        return requestKey(image.index1, image.generation);
    case mrhi_occluded:
        return std::nullopt;
    case mrhi_errorOutOfDate:
        kFound->second.outOfDate = true;
        return std::nullopt;
    case mrhi_errorDeviceLost:
        state.lost = true;
        return std::nullopt;
    default:
        return failed("the surface's image could not be acquired", kAcquired);
    }
}

std::uint32_t Device::surfaceFormat(std::uint64_t surface) const noexcept {
    const auto kFound = state_->surfaces.find(surface);
    if (kFound == state_->surfaces.end()) {
        return mrhi_formatNone;
    }
    return kFound->second.format;
}

} // namespace rawframe::render
