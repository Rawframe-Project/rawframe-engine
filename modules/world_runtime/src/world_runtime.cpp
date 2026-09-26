#include "checkpoints.h"
#include "rawframe/composition/composition.h"
#include "rawframe/world_runtime/errors.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_runtime/simulation.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace rawframe::world_runtime {

namespace {

constexpr std::string_view kIdentity = "rawframe.world_runtime.world";
constexpr std::string_view kProvided[] = {kSimulation.name};

constexpr diagnostics::EventIdentity kSystemFailed{"world_runtime", "system_failed"};
constexpr diagnostics::EventIdentity kTickFailed{"world_runtime", "tick_failed"};
constexpr diagnostics::EventIdentity kTickDebt{"world_runtime", "tick_debt"};
constexpr diagnostics::EventIdentity kOverloadedEvent{"world_runtime", "overloaded"};
constexpr diagnostics::EventIdentity kTickSummary{"world_runtime", "tick_summary"};

/// Tick durations kept for the summary: the most recent this many.
constexpr std::size_t kKeptTickDurations = std::size_t{1} << 16U;

std::unexpected<result::Error> alreadyStarted(std::string_view description) {
    return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                       kWorldRuntimeDomain,
                                                       code(WorldRuntimeError::AlreadyStarted),
                                                       description)
                                              .error()};
}

struct Settings {
    world::TickRate rate;
    std::uint32_t maximumTicksPerIteration = 4;
    world::WorldSettings world;
    /// How far behind a World is degraded (SPEC-0013's degraded debt
    /// threshold), and past which it is overloaded at once (its shutdown
    /// threshold; nought never), in milliseconds of ticks (D212).
    std::uint64_t degradedMs = 1000;
    std::uint64_t debtLimitMs = 0;
    /// How long a World may stay degraded before it is overloaded; nought
    /// never.
    execution::MonotonicDuration overloadAfter;
};

result::Result<Settings> readSettings(const composition::Configuration& configuration) {
    constexpr std::uint64_t kMaximum32 = std::numeric_limits<std::uint32_t>::max();
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kTicks, configuration.unsignedInteger("world.tick_rate", 60));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeconds, configuration.unsignedInteger("world.tick_rate_seconds", 1));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kCatchUp,
                        configuration.unsignedInteger("world.maximum_ticks_per_iteration", 4));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeed, configuration.unsignedInteger("world.root_seed", 0));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kEntities,
                        configuration.unsignedInteger("world.maximum_entities", std::uint64_t{1} << 20U));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kOverloadMs, configuration.unsignedInteger("world.overload_ms", 30'000));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kDegradedMs, configuration.unsignedInteger("world.degraded_ms", 1000));
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kDebtLimitMs, configuration.unsignedInteger("world.debt_limit_ms", 0));
    if (kTicks > kMaximum32 || kSeconds > kMaximum32 || kCatchUp == 0 || kCatchUp > kMaximum32 || kEntities == 0 ||
        kEntities > kMaximum32 || kOverloadMs > kMaximum32 || kDegradedMs == 0 || kDegradedMs > kMaximum32 ||
        kDebtLimitMs > kMaximum32 || (kDebtLimitMs != 0 && kDebtLimitMs < kDegradedMs)) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldRuntimeDomain,
                            code(WorldRuntimeError::SettingOutOfRange),
                            "a world setting is out of range");
    }
    RAWFRAME_TRY_ASSIGN(const world::TickRate kRate,
                        world::TickRate::of(static_cast<std::uint32_t>(kTicks), static_cast<std::uint32_t>(kSeconds)));
    return Settings{
        .rate = kRate,
        .maximumTicksPerIteration = static_cast<std::uint32_t>(kCatchUp),
        .world = world::WorldSettings{.maximumEntities = static_cast<std::uint32_t>(kEntities),
                                      .rootSeed = world::RootSeed{kSeed}},
        .degradedMs = kDegradedMs,
        .debtLimitMs = kDebtLimitMs,
        .overloadAfter = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kOverloadMs)),
    };
}

/// The World participant: owns the registry, World, schedule, and pacer, and
/// runs due ticks in `run_worlds`.
class WorldRuntime final : public composition::Participant, public Simulation {
public:
    explicit WorldRuntime(Settings settings) noexcept : settings_(settings) {
    }

    result::Status addComponent(const schema::ComponentDescriptor& descriptor) override {
        if (started_) {
            return alreadyStarted("components are added before the World starts");
        }
        registryBuilder_.add(descriptor);
        return {};
    }

    result::Status addSystems(SystemContributor& contributor) override {
        if (started_) {
            return alreadyStarted("systems are added before the World starts");
        }
        contributors_.push_back(&contributor);
        return {};
    }

    world::World* world() noexcept override {
        return world_.get();
    }
    world::TickIndex tick() const noexcept override {
        return tick_;
    }
    world::TickRate rate() const noexcept override {
        return settings_.rate;
    }
    std::uint64_t generation() const noexcept override {
        return generation_;
    }

    result::Result<std::unique_ptr<world::World>> candidate() const override {
        if (registry_ == nullptr) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::FailedPrecondition,
                             kWorldRuntimeDomain,
                             code(WorldRuntimeError::NotStarted),
                             "a candidate World exists only once the World has started")
                    .error()};
        }
        return std::make_unique<world::World>(registry_, settings_.world);
    }

    void replace(std::unique_ptr<world::World> world, world::TickIndex next) noexcept override {
        world_ = std::move(world);
        tick_ = next;
        ++generation_;
    }

    void holdAt(std::optional<world::TickIndex> limit) noexcept override {
        hold_ = limit;
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        started_ = true;
        context_ = &context;
        emitter_ = context.emitter();
        clock_ = &context.clock();
        durations_.reserve(kKeptTickDurations);
        RAWFRAME_TRY_ASSIGN(registry_, registryBuilder_.freeze());
        world_ = std::make_unique<world::World>(registry_, settings_.world);
        std::vector<world::SystemDeclaration> declarations;
        for (SystemContributor* contributor : contributors_) {
            RAWFRAME_TRY(contributor->declareSystems(*registry_, declarations));
        }
        RAWFRAME_TRY_ASSIGN(world::Schedule schedule, world::Schedule::compile(declarations, *registry_));
        schedule_.emplace(std::move(schedule));
        pacer_.emplace(settings_.rate, settings_.maximumTicksPerIteration, context.clock().now());
        running_ = true;
        return {};
    }

    void quiesce() noexcept override {
        running_ = false;
    }

    void stop() noexcept override {
        summarize();
        schedule_.reset();
        world_.reset();
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (!running_) {
            return;
        }
        const world::TickPacer::Due kDue = pacer_->due(frame.now);
        for (std::uint32_t index = 0; index < kDue.run && running_ && (!hold_ || tick_ < *hold_); ++index) {
            const execution::MonotonicInstant kStart = clock_->now();
            auto report = schedule_->runTick(*world_, tick_, settings_.rate, emitter_);
            record(clock_->now() - kStart);
            if (!report.has_value()) {
                // The World's state is no longer trustworthy: stop ticking and
                // say so once. The Host decides what happens next.
                running_ = false;
                emitter_.log(diagnostics::Severity::Critical,
                             kTickFailed,
                             "a World tick failed at its barrier",
                             {diagnostics::field("tick", tick_.value)});
                context_->reportHealth(composition::Health::Unhealthy, "tick_failed");
                return;
            }
            for (const auto& failure : report->failures) {
                // Why, as the system's owner recorded it: `key: value` pairs.
                std::string context;
                for (const result::ContextField& each : failure.error.context()) {
                    context += context.empty() ? "" : "; ";
                    context += each.key;
                    context += ": ";
                    context += each.value;
                }
                emitter_.log(diagnostics::Severity::Warning,
                             kSystemFailed,
                             "a system returned an error; its commands were discarded",
                             {diagnostics::field("system", std::string_view{failure.system}),
                              diagnostics::field("tick", report->tick.value),
                              diagnostics::field("error", failure.error.description()),
                              diagnostics::field("errorClass", result::describe(failure.error.errorClass())),
                              diagnostics::field("context", std::string_view{context})});
            }
            pacer_->ran(1);
        }
        if (kDue.debt != 0) {
            emitter_.gauge(kTickDebt, diagnostics::Unit::Count, static_cast<double>(kDue.debt));
        }
        // SPEC-0012's tick progress, on SPEC-0013's thresholds (D212): a
        // World `world.degraded_ms` or more behind is degraded, and healthy
        // again once it has caught up; one degraded for longer than
        // `world.overload_ms`, or `world.debt_limit_ms` behind, is
        // overloaded, and stays so.
        const std::uint64_t kDebtMs = kDue.debt * 1000 * settings_.rate.seconds / settings_.rate.ticks;
        if (overloaded_) {
            return;
        }
        if (kDebtMs < settings_.degradedMs) {
            behindSince_.reset();
            context_->reportHealth(composition::Health::Healthy, {});
            return;
        }
        if (!behindSince_.has_value()) {
            behindSince_ = frame.now;
        }
        if ((settings_.overloadAfter.nanoseconds != 0 && frame.now - *behindSince_ >= settings_.overloadAfter) ||
            (settings_.debtLimitMs != 0 && kDebtMs >= settings_.debtLimitMs)) {
            overloaded_ = true;
            emitter_.log(diagnostics::Severity::Critical,
                         kOverloadedEvent,
                         "the World has been behind for longer than it may be",
                         {diagnostics::field("debt", kDue.debt),
                          diagnostics::field("behindMs", (frame.now - *behindSince_).nanoseconds / 1'000'000)});
            context_->reportHealth(composition::Health::Unhealthy, composition::kOverloaded);
            return;
        }
        context_->reportHealth(composition::Health::Degraded, "tick_debt");
    }

    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kSimulation.name) {
            return composition::provideAs<Simulation>(*this);
        }
        return {};
    }

private:
    void record(execution::MonotonicDuration duration) {
        if (durations_.size() < kKeptTickDurations) {
            durations_.push_back(duration.nanoseconds);
        } else {
            durations_[recorded_ % kKeptTickDurations] = duration.nanoseconds;
        }
        ++recorded_;
    }

    /// How long ticks took, for SPEC-0013's tick budget: once, at stop.
    void summarize() {
        if (durations_.empty()) {
            return;
        }
        std::vector<std::int64_t> sorted = durations_;
        std::sort(sorted.begin(), sorted.end());
        const auto kAt = [&sorted](double fraction) {
            return static_cast<double>(
                       sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1))]) /
                   1000.0;
        };
        emitter_.log(diagnostics::Severity::Info,
                     kTickSummary,
                     "World tick durations, in microseconds",
                     {diagnostics::field("ticks", recorded_),
                      diagnostics::field("p50", kAt(0.50)),
                      diagnostics::field("p95", kAt(0.95)),
                      diagnostics::field("p99", kAt(0.99)),
                      diagnostics::field("max", kAt(1.0))});
    }

    Settings settings_;
    const execution::MonotonicSource* clock_ = nullptr;
    std::vector<std::int64_t> durations_;
    std::uint64_t recorded_ = 0;
    schema::RegistryBuilder registryBuilder_;
    std::vector<SystemContributor*> contributors_;
    std::shared_ptr<const schema::SchemaRegistry> registry_;
    std::unique_ptr<world::World> world_;
    std::optional<world::Schedule> schedule_;
    std::optional<world::TickPacer> pacer_;
    world::TickIndex tick_;
    std::uint64_t generation_ = 0;
    std::optional<world::TickIndex> hold_;
    composition::ParticipantContext* context_ = nullptr;
    diagnostics::Emitter emitter_;
    bool started_ = false;
    bool running_ = false;
    /// When the World fell more than a second behind, while it stays there;
    /// and whether it has been behind too long.
    std::optional<execution::MonotonicInstant> behindSince_;
    bool overloaded_ = false;
};

result::Result<composition::ParticipantOwner> makeWorldRuntime(composition::ParticipantContext& context) noexcept {
    RAWFRAME_TRY_ASSIGN(const Settings kSettings, readSettings(context.configuration()));
    return composition::ParticipantOwner{new WorldRuntime{kSettings}};
}

} // namespace

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = kIdentity,
        .factory = &makeWorldRuntime,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kProvided,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "world_runtime.world",
        .budgetOwner = "world",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::RunWorlds),
    });
    registerCheckpoints(registrar);
}

} // namespace rawframe::world_runtime
