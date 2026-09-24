#include "rawframe/world/schedule.h"

#include "rawframe/world/errors.h"

#include <algorithm>
#include <map>
#include <set>

namespace rawframe::world {

namespace {

std::unexpected<result::Error> refuse(WorldError error, std::string_view description, std::string_view system) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kWorldDomain, code(error), description)
            .error()
            .withContext("system", system)};
}

bool overlaps(const std::vector<schema::ComponentRuntimeId>& left,
              const std::vector<schema::ComponentRuntimeId>& right) {
    for (const schema::ComponentRuntimeId kComponent : left) {
        if (std::find(right.begin(), right.end(), kComponent) != right.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string_view describe(Phase phase) noexcept {
    switch (phase) {
    case Phase::BeginTick:
        return "begin_tick";
    case Phase::ApplyInputs:
        return "apply_inputs";
    case Phase::PreSimulation:
        return "pre_simulation";
    case Phase::Simulation:
        return "simulation";
    case Phase::PostSimulation:
        return "post_simulation";
    case Phase::Commit:
        return "commit";
    case Phase::Replication:
        return "replication";
    case Phase::EndTick:
        return "end_tick";
    }
    return "simulation";
}

result::Result<Schedule> Schedule::compile(std::span<const SystemDeclaration> declarations,
                                           const schema::SchemaRegistry& registry,
                                           CommandBufferSettings commandSettings) {
    std::map<std::string_view, const SystemDeclaration*> byIdentity;
    const std::size_t kComponents = registry.components().size();
    for (const SystemDeclaration& declaration : declarations) {
        if (declaration.identity.empty() || declaration.system == nullptr) {
            return refuse(WorldError::UnknownSystem, "a system without an identity or an implementation", "");
        }
        if (!byIdentity.emplace(declaration.identity, &declaration).second) {
            return refuse(WorldError::DuplicateSystem, "two systems share an identity", declaration.identity);
        }
        for (const auto kAccess : {declaration.reads, declaration.writes}) {
            for (const schema::ComponentRuntimeId kComponent : kAccess) {
                if (kComponent.value >= kComponents) {
                    return refuse(WorldError::UndeclaredAccess,
                                  "a system declares access to a component outside the registry",
                                  declaration.identity);
                }
            }
        }
    }

    // Edges within a phase: `from` runs before `to`.
    std::map<std::string_view, std::vector<std::string_view>> successors;
    std::map<std::string_view, std::size_t> waiting;
    for (const SystemDeclaration& declaration : declarations) {
        waiting.emplace(declaration.identity, 0);
    }
    const auto kLink = [&](std::string_view from, std::string_view to, std::string_view named) -> result::Status {
        const auto kFrom = byIdentity.find(from);
        const auto kTo = byIdentity.find(to);
        if (kFrom == byIdentity.end() || kTo == byIdentity.end()) {
            return refuse(WorldError::UnknownSystem, "an ordering names an unknown system", named);
        }
        if (kFrom->second->phase != kTo->second->phase) {
            return refuse(
                WorldError::CrossPhaseOrdering, "an ordering crosses phases, which already order themselves", named);
        }
        successors[from].push_back(to);
        ++waiting[to];
        return {};
    };
    for (const SystemDeclaration& declaration : declarations) {
        for (const std::string_view kEarlier : declaration.after) {
            RAWFRAME_TRY(kLink(kEarlier, declaration.identity, declaration.identity));
        }
        for (const std::string_view kLater : declaration.before) {
            RAWFRAME_TRY(kLink(declaration.identity, kLater, declaration.identity));
        }
    }

    // Kahn's algorithm per phase, drained in ascending identity order.
    Schedule schedule;
    for (std::size_t phase = 0; phase < kPhaseCount; ++phase) {
        schedule.phaseStart_[phase] = schedule.entries_.size();
        std::set<std::string_view> ready;
        std::size_t inPhase = 0;
        for (const auto& [identity, declaration] : byIdentity) {
            if (static_cast<std::size_t>(declaration->phase) == phase) {
                ++inPhase;
                if (waiting[identity] == 0) {
                    ready.insert(identity);
                }
            }
        }
        std::size_t placed = 0;
        while (!ready.empty()) {
            const std::string_view kNext = *ready.begin();
            ready.erase(ready.begin());
            const SystemDeclaration& declaration = *byIdentity[kNext];
            schedule.entries_.push_back(Entry{
                .identity = std::string{kNext},
                .phase = declaration.phase,
                .reads = {declaration.reads.begin(), declaration.reads.end()},
                .writes = {declaration.writes.begin(), declaration.writes.end()},
                .exclusive = declaration.exclusive,
                .system = declaration.system,
                .commands = std::make_unique<CommandBuffer>(commandSettings),
            });
            ++placed;
            for (const std::string_view kSuccessor : successors[kNext]) {
                if (--waiting[kSuccessor] == 0) {
                    ready.insert(kSuccessor);
                }
            }
        }
        if (placed != inPhase) {
            return refuse(WorldError::SystemCycle,
                          "the systems of a phase order each other in a cycle",
                          describe(static_cast<Phase>(phase)));
        }
    }
    schedule.phaseStart_[kPhaseCount] = schedule.entries_.size();
    return schedule;
}

result::Status Schedule::applyCommands(World& world, std::size_t firstPhase, std::size_t endPhase, TickReport& report) {
    for (std::size_t index = phaseStart_[firstPhase]; index < phaseStart_[endPhase]; ++index) {
        auto applied = world.apply(*entries_[index].commands);
        if (!applied.has_value()) {
            // Nothing recorded this tick may leak into the next.
            for (std::size_t rest = index + 1; rest < phaseStart_[endPhase]; ++rest) {
                entries_[rest].commands->clear();
            }
            return std::unexpected<result::Error>{
                std::move(applied).error().withContext("system", entries_[index].identity)};
        }
        report.commandsApplied += applied->applied;
        report.commandsSkipped += applied->skippedStale;
    }
    return {};
}

result::Result<TickReport>
Schedule::runTick(World& world, TickIndex& tick, TickRate rate, diagnostics::Emitter emitter) {
    TickReport report;
    report.tick = tick;
    constexpr auto kCommit = static_cast<std::size_t>(Phase::Commit);
    for (std::size_t phase = 0; phase < kPhaseCount; ++phase) {
        if (phase == kCommit) {
            // The barrier: everything recorded so far becomes visible to the
            // commit phase and everything after it.
            RAWFRAME_TRY(applyCommands(world, 0, kCommit, report));
        }
        world.lockStructure();
        for (std::size_t index = phaseStart_[phase]; index < phaseStart_[phase + 1]; ++index) {
            Entry& entry = entries_[index];
            SystemContext context{world, *entry.commands, tick, rate, emitter};
            if (auto ran = entry.system->run(context); !ran.has_value()) {
                entry.commands->clear();
                report.failures.push_back(TickReport::Failure{entry.identity, std::move(ran).error()});
            }
        }
        world.unlockStructure();
    }
    RAWFRAME_TRY(applyCommands(world, kCommit, kPhaseCount, report));
    ++tick.value;
    return report;
}

std::vector<std::string> Schedule::order() const {
    std::vector<std::string> identities;
    for (const Entry& entry : entries_) {
        identities.push_back(entry.identity);
    }
    return identities;
}

const Schedule::Entry* Schedule::find(std::string_view identity) const {
    for (const Entry& entry : entries_) {
        if (entry.identity == identity) {
            return &entry;
        }
    }
    return nullptr;
}

bool Schedule::conflicts(std::string_view first, std::string_view second) const {
    const Entry* left = find(first);
    const Entry* right = find(second);
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return left->exclusive || right->exclusive || overlaps(left->writes, right->writes) ||
           overlaps(left->writes, right->reads) || overlaps(right->writes, left->reads);
}

} // namespace rawframe::world
