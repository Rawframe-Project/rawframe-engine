#include "rawframe/composition/composition.h"

#include "rawframe/base/assert.h"

namespace rawframe::composition {

namespace {

constexpr diagnostics::EventIdentity kStopOverran{"composition", "participant_stop_overran"};

} // namespace

result::Status Participant::start(ParticipantContext&) noexcept {
    return {};
}

void Participant::quiesce() noexcept {
}

void Participant::stop() noexcept {
}

void Participant::runHostPhase(HostPhase, const HostFrame&) noexcept {
}

CapabilityObject Participant::provide(std::string_view) noexcept {
    return {};
}

bool ParticipantContext::has(std::string_view capability) const noexcept {
    for (const auto& resolved : composition_->slots_[index_].planned->capabilities) {
        if (resolved.capability == capability) {
            return true;
        }
    }
    return false;
}

std::string_view ParticipantContext::identity() const noexcept {
    return composition_->slots_[index_].planned->identity;
}

execution::CancellationScope& ParticipantContext::scope() noexcept {
    return *composition_->slots_[index_].scope->scope;
}

execution::OwnerId ParticipantContext::owner() const noexcept {
    return Composition::ownerFor(identity());
}

execution::Executor* ParticipantContext::cpuExecutor() const noexcept {
    return composition_->slots_[index_].cpuAdmitted ? composition_->services_.cpu : nullptr;
}

execution::Executor* ParticipantContext::blockingIoExecutor() const noexcept {
    return composition_->slots_[index_].blockingIoAdmitted ? composition_->services_.blockingIo : nullptr;
}

const execution::MonotonicSource& ParticipantContext::clock() const noexcept {
    return *composition_->services_.clock;
}

diagnostics::Emitter ParticipantContext::emitter() const noexcept {
    return composition_->services_.emitter;
}

result::Result<CapabilityObject> ParticipantContext::resolve(std::string_view capability) noexcept {
    for (const auto& resolved : composition_->slots_[index_].planned->capabilities) {
        if (resolved.capability != capability) {
            continue;
        }
        // Providers come earlier in plan order, so this one is constructed.
        Participant& provider = *composition_->slots_[resolved.provider].object;
        const CapabilityObject kObject = provider.provide(capability);
        if (kObject.object == nullptr) {
            return result::fail(result::ErrorClass::Internal,
                                kCompositionDomain,
                                code(CompositionError::CapabilityNotProvided),
                                "a participant declared a capability it does not provide");
        }
        return kObject;
    }
    return result::fail(result::ErrorClass::FailedPrecondition,
                        kCompositionDomain,
                        code(CompositionError::CapabilityNotResolved),
                        "the plan did not resolve this capability for this participant");
}

Composition::Composition(const Plan& plan, const HostServices& services) noexcept : plan_(&plan), services_(services) {
    RAWFRAME_CHECK(services.clock != nullptr && services.scope != nullptr, "a composition needs a clock and a scope");
}

Composition::~Composition() {
    stop();
}

execution::OwnerId Composition::ownerFor(std::string_view identity) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char kCharacter : identity) {
        hash ^= static_cast<unsigned char>(kCharacter);
        hash *= 0x100000001B3ULL;
    }
    return execution::OwnerId{hash};
}

result::Status Composition::admit(Slot& slot) {
    const ExecutorRequirement& requirement = slot.planned->executor;
    const execution::OwnerId kOwner = ownerFor(slot.planned->identity);
    if (requirement.cpu) {
        RAWFRAME_CHECK(services_.cpu != nullptr, "planning checked the CPU executor exists");
        RAWFRAME_TRY(services_.cpu->admitOwner(kOwner, requirement.quota));
        slot.cpuAdmitted = true;
    }
    if (requirement.blockingIo) {
        RAWFRAME_CHECK(services_.blockingIo != nullptr, "planning checked the blocking-I/O executor exists");
        RAWFRAME_TRY(services_.blockingIo->admitOwner(kOwner, requirement.quota));
        slot.blockingIoAdmitted = true;
    }
    return {};
}

result::Status Composition::start() {
    RAWFRAME_CHECK(!running_ && slots_.empty(), "start on a composition that is already running");
    const auto kParticipants = plan_->participants();
    slots_.resize(kParticipants.size());

    // Construction, in plan order. Nothing starts work yet.
    for (std::size_t index = 0; index < kParticipants.size(); ++index) {
        Slot& slot = slots_[index];
        slot.planned = &kParticipants[index];
        slot.scope = std::make_unique<ScopeHolder>(*services_.scope, slot.planned->cancellation.failure);
        if (!slot.scope->scope.has_value()) {
            auto error = std::move(slot.scope->scope).error();
            slot.scope.reset();
            unwind(0);
            return std::unexpected<result::Error>{std::move(error)};
        }
        slot.context = std::make_unique<ParticipantContext>(*this, index);
        if (auto admitted = admit(slot); !admitted.has_value()) {
            unwind(0);
            return admitted;
        }
        auto constructed = slot.planned->factory(*slot.context);
        if (!constructed.has_value()) {
            unwind(0);
            return std::unexpected<result::Error>{std::move(constructed).error()};
        }
        slot.object = std::move(*constructed);
        slot.state = ParticipantState::Constructed;
    }

    // Start, in the same order. A participant whose start fails may already
    // own work, so it is unwound with the ones before it.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        slot.state = ParticipantState::Starting;
        if (auto started = slot.object->start(*slot.context); !started.has_value()) {
            unwind(index + 1);
            return started;
        }
        slot.state = ParticipantState::Running;
    }
    running_ = true;
    return {};
}

void Composition::stop() noexcept {
    if (!running_) {
        return;
    }
    running_ = false;
    unwind(slots_.size());
}

void Composition::unwind(std::size_t started) noexcept {
    std::vector<execution::MonotonicDuration> spent(slots_.size());
    const auto kTimed = [&](std::size_t index, auto&& step) {
        const execution::MonotonicInstant kBegin = services_.clock->now();
        step();
        spent[index] = spent[index] + (services_.clock->now() - kBegin);
    };

    // Quiesce in reverse start order: cancel the participant's scope, then let
    // it close admission.
    for (std::size_t index = started; index-- > 0;) {
        Slot& slot = slots_[index];
        slot.state = ParticipantState::Quiescing;
        kTimed(index, [&] {
            slot.scope->scope->cancel(execution::CancelReason::OwnerStopping);
            slot.object->quiesce();
        });
    }
    // Stop in reverse start order: dependents before their dependencies.
    for (std::size_t index = started; index-- > 0;) {
        Slot& slot = slots_[index];
        kTimed(index, [&] {
            slot.object->stop();
        });
        slot.state = ParticipantState::Stopped;
        if (spent[index] > slot.planned->lifecycle.stopBudget) {
            // It returned, so nothing it references is at risk: reported, not
            // fatal.
            services_.emitter.log(diagnostics::Severity::Warning,
                                  kStopOverran,
                                  "a participant's quiesce and stop exceeded its stop budget",
                                  {diagnostics::field("participant", std::string_view{slot.planned->identity}),
                                   diagnostics::field("nanoseconds", spent[index].nanoseconds)});
        }
    }
    // Destroy everything constructed, in reverse, then release admissions and
    // scopes.
    for (std::size_t index = slots_.size(); index-- > 0;) {
        Slot& slot = slots_[index];
        slot.object.reset();
        slot.state = ParticipantState::Destroyed;
        const execution::OwnerId kOwner = ownerFor(slot.planned != nullptr ? slot.planned->identity : "");
        if (slot.cpuAdmitted) {
            static_cast<void>(services_.cpu->retireOwner(kOwner));
        }
        if (slot.blockingIoAdmitted) {
            static_cast<void>(services_.blockingIo->retireOwner(kOwner));
        }
        slot.context.reset();
        slot.scope.reset();
    }
    slots_.clear();
}

void Composition::runHostPhase(HostPhase phase, const HostFrame& frame) noexcept {
    if (!running_) {
        return;
    }
    const std::uint16_t kBit = hostPhaseBit(phase);
    for (Slot& slot : slots_) {
        if ((slot.planned->hostPhases & kBit) != 0) {
            slot.object->runHostPhase(phase, frame);
        }
    }
}

ParticipantState Composition::state(std::string_view identity) const noexcept {
    for (const Slot& slot : slots_) {
        if (slot.planned != nullptr && slot.planned->identity == identity) {
            return slot.state;
        }
    }
    return ParticipantState::Destroyed;
}

} // namespace rawframe::composition
