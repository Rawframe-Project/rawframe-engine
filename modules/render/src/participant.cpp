#include "frames.h"
#include "rawframe/composition/composition.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/render/device.h"
#include "rawframe/render/registrar.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::render {

namespace {

constexpr diagnostics::EventIdentity kReady{"render", "device_ready"};
constexpr diagnostics::EventIdentity kUnavailable{"render", "device_unavailable"};
constexpr diagnostics::EventIdentity kLost{"render", "device_lost"};
constexpr diagnostics::EventIdentity kSurfaceMade{"render", "surface_made"};
constexpr diagnostics::EventIdentity kSurfaceFailed{"render", "surface_failed"};
constexpr diagnostics::EventIdentity kSubstituted{"render", "present_policy_substituted"};
constexpr diagnostics::EventIdentity kSurfaceSummary{"render", "surface_summary"};
constexpr diagnostics::EventIdentity kUnasked{"render", "device_unasked"};
constexpr diagnostics::EventIdentity kOutputRecord{"render", "output_record"};
constexpr diagnostics::EventIdentity kOutputSubstituted{"render", "output_substituted"};

/// The modes a record says its surface offers, apart by spaces.
std::string offeredText(const OutputRecord& record) {
    std::string text;
    for (std::size_t at = 0; at < kOutputModes; ++at) {
        if (record.offered.at(at)) {
            text += text.empty() ? "" : " ";
            text += nameOf(static_cast<OutputMode>(at));
        }
    }
    return text;
}
constexpr std::string_view kProvided[] = {kDevice.name};
constexpr std::string_view kMaybe[] = {window::kSurfaces.name};

constexpr std::string_view kPolicyNames[] = {"vsync", "adaptive_vsync", "low_latency_vsync", "immediate"};

std::string_view nameOf(PresentPolicy policy) noexcept {
    return kPolicyNames[static_cast<std::size_t>(policy)];
}
constexpr std::uint32_t kServer = composition::only(composition::TargetRole::DedicatedServer);

/// Holds the one device: asked for at start, opened as its answers come,
/// and lent while it is ready. A device that cannot be had is reported
/// once and the process draws nothing, as a client without a GPU would.
/// Where the process has windows (`rawframe.window.surfaces`), the device
/// is asked for once the first window has a surface, so the adapter chosen
/// presents to it, and each window's surface generation is made into a
/// surface of the device as its handles come.
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
        if (const auto kPolicy = context.configuration().text("render.present")) {
            bool known = false;
            for (std::size_t at = 0; at < std::size(kPolicyNames); ++at) {
                if (*kPolicy == kPolicyNames[at]) {
                    policy_ = static_cast<PresentPolicy>(at);
                    known = true;
                }
            }
            if (!known) {
                return std::unexpected<result::Error>{
                    result::fail(result::ErrorClass::InvalidArgument,
                                 composition::kCompositionDomain,
                                 code(composition::CompositionError::BadConfiguration),
                                 "render.present is vsync, adaptive_vsync, low_latency_vsync, or immediate")
                        .error()};
            }
        }
        // ADR-0047's output mode asked of every window (D365): SDR unless
        // named; one that cannot be used falls back to SDR, said so.
        if (const auto kOutput = context.configuration().text("render.output")) {
            const auto kMode = outputModeNamed(*kOutput);
            if (!kMode.has_value()) {
                return std::unexpected<result::Error>{
                    result::fail(result::ErrorClass::InvalidArgument,
                                 composition::kCompositionDomain,
                                 code(composition::CompositionError::BadConfiguration),
                                 "render.output is sdr_srgb, hdr_linear_fp16_rec709, or hdr10_pq_rec2020")
                        .error()};
            }
            output_ = *kMode;
        }
        if (context.has(window::kSurfaces.name)) {
            RAWFRAME_TRY_ASSIGN(windows_, context.capability(window::kSurfaces));
        }
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        context_ = &context;
        poll();
        return {};
    }

    void stop() noexcept override {
        if (device_ == nullptr) {
            unasked();
            return;
        }
        // Surfaces before their windows, which the host ends after the
        // Host; the device itself when this goes.
        for (const auto& [window, surface] : surfaces_) {
            device_->release(surface.key);
        }
        surfaces_.clear();
        if (windows_ != nullptr) {
            emitter_.log(diagnostics::Severity::Info,
                         kSurfaceSummary,
                         "the windows' surfaces on the device",
                         {diagnostics::field("surfacesMade", surfacesMade_),
                          diagnostics::field("prepared", prepared_),
                          diagnostics::field("reconfigured", reconfigured_),
                          diagnostics::field("notDrawable", notDrawable_),
                          diagnostics::field("policy", nameOf(used_.value_or(policy_)))});
        }
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

    std::optional<std::pair<std::uint64_t, PreparedSurface>> prepare(window::WindowId window) noexcept override {
        Device* device = ready();
        const auto kSurface = surfaces_.find(window);
        if (device == nullptr || windows_ == nullptr || kSurface == surfaces_.end()) {
            return std::nullopt;
        }
        for (const window::SurfaceState& state : windows_->states()) {
            if (state.window != window) {
                continue;
            }
            auto prepared = device->prepare(kSurface->second.key, state, policy_, output_);
            if (!prepared.has_value()) {
                failedSurface(prepared.error());
                device->release(kSurface->second.key);
                surfaces_.erase(kSurface);
                return std::nullopt;
            }
            ++prepared_;
            reconfigured_ += prepared->reconfigured ? 1U : 0U;
            notDrawable_ += prepared->drawable ? 0U : 1U;
            if (prepared->reconfigured && prepared->policy != policy_ && used_ != prepared->policy) {
                // Once per change, as SPEC-0024 asks.
                emitter_.log(diagnostics::Severity::Warning,
                             kSubstituted,
                             "the present policy asked for is not offered: another of its chain is used",
                             {diagnostics::field("asked", nameOf(policy_)),
                              diagnostics::field("used", nameOf(prepared->policy))});
            }
            if (prepared->reconfigured) {
                used_ = prepared->policy;
            }
            if (prepared->outputChanged) {
                told(prepared->output);
            }
            return std::pair{kSurface->second.key, *prepared};
        }
        return std::nullopt;
    }

private:
    /// A window's HDR capability record told at each revision (D365), and
    /// the mode asked for, when another is used, said so.
    void told(const OutputRecord& record) noexcept {
        emitter_.log(diagnostics::Severity::Info,
                     kOutputRecord,
                     "a window's HDR capability record moved to a new revision",
                     {diagnostics::field("revision", record.revision),
                      diagnostics::field("offered", offeredText(record)),
                      diagnostics::field("active", nameOf(record.active)),
                      diagnostics::field("referenceWhiteNits", static_cast<double>(record.referenceWhiteNits)),
                      diagnostics::field("hdrReported", record.display.reported),
                      diagnostics::field("hdrOn", record.display.hdrOn),
                      diagnostics::field("peakNits", static_cast<double>(record.display.peakNits))});
        if (record.fallback != OutputFallback::None) {
            emitter_.log(diagnostics::Severity::Warning,
                         kOutputSubstituted,
                         "the output mode asked for is not used: SDR is",
                         {diagnostics::field("asked", nameOf(output_)),
                          diagnostics::field("used", nameOf(record.active)),
                          diagnostics::field("why", nameOf(record.fallback)),
                          diagnostics::field("revision", record.revision)});
        }
    }

    /// A window's surface on the device, for one surface generation.
    struct Surface {
        std::uint64_t key = 0;
        std::uint32_t generation = 0;
    };

    /// A run with windows that never asked for the device, said with what
    /// its first window had (D586): a client that showed nothing says why.
    void unasked() noexcept {
        if (windows_ == nullptr || requested_ || !settings_.has_value()) {
            return;
        }
        const auto kStates = windows_->states();
        const window::SurfaceState kFirst = kStates.empty() ? window::SurfaceState{} : kStates[0];
        emitter_.log(diagnostics::Severity::Warning,
                     kUnasked,
                     "no device was asked for: no window had a surface to present to",
                     {diagnostics::field("windows", kStates.size()),
                      diagnostics::field("generation", kFirst.generation),
                      diagnostics::field("width", kFirst.pixelSize.width),
                      diagnostics::field("height", kFirst.pixelSize.height)});
    }

    /// The device asked for, the opening moved on while it lasts, a loss
    /// noticed once after, and the windows' surfaces made as their handles
    /// come.
    void poll() noexcept {
        if (!settings_.has_value()) {
            return;
        }
        if (device_ == nullptr && !requested_) {
            // With windows, only once the first has a surface to present
            // to, so the adapter chosen presents there.
            std::optional<window::HandleBundle> first;
            if (windows_ != nullptr) {
                if (windows_->states().empty()) {
                    return;
                }
                first = windows_->take(windows_->states()[0].window);
                if (!first.has_value()) {
                    return;
                }
            }
            requested_ = true;
            auto device = Device::request(*settings_);
            if (!device.has_value()) {
                unavailable(device.error());
                return;
            }
            device_ = std::move(*device);
            if (first.has_value()) {
                associate(windows_->states()[0].window, *first);
            }
        }
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
            // SPEC-0024: a lost device is the presentation's end, and the
            // Host's policy a client's exit (D538).
            if (context_ != nullptr) {
                context_->reportHealth(composition::Health::Unhealthy, "device_lost");
            }
        }
        if (windows_ != nullptr) {
            follow();
        }
    }

    /// Each window's new surface generation made a surface, and a window
    /// gone or without a surface its surface released.
    void follow() noexcept {
        for (auto at = surfaces_.begin(); at != surfaces_.end();) {
            bool current = false;
            for (const window::SurfaceState& state : windows_->states()) {
                current = current || (state.window == at->first && state.generation == at->second.generation);
            }
            if (current) {
                ++at;
            } else {
                device_->release(at->second.key);
                at = surfaces_.erase(at);
            }
        }
        for (const window::SurfaceState& state : windows_->states()) {
            if (state.generation == 0 || surfaces_.contains(state.window)) {
                continue;
            }
            if (auto bundle = windows_->take(state.window)) {
                associate(state.window, *bundle);
            }
        }
    }

    void associate(window::WindowId window, const window::HandleBundle& bundle) noexcept {
        auto made = device_->associate(bundle);
        if (!made.has_value()) {
            failedSurface(made.error());
            return;
        }
        surfaces_[window] = Surface{.key = *made, .generation = bundle.generation};
        ++surfacesMade_;
        emitter_.log(diagnostics::Severity::Info,
                     kSurfaceMade,
                     "a window's surface was made on the device",
                     {diagnostics::field("generation", bundle.generation)});
    }

    void failedSurface(const result::Error& error) noexcept {
        emitter_.log(diagnostics::Severity::Warning,
                     kSurfaceFailed,
                     "a window's surface could not be used: it shows nothing",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    void unavailable(const result::Error& error) noexcept {
        emitter_.log(diagnostics::Severity::Warning,
                     kUnavailable,
                     "no device could be had: nothing is drawn",
                     {diagnostics::field("reason", std::string{error.description()})});
    }

    std::optional<DeviceSettings> settings_;
    PresentPolicy policy_ = PresentPolicy::Vsync;
    OutputMode output_ = OutputMode::SdrSrgb;
    std::optional<PresentPolicy> used_;
    window::Surfaces* windows_ = nullptr;
    bool requested_ = false;
    std::unique_ptr<Device> device_;
    std::map<window::WindowId, Surface> surfaces_;
    std::uint64_t surfacesMade_ = 0;
    std::uint64_t prepared_ = 0;
    std::uint64_t reconfigured_ = 0;
    std::uint64_t notDrawable_ = 0;
    bool opened_ = false;
    bool lostSeen_ = false;
    diagnostics::Emitter emitter_;
    composition::ParticipantContext* context_ = nullptr;
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
        .optionalCapabilities = kMaybe,
        .eligibility = {.roles = ~kServer},
        // Stopping releases surfaces, which waits for nothing.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)},
        .observabilityIdentity = "render.device",
        .budgetOwner = "render",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::PlatformPoll),
    });
    registerFrames(registrar);
}

} // namespace rawframe::render
