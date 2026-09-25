#include "rawframe/base/threads.h"
#include "rawframe/composition/composition.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/players.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/save.h"
#include "rawframe/world_runtime/simulation.h"
#include "rawframe/world_save/errors.h"
#include "rawframe/world_save/store.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

// Saves are kept in a directory, so only where there are files.
#if RAWFRAME_FILE_SYSTEM

namespace rawframe::world_runtime {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kLoaded{"world_runtime", "save_loaded"};
constexpr EventIdentity kAbsent{"world_runtime", "save_absent"};
constexpr EventIdentity kKept{"world_runtime", "save_kept"};
constexpr EventIdentity kKeepFailed{"world_runtime", "save_keep_failed"};
constexpr EventIdentity kPlayerLoaded{"world_runtime", "player_save_loaded"};
constexpr EventIdentity kPlayerRefused{"world_runtime", "player_save_refused"};
constexpr EventIdentity kSuperseded{"world_runtime", "save_superseded"};

constexpr std::string_view kNeeds[] = {kSimulation.name};
constexpr std::string_view kMaybe[] = {kSavePlan.name};
constexpr std::string_view kProvides[] = {kPlayerPresence.name};
// After the checkpoints participant, so a restore has replaced the World
// before the World's save would be applied to it.
constexpr std::string_view kAfter[] = {"rawframe.world_runtime.checkpoints"};

std::unexpected<result::Error> misconfigured(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kWorldRuntimeDomain, code(WorldRuntimeError::SaveMisconfigured), why);
}

/// A player's slot: `p-` and the identity in hex.
std::string playerSlot(PlayerIdentity identity) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string slot = "p-";
    for (const std::uint64_t kWord : {identity.value.high, identity.value.low}) {
        for (int shift = 60; shift >= 0; shift -= 4) {
            slot.push_back(kDigits[(kWord >> static_cast<unsigned>(shift)) & 0xFU]);
        }
    }
    return slot;
}

/// The World's save and each player's (ADR-0057). The World's slot is
/// loaded when the World starts and kept every `save.every_seconds` and
/// when it stops; a player's is applied to the player's entity when they
/// join and kept when they leave. Captures happen between ticks on the Host
/// thread; writes go on a queue one blocking-I/O worker empties in order, so
/// two writes to one slot keep the later. A kept save that does not read is
/// never written over: the World's stops the start, and a player's leaves
/// that player unkept until an operator looks.
class Saves final : public composition::Participant, public PlayerPresence {
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
        restoring_ = configuration.text("checkpoint.restore").has_value();
        const base::Bits128Parse kParsed = base::parseBits128Hex(configuration.text("save.namespace").value_or(""));
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
        players_ = plan->playerSaveDeclaration();
        auto world = plan->saveDeclaration();
        declaration_ = world.has_value() ? *world : nullptr;
        if (declaration_ == nullptr && players_ == nullptr) {
            return misconfigured("save.directory needs a game that declares a save");
        }
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
        if (declaration_ != nullptr && restoring_) {
            // The checkpoint is the whole World, and the operator chose it.
            emitter_.log(diagnostics::Severity::Warning,
                         kSuperseded,
                         "the World was restored from a checkpoint, so its save is not applied; the next keep "
                         "writes the restored World",
                         {diagnostics::field("slot", std::string_view{slot_})});
        } else if (declaration_ != nullptr) {
            RAWFRAME_TRY(loadWorld());
        }
        started_ = true;
        return {};
    }

    /// Between ticks: reports finished writes, and keeps the World when it
    /// is due.
    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (!started_) {
            return;
        }
        report();
        if (declaration_ == nullptr || every_.nanoseconds == 0 || frame.now - lastKept_ < every_) {
            return;
        }
        lastKept_ = frame.now;
        keepWorld();
    }

    /// The World's last keep, and every write queued, on the Host thread,
    /// where blocking is allowed. A start that failed keeps nothing: the
    /// kept save may be one that did not read.
    void stop() noexcept override {
        if (!started_ || simulation_->world() == nullptr) {
            return;
        }
        if (declaration_ != nullptr) {
            keepWorld();
        }
        // Helping: the write runs here if no worker has taken it.
        while (writing_.load(std::memory_order_acquire)) {
            if (io_ != nullptr && io_->runOne()) {
                continue;
            }
#if RAWFRAME_THREADS
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
#else
            RAWFRAME_PANIC("a save being written that nothing here can finish");
#endif
        }
        drain();
        report();
    }

    void joined(world::World& world, world::EntityHandle player, PlayerIdentity identity) noexcept override {
        if (!started_ || players_ == nullptr) {
            return;
        }
        const std::string kSlot = playerSlot(identity);
        auto bytes = store_->load(kSlot, limits_.maximumBytes);
        if (!bytes.has_value() && bytes.error().domain() == world_save::kSaveDomain &&
            bytes.error().code() == world_save::code(world_save::SaveError::Absent)) {
            return;
        }
        auto applied = std::move(bytes).and_then([&](const std::vector<std::byte>& kBytes) {
            return world_save::read(kBytes, *players_, world.registry(), space_, limits_)
                .and_then([&](const world_save::StagedSave& staged) {
                    return world_save::applyTo(
                        staged, *players_, world, player, world::PersistentEntityId{identity.value});
                });
        });
        if (!applied.has_value()) {
            unkept_.insert(identity);
            emitter_.log(diagnostics::Severity::Error,
                         kPlayerRefused,
                         "a player's save did not apply; they play without it, and it is not written over",
                         {diagnostics::field("slot", std::string_view{kSlot}),
                          diagnostics::field("error", applied.error().description())});
            return;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kPlayerLoaded,
                     "a player took their save",
                     {diagnostics::field("slot", std::string_view{kSlot})});
    }

    void leaving(world::World& world, world::EntityHandle player, PlayerIdentity identity) noexcept override {
        if (!started_ || players_ == nullptr || unkept_.contains(identity)) {
            return;
        }
        auto bytes = world_save::captureEntity(
            world, *players_, space_, player, world::PersistentEntityId{identity.value}, limits_);
        if (!bytes.has_value()) {
            emitter_.log(diagnostics::Severity::Error,
                         kKeepFailed,
                         "a player could not be saved",
                         {diagnostics::field("error", bytes.error().description())});
            return;
        }
        queue(playerSlot(identity), std::move(*bytes));
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kPlayerPresence.name) {
            return composition::provideAs<PlayerPresence>(*this);
        }
        return {};
    }

private:
    struct Keep {
        std::string slot;
        std::vector<std::byte> bytes;
    };
    struct Outcome {
        std::string slot;
        std::size_t bytes = 0;
        std::string failure;
    };

    result::Status loadWorld() {
        auto bytes = store_->load(slot_, limits_.maximumBytes);
        if (!bytes.has_value()) {
            if (bytes.error().domain() == world_save::kSaveDomain &&
                bytes.error().code() == world_save::code(world_save::SaveError::Absent)) {
                emitter_.log(diagnostics::Severity::Info,
                             kAbsent,
                             "no save is kept yet; the World starts fresh",
                             {diagnostics::field("slot", std::string_view{slot_})});
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
                      diagnostics::field("created", kApplied.created),
                      diagnostics::field("migrated", kStaged.migrated)});
        return {};
    }

    void keepWorld() noexcept {
        auto bytes = world_save::capture(*simulation_->world(), *declaration_, space_, limits_);
        if (!bytes.has_value()) {
            emitter_.log(diagnostics::Severity::Error,
                         kKeepFailed,
                         "the World could not be saved",
                         {diagnostics::field("error", bytes.error().description())});
            return;
        }
        queue(slot_, std::move(*bytes));
    }

    /// Queues a write and, if no worker is writing, starts one; written
    /// here instead if none can be started.
    void queue(std::string slot, std::vector<std::byte> bytes) noexcept {
        {
            const std::lock_guard kLock{mutex_};
            queued_.push_back(Keep{.slot = std::move(slot), .bytes = std::move(bytes)});
        }
        if (writing_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto kSubmitted =
            io_ != nullptr ? io_->submit(owner_,
                                         execution::Priority::Background,
                                         [this]() noexcept {
                                             drain();
                                             writing_.store(false, std::memory_order_release);
                                             // A keep queued after the last pop and
                                             // before the flag cleared would wait for
                                             // the next; so take it now.
                                             if (pending() && !writing_.exchange(true, std::memory_order_acq_rel)) {
                                                 drain();
                                                 writing_.store(false, std::memory_order_release);
                                             }
                                         })
                           : result::Status{misconfigured("saves need the blocking-I/O executor")};
        if (!kSubmitted.has_value()) {
            drain();
            writing_.store(false, std::memory_order_release);
        }
    }

    bool pending() noexcept {
        const std::lock_guard kLock{mutex_};
        return !queued_.empty();
    }

    /// Writes everything queued, in order.
    void drain() noexcept {
        for (;;) {
            Keep next;
            {
                const std::lock_guard kLock{mutex_};
                if (queued_.empty()) {
                    return;
                }
                next = std::move(queued_.front());
                queued_.pop_front();
            }
            auto kept = store_->keep(next.slot, next.bytes);
            const std::lock_guard kLock{mutex_};
            outcomes_.push_back(
                Outcome{.slot = std::move(next.slot),
                        .bytes = next.bytes.size(),
                        .failure = kept.has_value() ? std::string{} : std::string{kept.error().description()}});
        }
    }

    void report() noexcept {
        std::vector<Outcome> outcomes;
        {
            const std::lock_guard kLock{mutex_};
            outcomes.swap(outcomes_);
        }
        for (const Outcome& outcome : outcomes) {
            if (outcome.failure.empty()) {
                emitter_.log(diagnostics::Severity::Info,
                             kKept,
                             "a save was kept",
                             {diagnostics::field("slot", std::string_view{outcome.slot}),
                              diagnostics::field("bytes", outcome.bytes)});
            } else {
                emitter_.log(diagnostics::Severity::Error,
                             kKeepFailed,
                             "a save could not be kept; the one kept before stays",
                             {diagnostics::field("slot", std::string_view{outcome.slot}),
                              diagnostics::field("error", std::string_view{outcome.failure})});
            }
        }
    }

    Simulation* simulation_ = nullptr;
    const world_save::SaveDeclaration* declaration_ = nullptr;
    const world_save::SaveDeclaration* players_ = nullptr;
    std::optional<world_save::DirectorySaveStore> store_;
    world_save::SaveLimits limits_;
    std::string slot_;
    base::Bits128 space_;
    execution::MonotonicDuration every_;
    execution::MonotonicInstant lastKept_;
    execution::Executor* io_ = nullptr;
    execution::OwnerId owner_;
    diagnostics::Emitter emitter_;
    /// Whether the World's slot was loaded, or found absent: only then is
    /// keeping safe.
    bool started_ = false;
    /// Whether a checkpoint restore brings the World instead.
    bool restoring_ = false;
    /// Players whose kept save did not apply.
    std::set<PlayerIdentity> unkept_;
    base::Mutex mutex_;
    std::deque<Keep> queued_;
    std::vector<Outcome> outcomes_;
    std::atomic<bool> writing_{false};
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
        .providedCapabilities = kProvides,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        .requiredParticipants = kAfter,
        .executor = {.cpu = false, .blockingIo = true, .quota = {.maximumPendingTasks = 1}},
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(1000)},
        .observabilityIdentity = "world_runtime.saves",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::world_runtime

#endif
