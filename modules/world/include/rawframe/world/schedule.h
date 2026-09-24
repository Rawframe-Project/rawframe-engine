#pragma once

#include "rawframe/diagnostics/emitter.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/command_buffer.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world {

/// The World schedule's phases, in order. The set is closed: nothing adds a
/// phase or a position (SPEC-0005).
enum class Phase : std::uint8_t {
    BeginTick,
    ApplyInputs,
    PreSimulation,
    Simulation,
    PostSimulation,
    Commit,
    Replication,
    EndTick,
};

inline constexpr std::size_t kPhaseCount = 8;

[[nodiscard]] std::string_view describe(Phase phase) noexcept;

/// What a system sees while it runs: the World with its structure locked, its
/// own command buffer, and the tick.
struct SystemContext {
    World& world;
    CommandBuffer& commands;
    TickIndex tick;
    TickRate rate;
    diagnostics::Emitter emitter;
};

/// Behaviour over World data. Returning an Error discards the commands the
/// system recorded this tick and is reported; other systems still run.
class System {
public:
    System() = default;
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    virtual ~System() = default;

    [[nodiscard]] virtual result::Status run(SystemContext& context) noexcept = 0;
};

/// One system as declared (SPEC-0005 system declaration). Borrowed only for
/// the duration of `Schedule::compile`, which copies it.
struct SystemDeclaration {
    std::string_view identity;
    Phase phase = Phase::Simulation;
    std::span<const schema::ComponentRuntimeId> reads;
    std::span<const schema::ComponentRuntimeId> writes;
    /// Systems in the same phase this one runs after, or before.
    std::span<const std::string_view> after;
    std::span<const std::string_view> before;
    /// Runs alone for its World: conflicts with every other system.
    bool exclusive = false;
    System* system = nullptr;
};

/// What one tick did.
struct TickReport {
    TickIndex tick;
    std::size_t commandsApplied = 0;
    std::size_t commandsSkipped = 0;
    struct Failure {
        std::string system;
        result::Error error;
    };
    /// Systems that returned an Error; their commands were discarded.
    std::vector<Failure> failures;
};

/// A compiled, immutable system order. Each phase runs its systems in a
/// topological order of the declared before/after edges, with stable identity
/// breaking ties, so worker timing can never become gameplay order. Declared
/// reads and writes do not change that order; they say which systems may later
/// run side by side. Commands recorded before the commit phase are applied at
/// its start, and the rest at the end of the tick, each system's in order.
class Schedule {
public:
    /// Validates and orders the declarations. Refuses an empty or duplicate
    /// identity, a missing system, an unknown before/after target, an edge to a
    /// system in another phase, a cycle, and access to a component outside the
    /// registry.
    [[nodiscard]] static result::Result<Schedule> compile(std::span<const SystemDeclaration> declarations,
                                                          const schema::SchemaRegistry& registry,
                                                          CommandBufferSettings commandSettings = {});

    /// Runs one tick: every phase in order, then advances `tick`.
    [[nodiscard]] result::Result<TickReport>
    runTick(World& world, TickIndex& tick, TickRate rate, diagnostics::Emitter emitter = {});

    /// Identities in execution order, phase by phase.
    [[nodiscard]] std::vector<std::string> order() const;

    /// Whether two systems' declared access conflicts: a write overlaps a read
    /// or write of the other, or either is exclusive.
    [[nodiscard]] bool conflicts(std::string_view first, std::string_view second) const;

private:
    struct Entry {
        std::string identity;
        Phase phase;
        std::vector<schema::ComponentRuntimeId> reads;
        std::vector<schema::ComponentRuntimeId> writes;
        bool exclusive = false;
        System* system = nullptr;
        std::unique_ptr<CommandBuffer> commands;
    };

    [[nodiscard]] result::Status
    applyCommands(World& world, std::size_t firstPhase, std::size_t endPhase, TickReport& report);
    [[nodiscard]] const Entry* find(std::string_view identity) const;

    std::vector<Entry> entries_; // in execution order
    std::array<std::size_t, kPhaseCount + 1> phaseStart_{};
};

} // namespace rawframe::world
