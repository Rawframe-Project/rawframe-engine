#include "checkpoints.h"
#include "rawframe/composition/composition.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/save.h"
#include "rawframe/world_runtime/simulation.h"
#include "rawframe/world_save/errors.h"
#include "rawframe/world_save/store.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rawframe::world_runtime {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kLoaded{"world_runtime", "save_loaded"};
constexpr EventIdentity kAbsent{"world_runtime", "save_absent"};
constexpr EventIdentity kKept{"world_runtime", "save_kept"};
constexpr EventIdentity kKeepFailed{"world_runtime", "save_keep_failed"};

constexpr std::string_view kNeeds[] = {kSimulation.name};
constexpr std::string_view kMaybe[] = {kSavePlan.name};

std::unexpected<result::Error> misconfigured(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kWorldRuntimeDomain, code(WorldRuntimeError::SaveMisconfigured), why);
}

/// Loads the operator's slot into the World when it starts, and keeps it at
/// the declared points: every `save.every_seconds`, and when it stops. A
/// capture happens between ticks, on the Host thread; the write, on a
/// blocking-I/O worker, one at a time. A kept save that does not read stops
/// the start, so the next keep cannot overwrite what an operator may want
/// back.
class Saves final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        const auto kDirectory = configuration.text("save.directory");
        if (!kDirectory.has_value()) {
            return {};
        }
        if (!context.has(kSavePlan.name)) {
            return misconfigured("save.directory needs a game that declares a save");
        }
        slot_ = std::string{configuration.text("save.slot").value_or("world")};
        const auto kSpace = configuration.text("save.namespace");
        const base::Bits128Parse kParsed = base::parseBits128Hex(kSpace.value_or(""));
        if (!kParsed.parsed || kParsed.value == base::Bits128{}) {
            return misconfigured("save.namespace is the persistence namespace, 32 lowercase hex digits, not nought");
        }
        space_ = kParsed.value;
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kEvery, configuration.unsignedInteger("save.every_seconds", 0));
        if (kEvery > 86'400) {
            return misconfigured("save.every_seconds is at most a day");
        }
        every_ = execution::MonotonicDuration::fromSeconds(static_cast<std::int64_t>(kEvery));
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(kSimulation));
        RAWFRAME_TRY_ASSIGN(const SavePlan* plan, context.capability(kSavePlan));
        RAWFRAME_TRY_ASSIGN(declaration_, plan->saveDeclaration());
        store_.emplace(std::string{*kDirectory});
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (!store_.has_value()) {
            return {};
        }
        io_ = context.blockingIoExecutor();
        owner_ = context.owner();
        lastKept_ = context.clock().now();
        auto bytes = store_->load(slot_, limits_.maximumBytes);
        if (!bytes.has_value()) {
            if (bytes.error().domain() == world_save::kSaveDomain &&
                bytes.error().code() == world_save::code(world_save::SaveError::Absent)) {
                emitter_.log(diagnostics::Severity::Info,
                             kAbsent,
                             "no save is kept yet; the World starts fresh",
                             {diagnostics::field("slot", std::string_view{slot_})});
                started_ = true;
                return {};
            }
            return std::unexpected<result::Error>{std::move(bytes).error()};
        }
        world::World& world = *simulation_->world();
        RAWFRAME_TRY_ASSIGN(const world_save::StagedSave kStaged,
                            world_save::read(*bytes, *declaration_, world.registry(), space_, limits_));
        RAWFRAME_TRY_ASSIGN(const world_save::Applied kApplied, world_save::apply(kStaged, *declaration_, world));
        emitter_.log(diagnostics::Severity::Info,
                     kLoaded,
                     "the World took its save",
                     {diagnostics::field("slot", std::string_view{slot_}),
                      diagnostics::field("updated", kApplied.updated),
                      diagnostics::field("created", kApplied.created)});
        started_ = true;
        return {};
    }

    /// Between ticks: reports a write that finished, and starts the next
    /// when one is due and none is under way.
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (!started_) {
            return;
        }
        report();
        if (every_.nanoseconds == 0 || frame.now - lastKept_ < every_ || writing_.load(std::memory_order_acquire)) {
            return;
        }
        lastKept_ = frame.now;
        if (!captureNext()) {
            return;
        }
        writing_.store(true, std::memory_order_release);
        const auto kSubmitted = io_ != nullptr ? io_->submit(owner_,
                                                             execution::Priority::Background,
                                                             [this]() noexcept {
                                                                 write();
                                                             })
                                               : result::Status{misconfigured("saves need the blocking-I/O executor")};
        if (!kSubmitted.has_value()) {
            // Written here instead: late, but kept.
            write();
            report();
        }
    }

    /// The last keep, once any write under way has finished, on the Host
    /// thread, where blocking is allowed. A start that failed keeps
    /// nothing: the kept save may be one that did not read.
    void stop() noexcept override {
        if (!started_ || simulation_->world() == nullptr) {
            return;
        }
        while (writing_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        report();
        if (captureNext()) {
            writing_.store(true, std::memory_order_release);
            write();
            report();
        }
    }

private:
    /// Captures into the pending bytes; false, and logged, if it cannot.
    bool captureNext() noexcept {
        auto bytes = world_save::capture(*simulation_->world(), *declaration_, space_, limits_);
        if (!bytes.has_value()) {
            emitter_.log(diagnostics::Severity::Error,
                         kKeepFailed,
                         "the World could not be saved",
                         {diagnostics::field("error", bytes.error().description())});
            return false;
        }
        pending_ = std::move(*bytes);
        return true;
    }

    void write() noexcept {
        auto kept = store_->keep(slot_, pending_);
        failure_ = kept.has_value() ? std::string{} : std::string{kept.error().description()};
        written_ = pending_.size();
        finished_.store(true, std::memory_order_release);
        writing_.store(false, std::memory_order_release);
    }

    void report() noexcept {
        if (!finished_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        if (failure_.empty()) {
            emitter_.log(diagnostics::Severity::Info,
                         kKept,
                         "the World was saved",
                         {diagnostics::field("slot", std::string_view{slot_}), diagnostics::field("bytes", written_)});
        } else {
            emitter_.log(diagnostics::Severity::Error,
                         kKeepFailed,
                         "a save could not be kept; the one kept before stays",
                         {diagnostics::field("error", std::string_view{failure_})});
        }
    }

    Simulation* simulation_ = nullptr;
    const world_save::SaveDeclaration* declaration_ = nullptr;
    std::optional<world_save::DirectorySaveStore> store_;
    world_save::SaveLimits limits_;
    std::string slot_;
    base::Bits128 space_;
    execution::MonotonicDuration every_;
    execution::MonotonicInstant lastKept_;
    execution::Executor* io_ = nullptr;
    execution::OwnerId owner_;
    diagnostics::Emitter emitter_;
    /// The bytes being written; the writer alone touches them while
    /// `writing_` is set, and it sets `finished_` before clearing it.
    std::vector<std::byte> pending_;
    std::string failure_;
    std::size_t written_ = 0;
    /// Whether the slot was loaded, or found absent: only then is keeping
    /// it safe.
    bool started_ = false;
    std::atomic<bool> writing_{false};
    std::atomic<bool> finished_{false};
};

result::Result<composition::ParticipantOwner> makeSaves(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<Saves>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerSaves(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_runtime.saves",
        .factory = &makeSaves,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .executor = {.cpu = false, .blockingIo = true, .quota = {.maximumPendingTasks = 1}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(1000)},
        .observabilityIdentity = "world_runtime.saves",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::world_runtime
