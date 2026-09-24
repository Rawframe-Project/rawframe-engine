#pragma once

// Test participants and registrars. Factories and registrars are plain
// function pointers, so the scenario a test sets up lives in one static
// Fixture that they read; each test resets it first.

#include "rawframe/composition/composition.h"
#include "rawframe/composition/plan.h"
#include "rawframe/composition/registrar.h"

#include <string>
#include <string_view>
#include <vector>

namespace rawframe::composition::testing {

struct Fixture {
    /// Lifecycle events in order, as "<phase> <identity>".
    std::vector<std::string> journal;
    std::string failConstruct;
    std::string failStart;
    int factoryCalls = 0;
    /// What the two test modules' registrars submit.
    std::vector<ParticipantDeclaration> moduleA;
    std::vector<ParticipantDeclaration> moduleB;
    /// Advanced by a participant's stop, to overrun its budget.
    execution::ManualClock* clock = nullptr;
    execution::MonotonicDuration stopTakes;
    bool quiesceSawCancellation = false;
};

inline Fixture& fixture() {
    static Fixture instance;
    return instance;
}

inline Fixture& resetFixture() {
    fixture() = Fixture{};
    return fixture();
}

inline constexpr result::ErrorCode kInjected{900};

class Recording : public Participant {
public:
    explicit Recording(std::string identity) : identity_(std::move(identity)) {
    }
    ~Recording() override {
        fixture().journal.push_back("destroy " + identity_);
    }

    result::Status start(ParticipantContext& context) noexcept override {
        context_ = &context;
        fixture().journal.push_back("start " + identity_);
        if (fixture().failStart == identity_) {
            return result::fail(result::ErrorClass::Unavailable, kCompositionDomain, kInjected, "start failed");
        }
        return {};
    }

    void quiesce() noexcept override {
        fixture().journal.push_back("quiesce " + identity_);
        if (context_ != nullptr && context_->token().cancelled()) {
            fixture().quiesceSawCancellation = true;
        }
    }

    void stop() noexcept override {
        fixture().journal.push_back("stop " + identity_);
        if (fixture().clock != nullptr) {
            fixture().clock->advance(fixture().stopTakes);
        }
    }

private:
    std::string identity_;
    ParticipantContext* context_ = nullptr;
};

inline result::Result<ParticipantOwner> makeRecording(ParticipantContext& context) noexcept {
    ++fixture().factoryCalls;
    const std::string kIdentity{context.identity()};
    if (fixture().failConstruct == kIdentity) {
        return result::fail(result::ErrorClass::Unavailable, kCompositionDomain, kInjected, "construction failed");
    }
    fixture().journal.push_back("construct " + kIdentity);
    return ParticipantOwner{new Recording{kIdentity}};
}

inline void registerModuleA(ParticipantRegistrar& registrar) noexcept {
    for (const auto& declaration : fixture().moduleA) {
        registrar.submit(declaration);
    }
}

inline void registerModuleB(ParticipantRegistrar& registrar) noexcept {
    for (const auto& declaration : fixture().moduleB) {
        registrar.submit(declaration);
    }
}

inline constexpr std::uint8_t kAllScopes =
    scopeBit(LifetimeScope::Host) | scopeBit(LifetimeScope::Runtime) | scopeBit(LifetimeScope::World);

/// A declaration with everything a valid one needs.
inline ParticipantDeclaration declare(std::string_view identity, LifetimeScope scope = LifetimeScope::Runtime) {
    return ParticipantDeclaration{.identity = identity,
                                  .factory = &makeRecording,
                                  .scope = scope,
                                  .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(10)}};
}

inline CompositionRequest request(std::span<const RegistrarEntry> registrars) {
    return CompositionRequest{.registrars = registrars,
                              .role = TargetRole::Test,
                              .platform = Platform::Linux,
                              .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)};
}

inline std::vector<std::string> identities(const Plan& plan) {
    std::vector<std::string> names;
    for (const auto& participant : plan.participants()) {
        names.push_back(participant.identity);
    }
    return names;
}

inline std::vector<ProblemKind> kinds(const std::vector<Problem>& problems) {
    std::vector<ProblemKind> found;
    for (const auto& problem : problems) {
        found.push_back(problem.kind);
    }
    return found;
}

} // namespace rawframe::composition::testing
