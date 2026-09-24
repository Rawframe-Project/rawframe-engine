#pragma once

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/errors.h"
#include "rawframe/composition/participant.h"
#include "rawframe/composition/plan.h"
#include "rawframe/diagnostics/emitter.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rawframe::composition {

class Composition;

/// The process authorities a Host owns and hands to its composition
/// (SPEC-0005 Host). All must outlive the composition.
struct HostServices {
    const execution::MonotonicSource* clock = nullptr;
    /// The parent of every participant's cancellation scope.
    execution::CancellationScope* scope = nullptr;
    execution::Executor* cpu = nullptr;
    execution::Executor* blockingIo = nullptr;
    diagnostics::Emitter emitter;
    /// The Runtime's configuration snapshot; null reads as empty.
    const Configuration* configuration = nullptr;
};

/// What a participant's factory and `start` receive: its own identity, scope,
/// executor admission, and the capabilities planning resolved for it. Nothing
/// else is reachable, so there is nothing to look up.
class ParticipantContext {
public:
    ParticipantContext(Composition& composition, std::size_t index) noexcept
        : composition_(&composition), index_(index) {
    }
    ParticipantContext(const ParticipantContext&) = delete;
    ParticipantContext& operator=(const ParticipantContext&) = delete;

    /// A required or present optional capability, typed. Fails if planning did
    /// not resolve it for this participant, or if the provider supplies a
    /// different type or nothing under that name.
    template <typename Interface>
    [[nodiscard]] result::Result<Interface*> capability(const Capability<Interface>& capability) noexcept {
        RAWFRAME_TRY_ASSIGN(const CapabilityObject kObject, resolve(capability.name));
        if (kObject.typeTag != &detail::kTypeTag<Interface>) {
            return result::fail(result::ErrorClass::Internal,
                                kCompositionDomain,
                                code(CompositionError::CapabilityTypeMismatch),
                                "a capability's provider supplies a different type");
        }
        return static_cast<Interface*>(kObject.object);
    }

    /// Whether an optional capability is present in this plan.
    [[nodiscard]] bool has(std::string_view capability) const noexcept;

    [[nodiscard]] std::string_view identity() const noexcept;
    [[nodiscard]] execution::CancellationScope& scope() noexcept;
    [[nodiscard]] execution::CancellationToken token() noexcept {
        return execution::CancellationToken{scope()};
    }
    /// The owner this participant submits executor work as. Its quota was
    /// admitted from the declaration.
    [[nodiscard]] execution::OwnerId owner() const noexcept;
    [[nodiscard]] execution::Executor* cpuExecutor() const noexcept;
    [[nodiscard]] execution::Executor* blockingIoExecutor() const noexcept;
    [[nodiscard]] const execution::MonotonicSource& clock() const noexcept;
    [[nodiscard]] diagnostics::Emitter emitter() const noexcept;
    /// The Runtime's configuration snapshot, empty if the host gave none.
    [[nodiscard]] const Configuration& configuration() const noexcept;

private:
    [[nodiscard]] result::Result<CapabilityObject> resolve(std::string_view capability) noexcept;

    Composition* composition_;
    std::size_t index_;
};

/// SPEC-0005's lifecycle states for one participant.
enum class ParticipantState : std::uint8_t {
    Planned,
    Constructed,
    Starting,
    Running,
    Quiescing,
    Stopped,
    Destroyed,
};

/// A running instance of a plan. `start` constructs every participant in plan
/// order, then starts them in the same order; any failure rolls back exactly
/// what was done, in reverse, and returns the original Error. `stop` quiesces,
/// stops, and destroys in reverse start order. A stopped composition may start
/// again, with new participant instances.
class Composition {
public:
    Composition(const Plan& plan, const HostServices& services) noexcept;
    Composition(const Composition&) = delete;
    Composition& operator=(const Composition&) = delete;
    /// Stops if running.
    ~Composition();

    [[nodiscard]] result::Status start();
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    /// Runs one Host phase: every running participant that declared it, in
    /// plan order. Does nothing unless running.
    void runHostPhase(HostPhase phase, const HostFrame& frame) noexcept;
    [[nodiscard]] ParticipantState state(std::string_view identity) const noexcept;

    /// Stable owner identity for a participant: FNV-1a over its identity.
    [[nodiscard]] static execution::OwnerId ownerFor(std::string_view identity) noexcept;

private:
    friend class ParticipantContext;

    /// A participant's own cancellation scope, built in place because scopes
    /// cannot move.
    struct ScopeHolder {
        ScopeHolder(execution::CancellationScope& parent, execution::FailurePolicy policy)
            : scope(execution::CancellationScope::child(parent, policy)) {
        }
        result::Result<execution::CancellationScope> scope;
    };

    struct Slot {
        const PlannedParticipant* planned = nullptr;
        ParticipantState state = ParticipantState::Planned;
        std::unique_ptr<ScopeHolder> scope;
        std::unique_ptr<ParticipantContext> context;
        ParticipantOwner object;
        bool cpuAdmitted = false;
        bool blockingIoAdmitted = false;
    };

    /// Undoes a partial or complete start: quiesce and stop the first
    /// `started` slots in reverse, then destroy every constructed one.
    void unwind(std::size_t started) noexcept;
    [[nodiscard]] result::Status admit(Slot& slot);

    const Plan* plan_;
    HostServices services_;
    std::vector<Slot> slots_;
    bool running_ = false;
};

} // namespace rawframe::composition
