#pragma once

#include "rawframe/composition/declaration.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::composition {

/// Why a plan was refused. Every problem found is reported, not only the
/// first, the way a compiler reports every error in a file.
enum class ProblemKind : std::uint8_t {
    DuplicateRegistrar,
    SubmitAfterClose,
    EmptyIdentity,
    DuplicateIdentity,
    MissingFactory,
    ScopeNotAllowed,
    MissingShutdownContract,
    UnknownSelection,
    IneligibleProvider,
    AmbiguousProvider,
    MissingProvider,
    MissingParticipant,
    LifetimeViolation,
    MissingExecutor,
    DependencyCycle,
    BudgetNotNested,
};

[[nodiscard]] std::string_view describe(ProblemKind kind) noexcept;

struct Problem {
    ProblemKind kind;
    /// The participant (or module, for a registrar problem) it concerns.
    std::string subject;
    std::string detail;
};

/// Chooses the provider of a capability that several eligible participants
/// provide. The participants not chosen are left out of the plan.
struct Selection {
    std::string_view capability;
    std::string_view participant;
};

/// Everything a plan is built from, supplied by the host.
struct CompositionRequest {
    std::span<const RegistrarEntry> registrars;
    TargetRole role = TargetRole::Test;
    Platform platform = Platform::Linux;
    std::span<const Selection> selections;
    bool cpuExecutor = true;
    bool blockingIoExecutor = true;
    /// The whole composition's stop budget. Participant budgets nest inside it.
    execution::MonotonicDuration shutdownBudget;
};

/// One participant in a validated plan: an owned copy of its declaration plus
/// what planning resolved.
struct PlannedParticipant {
    std::string identity;
    std::string owningModule;
    FactoryFunction factory = nullptr;
    LifetimeScope scope = LifetimeScope::World;
    std::vector<std::string> providedCapabilities;
    std::vector<std::string> requiredCapabilities;
    std::vector<std::string> optionalCapabilities;
    std::vector<std::string> requiredParticipants;
    ExecutorRequirement executor;
    LifecyclePolicy lifecycle;
    CancellationPolicy cancellation;
    std::string observabilityIdentity;
    std::string budgetOwner;

    struct Resolved {
        std::string capability;
        std::size_t provider; // index into Plan::participants()
    };
    /// Required capabilities, and the optional ones that are present.
    std::vector<Resolved> capabilities;
    /// Plan indices this participant needs constructed and started first.
    std::vector<std::size_t> dependencies;
};

/// A validated, immutable composition plan: participants in their start order,
/// which is dependency order with ties broken by scope and then identity, so
/// the same inputs always give the same plan.
class Plan {
public:
    [[nodiscard]] std::span<const PlannedParticipant> participants() const noexcept {
        return participants_;
    }
    [[nodiscard]] execution::MonotonicDuration shutdownBudget() const noexcept {
        return shutdownBudget_;
    }

private:
    friend result::Result<Plan> compose(const CompositionRequest& request, std::vector<Problem>& problems);

    std::vector<PlannedParticipant> participants_;
    execution::MonotonicDuration shutdownBudget_;
};

/// Runs every registrar once, in ascending module-ID order, and validates the
/// result under SPEC-0005 before anything is constructed. On any problem,
/// `problems` lists them all and the Error summarizes.
[[nodiscard]] result::Result<Plan> compose(const CompositionRequest& request, std::vector<Problem>& problems);

} // namespace rawframe::composition
