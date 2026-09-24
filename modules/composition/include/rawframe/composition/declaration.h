#pragma once

#include "rawframe/composition/participant.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace rawframe::composition {

/// Who owns a participant, from longest lived to shortest (SPEC-0005). A
/// participant may depend only on participants that live at least as long.
enum class LifetimeScope : std::uint8_t {
    Host,
    Runtime,
    World
};

/// What a composition is built for. Participants state which they may run in.
enum class TargetRole : std::uint8_t {
    DedicatedServer,
    Client,
    Tool,
    Test
};
enum class Platform : std::uint8_t {
    Linux,
    Windows,
    MacOs,
    Web
};

struct EligibilityMask {
    static constexpr std::uint32_t kAll = ~std::uint32_t{0};

    std::uint32_t roles = kAll;
    std::uint32_t platforms = kAll;

    [[nodiscard]] constexpr bool admits(TargetRole role, Platform platform) const noexcept {
        return (roles & (std::uint32_t{1} << static_cast<unsigned>(role))) != 0 &&
               (platforms & (std::uint32_t{1} << static_cast<unsigned>(platform))) != 0;
    }
};

[[nodiscard]] constexpr std::uint32_t only(TargetRole role) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(role);
}

[[nodiscard]] constexpr std::uint32_t only(Platform platform) noexcept {
    return std::uint32_t{1} << static_cast<unsigned>(platform);
}

/// The executors a participant needs, and the quota it runs under on each.
/// Composition admits the participant as an owner with that quota.
struct ExecutorRequirement {
    bool cpu = false;
    bool blockingIo = false;
    execution::Quota quota;
};

struct LifecyclePolicy {
    /// How long `quiesce` plus `stop` may take. Required and positive: a
    /// participant with no shutdown contract is refused. Budgets nest inside the
    /// composition's (SPEC-0048).
    execution::MonotonicDuration stopBudget;
};

struct CancellationPolicy {
    /// Whether a failure of the participant's own work cancels the rest of it.
    execution::FailurePolicy failure = execution::FailurePolicy::Isolate;
};

using ParticipantOwner = std::unique_ptr<Participant>;

/// Constructs a complete participant, or fails. Runs only during validated
/// plan execution, never during registration.
using FactoryFunction = result::Result<ParticipantOwner> (*)(ParticipantContext& context) noexcept;

/// One participant as a registrar declares it. Members follow SPEC-0005's
/// required declaration list in its order (SPEC-0049). Everything is borrowed
/// for the duration of `submit`, which copies it.
struct ParticipantDeclaration {
    std::string_view identity;
    FactoryFunction factory = nullptr;
    LifetimeScope scope = LifetimeScope::World;
    std::span<const std::string_view> providedCapabilities;
    std::span<const std::string_view> requiredCapabilities;
    /// Used when present and otherwise absent; resolved during planning, never
    /// looked up later (SPEC-0005 optional capability use).
    std::span<const std::string_view> optionalCapabilities;
    std::span<const std::string_view> requiredParticipants;
    EligibilityMask eligibility;
    ExecutorRequirement executor;
    LifecyclePolicy lifecycle;
    CancellationPolicy cancellation;
    std::string_view observabilityIdentity;
    std::string_view budgetOwner;
    /// The Host phases this participant runs work in, as hostPhaseBit values.
    std::uint16_t hostPhases = 0;
};

} // namespace rawframe::composition
