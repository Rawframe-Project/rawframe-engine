#include "rawframe/render/device.h"

#include "rawframe/render/errors.h"

#include <map>
#include <maul-rhi/capabilities.h>
#include <maul-rhi/device.h>
#include <maul-rhi/frame.h>
#include <maul-rhi/instance.h>
#include <string_view>

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
    /// The adapters were asked for.
    Finding,
    /// A device is opening on the adapter chosen.
    Opening,
    Ready,
    Failed,
};

} // namespace

struct Device::State {
    DeviceSettings settings;
    mrhiInstance* instance = nullptr;
    mrhiDevice* device = nullptr;
    Phase phase = Phase::Finding;
    std::optional<AdapterDescription> adapter;
    std::optional<result::Error> failure;
    /// Answers not yet taken, by request, with their outcomes; at most the
    /// device's own notification bound, since each asker takes its own.
    std::map<std::uint64_t, mrhiResult> answers;
    bool lost = false;

    ~State() {
        // The device goes before the instance that made it.
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
        mrhiAdapterId chosen{};
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
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    def.allowSoftware = settings.allowSoftware;
    mrhiRequestId request{};
    if (const mrhiResult kAsked = mrhiRequestAdapters(state->instance, &def, &request); kAsked != mrhi_success) {
        return failed("the adapters could not be asked for", kAsked);
    }
    return std::unique_ptr<Device>{new Device{std::move(state)}};
}

result::Result<bool> Device::open() {
    State& state = *state_;
    if (state.phase == Phase::Failed) {
        return std::unexpected<result::Error>{state.failure->clone()};
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
