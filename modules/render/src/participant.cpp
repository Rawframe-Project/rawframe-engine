#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/device.h"
#include "rawframe/render/registrar.h"

#include <memory>
#include <optional>
#include <string>

namespace rawframe::render {

namespace {

constexpr diagnostics::EventIdentity kReady{"render", "device_ready"};
constexpr diagnostics::EventIdentity kUnavailable{"render", "device_unavailable"};
constexpr diagnostics::EventIdentity kLost{"render", "device_lost"};
constexpr std::string_view kProvided[] = {kDevice.name};
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Holds the one device: asked for at start, opened as its answers come,
/// and lent while it is ready. A device that cannot be had is reported
/// once and the process draws nothing, as a client without a GPU would.
class DeviceParticipant final : public composition::Participant, public DeviceHolder {
public:
    result::Status load(composition::ParticipantContext& context) {
        const std::optional<std::string_view> kWanted = context.configuration().text("render.device");
        if (!kWanted.has_value()) {
            return {};
        }
        if (*kWanted != "hardware" && *kWanted != "any") {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               composition::kCompositionDomain,
                                                               code(composition::CompositionError::BadConfiguration),
                                                               "render.device is hardware or any")
                                                      .error()};
        }
        settings_ = DeviceSettings{.allowSoftware = *kWanted == "any"};
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (!settings_.has_value()) {
            return {};
        }
        auto device = Device::request(*settings_);
        if (!device.has_value()) {
            unavailable(device.error());
            return {};
        }
        device_ = std::move(*device);
        poll();
        return {};
    }

    void runHostPhase(composition::HostPhase /*phase*/, const composition::HostFrame& /*frame*/) noexcept override {
        poll();
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kDevice.name) {
            return composition::provideAs<DeviceHolder>(*this);
        }
        return {};
    }

    Device* ready() noexcept override {
        return opened_ && !lostSeen_ ? device_.get() : nullptr;
    }

private:
    /// The opening moved on while it lasts; a loss noticed once after.
    void poll() noexcept {
        if (device_ == nullptr) {
            return;
        }
        if (!opened_) {
            const auto kOpen = device_->open();
            if (!kOpen.has_value()) {
                unavailable(kOpen.error());
                device_.reset();
                return;
            }
            if (!*kOpen) {
                return;
            }
            opened_ = true;
            emitter_.log(diagnostics::Severity::Info,
                         kReady,
                         "the device is ready",
                         {diagnostics::field("adapter", device_->adapter()->name),
                          diagnostics::field("software", device_->adapter()->software)});
        }
        if (!lostSeen_ && device_->lost()) {
            lostSeen_ = true;
            emitter_.log(diagnostics::Severity::Error, kLost, "the device was lost: nothing more is drawn", {});
        }
    }

    void unavailable(const result::Error& error) noexcept {
        emitter_.log(diagnostics::Severity::Warning,
                     kUnavailable,
                     "no device could be had: nothing is drawn",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    std::optional<DeviceSettings> settings_;
    std::unique_ptr<Device> device_;
    bool opened_ = false;
    bool lostSeen_ = false;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> make(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<DeviceParticipant>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.render.device",
        .factory = &make,
        .scope = composition::LifetimeScope::Runtime,
        .providedCapabilities = kProvided,
        .eligibility = {.roles = ~kServer},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "render.device",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PlatformPoll),
    });
}

} // namespace rawframe::render
