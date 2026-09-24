#include "rawframe/composition/plan.h"

#include "rawframe/composition/errors.h"
#include "rawframe/execution/budget.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>

namespace rawframe::composition {

namespace detail {

/// What the registrars submitted, gathered before any validation.
struct Collector {
    struct Collected {
        PlannedParticipant participant;
        EligibilityMask eligibility;
    };

    std::vector<Collected> collected;
    std::vector<Problem>* problems;

    void report(ProblemKind kind, std::string_view subject, std::string_view detail) {
        problems->push_back(Problem{kind, std::string{subject}, std::string{detail}});
    }
};

} // namespace detail

namespace {

std::vector<std::string> copyAll(std::span<const std::string_view> names) {
    return std::vector<std::string>(names.begin(), names.end());
}

/// Longer-lived scopes rank lower.
int rank(LifetimeScope scope) noexcept {
    return static_cast<int>(scope);
}

bool contains(const std::vector<std::string>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

std::string_view describe(ProblemKind kind) noexcept {
    switch (kind) {
    case ProblemKind::DuplicateRegistrar:
        return "duplicate_registrar";
    case ProblemKind::SubmitAfterClose:
        return "submit_after_close";
    case ProblemKind::EmptyIdentity:
        return "empty_identity";
    case ProblemKind::DuplicateIdentity:
        return "duplicate_identity";
    case ProblemKind::MissingFactory:
        return "missing_factory";
    case ProblemKind::ScopeNotAllowed:
        return "scope_not_allowed";
    case ProblemKind::MissingShutdownContract:
        return "missing_shutdown_contract";
    case ProblemKind::UnknownSelection:
        return "unknown_selection";
    case ProblemKind::IneligibleProvider:
        return "ineligible_provider";
    case ProblemKind::AmbiguousProvider:
        return "ambiguous_provider";
    case ProblemKind::MissingProvider:
        return "missing_provider";
    case ProblemKind::MissingParticipant:
        return "missing_participant";
    case ProblemKind::LifetimeViolation:
        return "lifetime_violation";
    case ProblemKind::MissingExecutor:
        return "missing_executor";
    case ProblemKind::DependencyCycle:
        return "dependency_cycle";
    case ProblemKind::BudgetNotNested:
        return "budget_not_nested";
    }
    return "unknown";
}

void ParticipantRegistrar::submit(const ParticipantDeclaration& declaration) noexcept {
    detail::Collector& collector = *collector_;
    if (!open_) {
        collector.report(ProblemKind::SubmitAfterClose, owningModule_, "a registrar submitted after it returned");
        return;
    }
    if (declaration.identity.empty()) {
        collector.report(ProblemKind::EmptyIdentity, owningModule_, "a participant declared without an identity");
        return;
    }
    if ((allowedScopes_ & scopeBit(declaration.scope)) == 0) {
        collector.report(
            ProblemKind::ScopeNotAllowed, declaration.identity, "its scope is outside its module's registrar scopes");
        return;
    }
    if (declaration.factory == nullptr) {
        collector.report(ProblemKind::MissingFactory, declaration.identity, "no factory");
        return;
    }
    if (declaration.lifecycle.stopBudget.nanoseconds <= 0) {
        collector.report(ProblemKind::MissingShutdownContract, declaration.identity, "no positive stop budget");
        return;
    }
    // A deep copy, so a declaration may point at registrar-local storage.
    collector.collected.push_back(detail::Collector::Collected{
        .participant =
            PlannedParticipant{
                .identity = std::string{declaration.identity},
                .owningModule = std::string{owningModule_},
                .factory = declaration.factory,
                .scope = declaration.scope,
                .providedCapabilities = copyAll(declaration.providedCapabilities),
                .requiredCapabilities = copyAll(declaration.requiredCapabilities),
                .optionalCapabilities = copyAll(declaration.optionalCapabilities),
                .requiredParticipants = copyAll(declaration.requiredParticipants),
                .executor = declaration.executor,
                .lifecycle = declaration.lifecycle,
                .cancellation = declaration.cancellation,
                .observabilityIdentity = std::string{declaration.observabilityIdentity},
                .budgetOwner = std::string{declaration.budgetOwner},
            },
        .eligibility = declaration.eligibility,
    });
}

result::Result<Plan> compose(const CompositionRequest& request, std::vector<Problem>& problems) {
    const std::size_t kProblemsBefore = problems.size();
    detail::Collector collector{.collected = {}, .problems = &problems};

    // Registrars run in ascending module-ID order whatever order the host
    // listed them in, so the order carries no meaning (ADR-0068).
    std::vector<const RegistrarEntry*> entries;
    for (const RegistrarEntry& entry : request.registrars) {
        entries.push_back(&entry);
    }
    std::sort(entries.begin(), entries.end(), [](const RegistrarEntry* left, const RegistrarEntry* right) {
        return left->moduleId < right->moduleId;
    });
    // Every registrar stays alive for the whole pass, so a reference one of
    // them retained is refused as closed rather than dangling.
    std::vector<std::unique_ptr<ParticipantRegistrar>> registrars;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const RegistrarEntry& entry = *entries[index];
        if (index != 0 && entries[index - 1]->moduleId == entry.moduleId) {
            collector.report(ProblemKind::DuplicateRegistrar, entry.moduleId, "the module is listed twice");
            continue;
        }
        registrars.push_back(std::make_unique<ParticipantRegistrar>(collector, entry.moduleId, entry.scopes));
        entry.registrar(*registrars.back());
        registrars.back()->close();
    }

    auto& collected = collector.collected;
    const std::size_t kCount = collected.size();

    std::map<std::string_view, std::size_t> byIdentity;
    std::vector<bool> included(kCount, false);
    for (std::size_t index = 0; index < kCount; ++index) {
        const PlannedParticipant& participant = collected[index].participant;
        if (!byIdentity.emplace(participant.identity, index).second) {
            collector.report(ProblemKind::DuplicateIdentity, participant.identity, "declared more than once");
            continue;
        }
        included[index] = collected[index].eligibility.admits(request.role, request.platform);
    }

    // Selections choose one provider and leave the alternatives out.
    std::set<std::string_view> selected;
    for (const Selection& selection : request.selections) {
        const auto kFound = byIdentity.find(selection.participant);
        if (kFound == byIdentity.end() ||
            !contains(collected[kFound->second].participant.providedCapabilities, selection.capability) ||
            !selected.insert(selection.capability).second) {
            collector.report(ProblemKind::UnknownSelection,
                             selection.participant,
                             "the selection names no participant providing that capability, or repeats one");
            continue;
        }
        if (!included[kFound->second]) {
            collector.report(
                ProblemKind::IneligibleProvider, selection.participant, "selected but not eligible for this target");
            continue;
        }
        for (std::size_t index = 0; index < kCount; ++index) {
            if (index != kFound->second &&
                contains(collected[index].participant.providedCapabilities, selection.capability)) {
                included[index] = false;
            }
        }
    }

    std::map<std::string_view, std::vector<std::size_t>> providers;
    for (std::size_t index = 0; index < kCount; ++index) {
        if (included[index]) {
            for (const std::string& capability : collected[index].participant.providedCapabilities) {
                providers[capability].push_back(index);
            }
        }
    }
    for (const auto& [capability, candidates] : providers) {
        if (candidates.size() > 1) {
            collector.report(ProblemKind::AmbiguousProvider,
                             capability,
                             "several eligible participants provide it and no selection chooses one");
        }
    }

    const auto kProvidedByAnyone = [&](std::string_view capability) {
        for (const auto& entry : collected) {
            if (contains(entry.participant.providedCapabilities, capability)) {
                return true;
            }
        }
        return false;
    };

    // Resolve every edge, checking providers, lifetimes, and executors.
    for (std::size_t index = 0; index < kCount; ++index) {
        if (!included[index]) {
            continue;
        }
        PlannedParticipant& participant = collected[index].participant;
        const auto kDependOn = [&](std::size_t provider) {
            const PlannedParticipant& dependency = collected[provider].participant;
            if (rank(dependency.scope) > rank(participant.scope)) {
                collector.report(ProblemKind::LifetimeViolation,
                                 participant.identity,
                                 "depends on " + dependency.identity + ", which has a shorter lifetime");
            }
            if (std::find(participant.dependencies.begin(), participant.dependencies.end(), provider) ==
                participant.dependencies.end()) {
                participant.dependencies.push_back(provider);
            }
        };
        for (const std::string& capability : participant.requiredCapabilities) {
            const auto kFound = providers.find(capability);
            if (kFound != providers.end() && kFound->second.size() == 1) {
                participant.capabilities.push_back({capability, kFound->second.front()});
                kDependOn(kFound->second.front());
            } else if (kFound == providers.end()) {
                collector.report(kProvidedByAnyone(capability) ? ProblemKind::IneligibleProvider
                                                               : ProblemKind::MissingProvider,
                                 participant.identity,
                                 "requires " + capability + ", which no eligible participant provides");
            }
        }
        for (const std::string& capability : participant.optionalCapabilities) {
            const auto kFound = providers.find(capability);
            if (kFound != providers.end() && kFound->second.size() == 1) {
                participant.capabilities.push_back({capability, kFound->second.front()});
                kDependOn(kFound->second.front());
            }
        }
        for (const std::string& required : participant.requiredParticipants) {
            const auto kFound = byIdentity.find(required);
            if (kFound != byIdentity.end() && included[kFound->second]) {
                kDependOn(kFound->second);
            } else {
                collector.report(ProblemKind::MissingParticipant,
                                 participant.identity,
                                 "requires " + required + ", which is absent, ineligible, or not selected");
            }
        }
        if ((participant.executor.cpu && !request.cpuExecutor) ||
            (participant.executor.blockingIo && !request.blockingIoExecutor)) {
            collector.report(
                ProblemKind::MissingExecutor, participant.identity, "needs an executor this composition lacks");
        }
    }

    // Kahn's algorithm, draining ready participants by scope, then identity.
    std::vector<std::size_t> waitingOn(kCount, 0);
    std::vector<std::vector<std::size_t>> dependents(kCount);
    using ReadyKey = std::tuple<int, std::string_view, std::size_t>;
    std::set<ReadyKey> ready;
    for (std::size_t index = 0; index < kCount; ++index) {
        if (!included[index]) {
            continue;
        }
        const PlannedParticipant& participant = collected[index].participant;
        waitingOn[index] = participant.dependencies.size();
        for (const std::size_t kProvider : participant.dependencies) {
            dependents[kProvider].push_back(index);
        }
        if (waitingOn[index] == 0) {
            ready.emplace(rank(participant.scope), participant.identity, index);
        }
    }
    std::vector<std::size_t> order;
    while (!ready.empty()) {
        const std::size_t kNext = std::get<2>(*ready.begin());
        ready.erase(ready.begin());
        order.push_back(kNext);
        for (const std::size_t kDependent : dependents[kNext]) {
            if (--waitingOn[kDependent] == 0) {
                const PlannedParticipant& dependent = collected[kDependent].participant;
                ready.emplace(rank(dependent.scope), dependent.identity, kDependent);
            }
        }
    }
    for (std::size_t index = 0; index < kCount; ++index) {
        if (included[index] && waitingOn[index] != 0) {
            collector.report(
                ProblemKind::DependencyCycle, collected[index].participant.identity, "is part of a dependency cycle");
        }
    }

    std::vector<execution::MonotonicDuration> budgets;
    for (const std::size_t kIndex : order) {
        budgets.push_back(collected[kIndex].participant.lifecycle.stopBudget);
    }
    if (auto nested = execution::checkBudgetNesting(request.shutdownBudget, budgets); !nested.has_value()) {
        collector.report(ProblemKind::BudgetNotNested, "composition", nested.error().description());
    }

    if (problems.size() != kProblemsBefore) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::FailedPrecondition,
                         kCompositionDomain,
                         code(CompositionError::PlanRefused),
                         "the composition plan was refused; see its problem list")
                .error()
                .withContext("problems", std::to_string(problems.size() - kProblemsBefore))};
    }

    Plan plan;
    plan.shutdownBudget_ = request.shutdownBudget;
    std::vector<std::size_t> position(kCount, 0);
    for (std::size_t index = 0; index < order.size(); ++index) {
        position[order[index]] = index;
    }
    for (const std::size_t kIndex : order) {
        PlannedParticipant participant = std::move(collected[kIndex].participant);
        for (std::size_t& dependency : participant.dependencies) {
            dependency = position[dependency];
        }
        std::sort(participant.dependencies.begin(), participant.dependencies.end());
        for (PlannedParticipant::Resolved& resolved : participant.capabilities) {
            resolved.provider = position[resolved.provider];
        }
        plan.participants_.push_back(std::move(participant));
    }
    return plan;
}

} // namespace rawframe::composition
