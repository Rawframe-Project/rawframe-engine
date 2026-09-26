#include "checkpoints.h"

#include "rawframe/base/threads.h"
#include "rawframe/composition/composition.h"
#include "rawframe/world_runtime/checkpoint.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/simulation.h"
#include "rawframe/world_snapshot/checkpoint.h"

#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if RAWFRAME_THREADS
#include <chrono>
#include <thread>
#endif

namespace rawframe::world_runtime {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kCaptured{"world_runtime", "checkpoint_captured"};
constexpr EventIdentity kRestored{"world_runtime", "checkpoint_restored"};
constexpr EventIdentity kCaptureFailed{"world_runtime", "checkpoint_capture_failed"};

constexpr std::string_view kNeeds[] = {kSimulation.name};
constexpr std::string_view kMaybe[] = {kCheckpointPlan.name};

std::unexpected<result::Error> failed(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kWorldRuntimeDomain, code(WorldRuntimeError::CheckpointFailed), why);
}

std::string hex(const world_snapshot::Fingerprint& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::byte kByte : digest) {
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) >> 4U]);
        text.push_back(kDigits[std::to_integer<unsigned>(kByte) & 0xFU]);
    }
    return text;
}

result::Result<std::vector<std::byte>> readArtifact(const std::string& path, std::size_t maximum) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return failed("the checkpoint to restore cannot be opened");
    }
    std::vector<std::byte> bytes;
    std::array<std::byte, 65536> chunk{};
    std::size_t got = 0;
    while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) != 0 && bytes.size() <= maximum) {
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
    }
    const bool kFailed = std::ferror(file) != 0;
    std::fclose(file);
    if (kFailed || bytes.size() > maximum) {
        return failed("the checkpoint to restore cannot be read, or is larger than the snapshot profile allows");
    }
    return bytes;
}

/// Writes beside the target and renames, so a checkpoint is at its path
/// only once it is whole.
result::Status writeArtifact(const std::string& path, std::span<const std::byte> bytes) {
    const std::string kPartial = path + ".partial";
    std::FILE* file = std::fopen(kPartial.c_str(), "wb");
    if (file == nullptr) {
        return failed("the checkpoint cannot be written");
    }
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (std::fclose(file) != 0 || !kWritten || std::rename(kPartial.c_str(), path.c_str()) != 0) {
        std::remove(kPartial.c_str());
        return failed("the checkpoint cannot be written");
    }
    return {};
}

/// One capture past its safe point: sealed on the CPU executor, then
/// written on the blocking-I/O executor, one capture at a time (SPEC-0013's
/// one active encoder task, D227). The Host thread moves it on between
/// ticks; a task only fills in its own step's result.
struct Flight {
    enum class Step : std::uint8_t {
        Sealing,
        Sealed,
        Writing,
        Written
    };
    std::atomic<Step> step{Step::Sealing};
    world_snapshot::StagedCheckpoint staged;
    std::optional<result::Result<world_snapshot::SealedCheckpoint>> sealed;
    result::Status written;
    std::uint64_t tick = 0;
    std::string path;
    execution::MonotonicInstant start;
    std::int64_t pauseMicroseconds = 0;
};

/// Restores a checkpoint before the first tick, and captures at exact ticks.
class Checkpoints final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context) {
        const composition::Configuration& configuration = context.configuration();
        restorePath_ = std::string{configuration.path("checkpoint.restore").value_or("")};
        capturePrefix_ = std::string{configuration.path("checkpoint.capture_prefix").value_or("")};
        std::string_view ticks = configuration.text("checkpoint.capture_ticks").value_or("");
        while (!ticks.empty()) {
            const std::size_t kSpace = ticks.find(' ');
            const std::string_view kWord = ticks.substr(0, kSpace);
            std::uint64_t tick = 0;
            const auto [end, error] = std::from_chars(kWord.data(), kWord.data() + kWord.size(), tick);
            if (error != std::errc{} || end != kWord.data() + kWord.size() ||
                (!captureTicks_.empty() && tick <= captureTicks_.back())) {
                return failed("checkpoint.capture_ticks is increasing tick numbers separated by spaces");
            }
            captureTicks_.push_back(tick);
            ticks = kSpace == std::string_view::npos ? std::string_view{} : ticks.substr(kSpace + 1);
        }
        if (captureTicks_.empty() != capturePrefix_.empty()) {
            return failed("checkpoint.capture_ticks and checkpoint.capture_prefix go together");
        }
        if (restorePath_.empty() && captureTicks_.empty()) {
            return {};
        }
        if (!context.has(kCheckpointPlan.name)) {
            return failed("checkpoints need a plan of what persists, which a game provides");
        }
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(kSimulation));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kCheckpointPlan));
        RAWFRAME_TRY_ASSIGN(projection_, plan_->projection());
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        clock_ = &context.clock();
        cpu_ = context.cpuExecutor();
        io_ = context.blockingIoExecutor();
        owner_ = context.owner();
        if (simulation_ == nullptr) {
            return {};
        }
        if (!restorePath_.empty()) {
            RAWFRAME_TRY(restore());
        }
        holdNext();
        return {};
    }

    /// Between ticks: a capture under way moves on, and one due now is
    /// staged, then the next one is held for.
    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override {
        advance();
        if (simulation_ == nullptr || next_ >= captureTicks_.size() ||
            simulation_->tick().value != captureTicks_[next_]) {
            return;
        }
        // One capture at a time: the one before is finished first.
        finish();
        const auto kStatus = capture();
        if (!kStatus.has_value()) {
            failedAt(simulation_->tick().value, kStatus.error());
        }
        ++next_;
        holdNext();
    }

    /// A capture under way is finished, on the Host thread, where waiting
    /// is allowed.
    void stop() noexcept override {
        finish();
    }

private:
    void holdNext() noexcept {
        // Ticks already past are not waited for: a restore may start later.
        while (next_ < captureTicks_.size() && captureTicks_[next_] < simulation_->tick().value) {
            ++next_;
        }
        simulation_->holdAt(next_ < captureTicks_.size() ? std::optional{world::TickIndex{captureTicks_[next_]}}
                                                         : std::nullopt);
    }

    result::Status restore() {
        const execution::MonotonicInstant kStart = clock_->now();
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kBytes,
                            readArtifact(restorePath_, limits_.maximumArtifactBytes));
        RAWFRAME_TRY_ASSIGN(std::unique_ptr<world::World> candidate, simulation_->candidate());
        RAWFRAME_TRY_ASSIGN(const world_snapshot::CheckpointFacts kFacts,
                            world_snapshot::restore(kBytes, *projection_, plan_->identity(), limits_, *candidate));
        const world::TickRate kRate = simulation_->rate();
        // Both reduced: equal rates are equal fractions.
        if (static_cast<std::uint64_t>(kFacts.rate.ticks) * kRate.seconds !=
            static_cast<std::uint64_t>(kRate.ticks) * kFacts.rate.seconds) {
            return failed("the checkpoint was taken at another tick rate");
        }
        simulation_->replace(std::move(candidate), kFacts.tick);
        emitter_.log(diagnostics::Severity::Info,
                     kRestored,
                     "the World was restored from a checkpoint",
                     {diagnostics::field("tick", kFacts.tick.value),
                      diagnostics::field("entities", kFacts.entities),
                      diagnostics::field("rows", kFacts.rows),
                      diagnostics::field("digest", std::string_view{hex(kFacts.digest)}),
                      // SPEC-0013's restore deadline (D215).
                      diagnostics::field("restoreMs", (clock_->now() - kStart).nanoseconds / 1'000'000)});
        return {};
    }

    /// Stages the World at its safe point, the only part that holds it,
    /// and hands the rest to the executors (D227).
    result::Status capture() {
        const execution::MonotonicInstant kStart = clock_->now();
        const world::TickIndex kTick = simulation_->tick();
        RAWFRAME_TRY_ASSIGN(world_snapshot::StagedCheckpoint staged,
                            world_snapshot::stage(*simulation_->world(),
                                                  *projection_,
                                                  world_snapshot::CaptureSettings{.tick = kTick,
                                                                                  .rate = simulation_->rate(),
                                                                                  .identity = plan_->identity(),
                                                                                  .limits = limits_}));
        flight_ = std::make_unique<Flight>();
        flight_->staged = std::move(staged);
        flight_->tick = kTick.value;
        flight_->path = capturePrefix_ + std::to_string(kTick.value) + ".rfsn";
        flight_->start = kStart;
        flight_->pauseMicroseconds = (clock_->now() - kStart).nanoseconds / 1'000;
        run(cpu_, [flight = flight_.get()]() noexcept {
            flight->sealed.emplace(world_snapshot::seal(std::move(flight->staged)));
            flight->step.store(Flight::Step::Sealed, std::memory_order_release);
        });
        return {};
    }

    /// Runs `task` on `executor`, or here when there is none or it refuses;
    /// a refused task is left as it was.
    void run(execution::Executor* executor, execution::Task&& task) noexcept {
        if (executor == nullptr ||
            !executor->submit(owner_, execution::Priority::Background, std::move(task)).has_value()) {
            task();
        }
    }

    /// Moves a capture under way on: a sealed one is written, a written one
    /// reported.
    void advance() noexcept {
        if (flight_ == nullptr) {
            return;
        }
        switch (flight_->step.load(std::memory_order_acquire)) {
        case Flight::Step::Sealed:
            if (!flight_->sealed->has_value()) {
                failedAt(flight_->tick, flight_->sealed->error());
                flight_.reset();
                return;
            }
            flight_->step.store(Flight::Step::Writing, std::memory_order_release);
            run(io_, [flight = flight_.get()]() noexcept {
                flight->written = writeArtifact(flight->path, (*flight->sealed)->bytes);
                flight->step.store(Flight::Step::Written, std::memory_order_release);
            });
            return;
        case Flight::Step::Written: {
            if (!flight_->written.has_value()) {
                failedAt(flight_->tick, flight_->written.error());
                flight_.reset();
                return;
            }
            const world_snapshot::SealedCheckpoint& kSealed = **flight_->sealed;
            emitter_.log(diagnostics::Severity::Info,
                         kCaptured,
                         "a checkpoint was captured",
                         {diagnostics::field("tick", flight_->tick),
                          diagnostics::field("bytes", kSealed.bytes.size()),
                          diagnostics::field("digest", std::string_view{hex(kSealed.digest)}),
                          // SPEC-0013's safe-point pause: how long the World
                          // was held for the capture (D227).
                          diagnostics::field("pauseUs", flight_->pauseMicroseconds),
                          // SPEC-0013's capture deadline, the write included (D215).
                          diagnostics::field("captureMs", (clock_->now() - flight_->start).nanoseconds / 1'000'000)});
            flight_.reset();
            return;
        }
        case Flight::Step::Sealing:
        case Flight::Step::Writing:
            return;
        }
    }

    /// Finishes a capture under way, helping its executors meanwhile.
    void finish() noexcept {
        while (flight_ != nullptr) {
            advance();
            if (flight_ == nullptr) {
                return;
            }
            if ((cpu_ != nullptr && cpu_->runOne()) || (io_ != nullptr && io_->runOne())) {
                continue;
            }
#if RAWFRAME_THREADS
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
#endif
        }
    }

    void failedAt(std::uint64_t tick, const result::Error& error) noexcept {
        emitter_.log(diagnostics::Severity::Error,
                     kCaptureFailed,
                     "a checkpoint was not captured",
                     {diagnostics::field("tick", tick), diagnostics::field("error", error.description())});
    }

    const execution::MonotonicSource* clock_ = nullptr;
    execution::Executor* cpu_ = nullptr;
    execution::Executor* io_ = nullptr;
    execution::OwnerId owner_;
    Simulation* simulation_ = nullptr;
    const CheckpointPlan* plan_ = nullptr;
    const world_snapshot::SnapshotProjection* projection_ = nullptr;
    world_snapshot::SnapshotLimits limits_;
    diagnostics::Emitter emitter_;
    std::string restorePath_;
    std::string capturePrefix_;
    std::vector<std::uint64_t> captureTicks_;
    std::size_t next_ = 0;
    /// The capture under way, if any.
    std::unique_ptr<Flight> flight_;
};

result::Result<composition::ParticipantOwner> makeCheckpoints(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<Checkpoints>();
    RAWFRAME_TRY(participant->load(context));
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

void registerCheckpoints(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.world_runtime.checkpoints",
        .factory = &makeCheckpoints,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kNeeds,
        .optionalCapabilities = kMaybe,
        // One task at a time on each: the seal, then the write (D227).
        .executor = {.cpu = true, .blockingIo = true, .quota = {.maximumPendingTasks = 1}},
        // Stopping finishes a capture under way: milliseconds for the
        // canonical crowd, within the budget with room to spare.
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(500)},
        .observabilityIdentity = "world_runtime.checkpoints",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Maintenance),
    });
}

} // namespace rawframe::world_runtime
