#include "rawframe/base/sha256.h"
#include "rawframe/composition/composition.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/network/transport.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/random.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/client_worlds.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/input_source.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_replication/server.h"
#include "rawframe/world_runtime/players.h"
#include "rawframe/world_runtime/simulation.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rawframe::world_replication {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kListening{"replication", "listening"};
constexpr EventIdentity kServerSummary{"replication", "server_summary"};
constexpr EventIdentity kDivergence{"replication", "prediction_divergence"};
constexpr EventIdentity kRollbackAlarm{"replication", "rollback_rate_alarm"};
constexpr EventIdentity kBotsSummary{"replication", "bots_summary"};
constexpr EventIdentity kBotsAdmitted{"replication", "bots_admitted"};

constexpr std::string_view kServerNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {network::kTransport.name,
                                       kReplicationPlan.name,
                                       game_content::kGameContent.name,
                                       world_runtime::kPlayerPresence.name};
constexpr std::string_view kBotsProvide[] = {kClientWorlds.name};
constexpr std::string_view kBotsMaybe[] = {
    network::kTransport.name, kReplicationPlan.name, kInputSourcePlan.name, game_content::kGameContent.name};

/// The session bounds both sides use: a datagram fits one path MTU.
network::SessionProfile sessionProfile(std::size_t sessions) {
    return network::SessionProfile{.maximumSessions = sessions,
                                   .maximumPreAdmissionBytes = 4096,
                                   // SPEC-0013's ingress queued per connection
                                   // (D226).
                                   .maximumControlBuffer = std::size_t{256} << 10U,
                                   .maximumFramePayload = 4096,
                                   .maximumDatagramPayload = 1100,
                                   .admissionTimeout = execution::MonotonicDuration::fromSeconds(5)};
}

network::ProviderProfile providerProfile(std::size_t connections) {
    return network::ProviderProfile{.maximumConnections = connections,
                                    .maximumStreamsPerConnection = 8,
                                    .maximumStreamSend = 1024,
                                    .maximumDatagram = 1200,
                                    .maximumQueuedEvents = 8192,
                                    // SPEC-0013's ingress queued per connection (D239).
                                    .maximumQueuedBytes = std::size_t{256} << 10U};
}

/// What peers must agree on: the protocol, the game, its replicated
/// components, and the package revision, which is the CompositionId when
/// the Runtime's content is a Composition (SPEC-0010's package fingerprint)
/// and zero otherwise.
result::Result<network::Compatibility> compatibilityOf(composition::ParticipantContext& context,
                                                       const ReplicationPlan& plan) {
    network::Compatibility compatibility{
        .protocol = network::protocolFingerprint(), .game = plan.game(), .schema = tableFingerprint(plan.table())};
    if (context.has(game_content::kGameContent.name)) {
        RAWFRAME_TRY_ASSIGN(const game_content::GameContent* content, context.capability(game_content::kGameContent));
        if (const auto& kId = content->compositionId()) {
            compatibility.package.bytes = *kId;
        }
    }
    return compatibility;
}

/// Connections a server holds beyond its players, so a hello arriving when it
/// is full can still be answered `capacity` instead of the transport closing
/// on it.
std::size_t admissionRoom(std::size_t players) noexcept {
    return std::max<std::size_t>(4, players / 8);
}

std::unexpected<result::Error> missing(std::string_view why) {
    return result::fail(
        result::ErrorClass::FailedPrecondition, kReplicationDomain, code(ReplicationError::Malformed), why);
}

class ServerParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, std::string endpoint) {
        context_ = &context;
        endpoint_ = std::move(endpoint);
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        if (!context.has(network::kTransport.name) || !context.has(kReplicationPlan.name)) {
            return missing("a replication server needs a transport and a game's replication plan");
        }
        world_runtime::PlayerPresence* presence = nullptr;
        if (context.has(world_runtime::kPlayerPresence.name)) {
            RAWFRAME_TRY_ASSIGN(presence, context.capability(world_runtime::kPlayerPresence));
        }
        RAWFRAME_TRY_ASSIGN(network::Transport * transport, context.capability(network::kTransport));
        RAWFRAME_TRY_ASSIGN(ReplicationPlan * plan, context.capability(kReplicationPlan));
        plan_ = plan;
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kConnections,
                            context.configuration().unsignedInteger("replication.maximum_connections", 64));
        if (kConnections == 0 || kConnections > 4096) {
            return missing("replication.maximum_connections is 1 to 4096");
        }
        const auto kPlayers = static_cast<std::size_t>(kConnections);
        RAWFRAME_TRY_ASSIGN(provider_, transport->provider(providerProfile(kPlayers + admissionRoom(kPlayers))));
        RAWFRAME_TRY_ASSIGN(const network::Compatibility kExpected, compatibilityOf(context, *plan));
        RAWFRAME_TRY_ASSIGN(sessions_,
                            network::Sessions::server(
                                *provider_,
                                context.clock(),
                                network::ServerSettings{.profile = sessionProfile(kPlayers + admissionRoom(kPlayers)),
                                                        .expected = kExpected,
                                                        .admit = &admitWhileActive,
                                                        .admitContext = this,
                                                        .maximumAdmitted = kPlayers,
                                                        .tickRateTicks = simulation_->rate().ticks,
                                                        .tickRateSeconds = simulation_->rate().seconds}));
        // State at most so many times a second, every tick by default; the
        // period is the whole number of ticks that keeps under the rate
        // (D222).
        const world::TickRate kRate = simulation_->rate();
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kStateRate,
                            context.configuration().unsignedInteger("replication.state_rate", 0));
        const std::uint64_t kTicksPerSecond = kRate.ticks / kRate.seconds;
        if (kStateRate > kTicksPerSecond) {
            return missing("replication.state_rate is at most the World's tick rate");
        }
        const std::uint64_t kPeriod = kStateRate == 0 ? 1 : (kTicksPerSecond + kStateRate - 1) / kStateRate;
        // SPEC-0013's steady egress objective by default; the budget is per
        // publish, so it follows the World's rate and the period.
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kEgress,
                            context.configuration().unsignedInteger("replication.egress_bytes_per_second", 65'536));
        const std::uint64_t kPerPublish =
            kEgress > (std::uint64_t{1} << 30U) ? 0 : kEgress * kRate.seconds * kPeriod / kRate.ticks;
        if (kPerPublish < 64) {
            return missing(
                "replication.egress_bytes_per_second allows at least 64 bytes a publish, and at most 1 GiB/s");
        }
        RAWFRAME_TRY_ASSIGN(
            server_,
            ReplicationServer::create(
                *sessions_,
                ServerReplicationSettings{
                    .table = plan->table(),
                    .playerComponents = {plan->playerComponents().begin(), plan->playerComponents().end()},
                    .input = plan->input(),
                    .perception = plan->perceivedInput(),
                    .interest = plan->interest(),
                    .statePeriod = static_cast<std::uint32_t>(kPeriod),
                    .stateBytesPerPublish = static_cast<std::size_t>(kPerPublish),
                    .presence = presence,
                    .predicted = {plan->predictedComponents().begin(), plan->predictedComponents().end()}}));
        return simulation_->addSystems(*server_);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (sessions_ == nullptr) {
            return {};
        }
        RAWFRAME_TRY(sessions_->listen(network::Endpoint{endpoint_}));
        plan_->attach(server_.get());
        emitter_.log(diagnostics::Severity::Info,
                     kListening,
                     "replication is listening",
                     {diagnostics::field("endpoint", std::string_view{endpoint_})});
        return {};
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame& frame) noexcept override {
        if (server_ == nullptr || simulation_->world() == nullptr) {
            return;
        }
        if (simulation_->generation() != generation_) {
            generation_ = simulation_->generation();
            server_->forgetWorld();
        }
        server_->pump(*simulation_->world(), simulation_->tick());
        // A detection for operators and the game, never a response (SPEC-0041).
        for (const Divergence& divergence : server_->takeDivergences()) {
            emitter_.log(diagnostics::Severity::Warning,
                         kDivergence,
                         "a client's predicted state at a confirmed tick is not the server's",
                         {diagnostics::field("connection", divergence.connection.value),
                          diagnostics::field("tick", divergence.tick),
                          diagnostics::field("scope", divergence.scope),
                          diagnostics::field("expected", divergence.expected),
                          diagnostics::field("received", divergence.received)});
        }
        admitting_.clear();
        // Host phases run once the Host is active, so admission closed here
        // means it drains: the players hear so once.
        if (!noticed_ && !context_->admitting()) {
            noticed_ = true;
            server_->noticeStopping();
        }
        context_->reportConnections(server_->connections());
        // Its memory walks every connection's mappings: twice a second at
        // 120 iterations (D216).
        if (frame.iteration % 60 == 0) {
            context_->reportMemory(server_->heldBytes());
        }
    }

    void stop() noexcept override {
        if (server_ == nullptr) {
            return;
        }
        if (simulation_->world() != nullptr) {
            server_->leaveAll(*simulation_->world());
        }
        plan_->attach(nullptr);
        const ServerReplicationStatistics kStatistics = server_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kServerSummary,
                     "replication server totals",
                     {diagnostics::field("stateDatagrams", kStatistics.stateDatagrams),
                      diagnostics::field("stateBytes", kStatistics.stateBytes),
                      diagnostics::field("recordsSent", kStatistics.recordsSent),
                      diagnostics::field("recordsHeld", kStatistics.recordsHeld),
                      diagnostics::field("recordsDeferred", kStatistics.recordsDeferred),
                      diagnostics::field("interestLeft", kStatistics.interestLeft),
                      diagnostics::field("inputsConsumed", kStatistics.inputsConsumed),
                      diagnostics::field("inputsHeld", kStatistics.inputsHeld),
                      diagnostics::field("inputsNeutral", kStatistics.inputsNeutral),
                      diagnostics::field("inputsStale", kStatistics.inputsStale),
                      diagnostics::field("inputsRefused", kStatistics.inputsRefused),
                      diagnostics::field("perceptionsClamped", kStatistics.perceptionsClamped),
                      diagnostics::field("checksumsVerified", kStatistics.checksumsVerified),
                      diagnostics::field("checksumsUnverifiable", kStatistics.checksumsUnverifiable),
                      diagnostics::field("checksumsDiverged", kStatistics.checksumsDiverged),
                      diagnostics::field("checksumsLimited", kStatistics.checksumsLimited),
                      diagnostics::field("strikes", kStatistics.strikes),
                      diagnostics::field("inputsLimited", kStatistics.inputsLimited),
                      diagnostics::field("struckOut", sessions_->struckOut()),
                      diagnostics::field("admissionsRefused", refused_)});
    }

private:
    /// Gameplay admission is open only while the Host is active: before,
    /// and once it drains, a hello is refused as unavailable (SPEC-0012).
    /// While it is open, the game's own rule has the last word.
    static std::optional<network::Reject> admitWhileActive(const network::Hello& hello, void* context) noexcept {
        auto* self = static_cast<ServerParticipant*>(context);
        if (!self->context_->admitting()) {
            ++self->refused_;
            return network::Reject{.reason = network::RejectReason::Unavailable,
                                   .message = "the server is not admitting players"};
        }
        // One connection plays as one player at a time, so a player's save
        // is theirs alone.
        const auto kIdentity = world_runtime::playerIdentity(hello.requestedSession);
        if (kIdentity.has_value() && (self->server_->playing(*kIdentity) || self->admitting_.contains(*kIdentity))) {
            ++self->refused_;
            return network::Reject{.reason = network::RejectReason::Unavailable,
                                   .message = "this player is already playing"};
        }
        auto refusal = self->plan_->admit(hello);
        self->refused_ += refusal.has_value() ? 1 : 0;
        if (!refusal.has_value() && kIdentity.has_value()) {
            // Admitted in this pump, and not yet playing until it ends.
            self->admitting_.insert(*kIdentity);
        }
        return refusal;
    }

    composition::ParticipantContext* context_ = nullptr;
    std::uint64_t refused_ = 0;
    std::set<world_runtime::PlayerIdentity> admitting_;
    bool noticed_ = false;
    std::string endpoint_;
    ReplicationPlan* plan_ = nullptr;
    world_runtime::Simulation* simulation_ = nullptr;
    std::uint64_t generation_ = 0;
    diagnostics::Emitter emitter_;
    std::unique_ptr<network::Provider> provider_;
    std::unique_ptr<network::Sessions> sessions_;
    std::unique_ptr<ReplicationServer> server_;
};

result::Result<composition::ParticipantOwner> makeServer(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<ServerParticipant>();
    if (const auto kEndpoint = context.configuration().text("replication.endpoint")) {
        RAWFRAME_TRY(participant->load(context, std::string{*kEndpoint}));
    }
    return composition::ParticipantOwner{participant.release()};
}

/// A client's effect events until presentation takes them: at most
/// kEffectsWaiting, the oldest let go past them, so a client nothing
/// presents holds a bounded few.
class EffectQueue final : public EffectSink {
public:
    static constexpr std::size_t kEffectsWaiting = 256;

    void deliver(const PredictedEffect& effect) noexcept override {
        push({.effect = effect});
    }

    void cancel(const PredictedEffect& effect) noexcept override {
        push({.effect = effect, .cancelled = true});
    }

    void take(std::vector<EffectEvent>& into) noexcept {
        into.clear();
        for (std::size_t index = 0; index < size_; ++index) {
            into.push_back(events_[(first_ + index) % kEffectsWaiting]);
        }
        first_ = 0;
        size_ = 0;
    }

private:
    void push(const EffectEvent& event) noexcept {
        events_[(first_ + size_) % kEffectsWaiting] = event;
        if (size_ < kEffectsWaiting) {
            ++size_;
        } else {
            first_ = (first_ + 1) % kEffectsWaiting;
        }
    }

    std::array<EffectEvent, kEffectsWaiting> events_{};
    std::size_t first_ = 0;
    std::size_t size_ = 0;
};

/// One headless client.
struct Bot {
    std::unique_ptr<network::Provider> provider;
    std::unique_ptr<network::Sessions> sessions;
    std::unique_ptr<world::World> world;
    /// Declared before the client that points at them, so they outlive it.
    std::unique_ptr<Predictor> predictor;
    std::unique_ptr<EffectQueue> effects;
    std::unique_ptr<ReplicationClient> client;
    world::Pcg32 random;
    /// The game's input mapping with a hand on its controls, when the game
    /// declares one; random input otherwise.
    std::unique_ptr<InputSource> source;
    std::vector<std::byte> input;
    bool connecting = false;
    /// A client ticks at the server's rate: input goes out once per tick due
    /// since admission, not once per Host iteration.
    std::optional<execution::MonotonicInstant> admittedAt;
    std::uint64_t submitted = 0;
    /// Rollback alarms already reported.
    std::uint64_t alarms = 0;
};

class BotsParticipant final : public composition::Participant, public ClientWorlds {
public:
    composition::CapabilityObject provide(std::string_view capability) noexcept override {
        if (capability == kClientWorlds.name) {
            return composition::provideAs<ClientWorlds>(*this);
        }
        return {};
    }

    [[nodiscard]] std::size_t clientCount() const noexcept override {
        return bots_.size();
    }

    [[nodiscard]] ClientView client(std::size_t index) const noexcept override {
        if (index >= bots_.size()) {
            return {};
        }
        return ClientView{.world = bots_[index].world.get(), .owned = bots_[index].client->owned()};
    }

    void takeEffects(std::size_t index, std::vector<EffectEvent>& into) noexcept override {
        if (index >= bots_.size() || bots_[index].effects == nullptr) {
            into.clear();
            return;
        }
        bots_[index].effects->take(into);
    }

    result::Status load(composition::ParticipantContext& context, std::uint64_t count) {
        if (!context.has(network::kTransport.name) || !context.has(kReplicationPlan.name)) {
            return missing("bots need a transport and a game's replication plan");
        }
        RAWFRAME_TRY_ASSIGN(network::Transport * transport, context.capability(network::kTransport));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kReplicationPlan));
        RAWFRAME_TRY_ASSIGN(compatibility_, compatibilityOf(context, *plan_));
        const composition::Configuration& configuration = context.configuration();
        endpoint_ = std::string{configuration.text("bots.endpoint").value_or("arena")};
        // What every bot offers as its join ticket, byte for byte; none by
        // default.
        for (const char kCharacter : configuration.text("bots.ticket").value_or("")) {
            ticket_.push_back(static_cast<std::byte>(kCharacter));
        }
        // Bot n asks for the session `<bots.session>-n`, so it plays as the
        // same player in every run; none by default.
        sessionPrefix_ = std::string{configuration.text("bots.session").value_or("")};
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kSeed, configuration.unsignedInteger("bots.seed", 0));
        schema::RegistryBuilder builder;
        for (const schema::ComponentDescriptor& descriptor : plan_->components()) {
            builder.add(descriptor);
        }
        RAWFRAME_TRY_ASSIGN(registry_, builder.freeze());
        const auto kPredict = configuration.text("bots.predict");
        if (kPredict.has_value() && *kPredict != "true" && *kPredict != "false") {
            return missing("bots.predict is true or false");
        }
        // A game that predicts is played predicting unless told otherwise.
        const bool kPredicting = !plan_->predictedComponents().empty() && kPredict != "false";
        // A checksum record every second at 60 Hz by default (D204); the
        // divergence drill exists in development builds only.
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kChecksumInterval,
                            configuration.unsignedInteger("bots.checksum_interval", 60));
        if (kChecksumInterval > 1'000'000) {
            return missing("bots.checksum_interval is at most a million confirmed ticks");
        }
        checksumInterval_ = static_cast<std::uint32_t>(kChecksumInterval);
        const auto kDrill = configuration.text("bots.divergence_drill");
        if (kDrill.has_value() && (RAWFRAME_SHIPPING || (*kDrill != "true" && *kDrill != "false"))) {
            return missing("bots.divergence_drill is true or false, and only in a development build");
        }
        divergenceDrill_ = kDrill == "true";
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kAlarmLimit, configuration.unsignedInteger("bots.rollback_alarm", 30));
        if (kAlarmLimit > 1'000'000) {
            return missing("bots.rollback_alarm is at most a million rollbacks a second");
        }
        rollbackAlarm_ = static_cast<std::uint32_t>(kAlarmLimit);
        const auto kInterpolate = configuration.text("bots.interpolate");
        if (kInterpolate.has_value() && *kInterpolate != "true" && *kInterpolate != "false") {
            return missing("bots.interpolate is true or false");
        }
        std::optional<InterpolationSettings> interpolation;
        if (!plan_->interpolatedComponents().empty() && kInterpolate != "false") {
            interpolation = InterpolationSettings{
                .interpolated = {plan_->interpolatedComponents().begin(), plan_->interpolatedComponents().end()},
                .clock = &context.clock()};
        }
        InputSourcePlan* sources = nullptr;
        if (context.has(kInputSourcePlan.name)) {
            RAWFRAME_TRY_ASSIGN(sources, context.capability(kInputSourcePlan));
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            Bot bot{.random =
                        world::deriveStream(world::RootSeed{kSeed + index}, "rawframe.replication.bots", "steer")};
            RAWFRAME_TRY_ASSIGN(bot.provider, transport->provider(providerProfile(1)));
            RAWFRAME_TRY_ASSIGN(
                bot.sessions,
                network::Sessions::client(*bot.provider, context.clock(), {.profile = sessionProfile(1)}));
            bot.world = std::make_unique<world::World>(registry_);
            std::optional<PredictionSettings> prediction;
            if (kPredicting) {
                auto predictor = plan_->predictor();
                if (!predictor.has_value() && predictor.error().errorClass() != result::ErrorClass::ResourceExhausted) {
                    return std::unexpected<result::Error>{std::move(predictor).error()};
                }
                // A process holds only so many physics worlds; a bot past
                // them plays as a client that does not predict would.
                if (predictor.has_value()) {
                    bot.predictor = std::move(*predictor);
                    bot.effects = std::make_unique<EffectQueue>();
                    prediction = PredictionSettings{
                        .predictor = bot.predictor.get(),
                        .predicted = {plan_->predictedComponents().begin(), plan_->predictedComponents().end()},
                        .neighborhood = {plan_->nearbyComponents().begin(), plan_->nearbyComponents().end()},
                        .checksumInterval = checksumInterval_,
                        .rollbackAlarm = rollbackAlarm_,
                        .effects = bot.effects.get(),
                        .effectClasses = {plan_->effectClasses().begin(), plan_->effectClasses().end()},
                        .divergenceDrill = divergenceDrill_};
                } else {
                    ++unpredicted_;
                }
            }
            if (sources != nullptr) {
                auto source = sources->botSource(kSeed + index);
                if (source.has_value()) {
                    bot.source = std::move(*source);
                } else if (source.error().errorClass() != result::ErrorClass::NotFound) {
                    return std::unexpected<result::Error>{std::move(source).error()};
                }
            }
            RAWFRAME_TRY_ASSIGN(
                bot.client,
                ReplicationClient::create(*bot.sessions,
                                          *bot.world,
                                          ClientReplicationSettings{.table = plan_->table(),
                                                                    .input = plan_->input(),
                                                                    .perception = plan_->perceivedInput(),
                                                                    .prediction = prediction,
                                                                    .interpolation = interpolation}));
            if (plan_->input()) {
                bot.input.assign(plan_->input()->size, std::byte{0});
            }
            bots_.push_back(std::move(bot));
        }
        return {};
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        return {};
    }

    void runHostPhase(composition::HostPhase phase, const composition::HostFrame& frame) noexcept override {
        std::size_t admitted = 0;
        for (Bot& bot : bots_) {
            if (phase == composition::HostPhase::Ingress) {
                // The server may start listening after the bots start, so a
                // bot keeps trying until its connection is under way.
                if (!bot.connecting) {
                    bot.connecting = bot.client
                                         ->connect(network::Endpoint{endpoint_},
                                                   network::Hello{.compatibility = compatibility_,
                                                                  .requestedSession = sessionOf(bot),
                                                                  .ticket = ticket_,
                                                                  .maximumDatagram = 1100,
                                                                  .maximumFrame = 4096})
                                         .has_value();
                } else {
                    bot.client->pump();
                    // Pathology, not rollback itself: a local diagnostic,
                    // once for each second past the alarm (SPEC-0041).
                    const std::uint64_t kAlarms = bot.client->predictionStatistics().rollbackAlarms;
                    if (kAlarms > bot.alarms) {
                        bot.alarms = kAlarms;
                        emitter_.log(diagnostics::Severity::Warning,
                                     kRollbackAlarm,
                                     "a bot rolled back more in a second than the alarm allows",
                                     {diagnostics::field("bot", static_cast<std::uint64_t>(&bot - bots_.data())),
                                      diagnostics::field("alarms", kAlarms),
                                      diagnostics::field("limit", std::uint64_t{rollbackAlarm_})});
                    }
                }
                admitted += bot.client->admitted() ? 1 : 0;
            } else if (phase == composition::HostPhase::Egress && bot.client->admitted() && !bot.input.empty()) {
                const network::Accept& accept = *bot.client->accept();
                if (!bot.admittedAt) {
                    bot.admittedAt = frame.now;
                }
                const auto kElapsed = static_cast<std::uint64_t>((frame.now - *bot.admittedAt).nanoseconds);
                const std::uint64_t kDue =
                    1 + (kElapsed / 1'000'000U * accept.tickRateTicks / (1'000U * accept.tickRateSeconds));
                // A bot that fell far behind catches up a few ticks at a time.
                for (int burst = 0; burst < 4 && bot.submitted < kDue; ++burst, ++bot.submitted) {
                    steer(bot, bot.submitted);
                    static_cast<void>(bot.client->submitInput(bot.input));
                }
            }
        }
        // Once, when every bot is in: what a script driving a server waits
        // for before it goes on.
        if (phase == composition::HostPhase::Ingress && !allAdmitted_ && !bots_.empty() && admitted == bots_.size()) {
            allAdmitted_ = true;
            emitter_.log(diagnostics::Severity::Info,
                         kBotsAdmitted,
                         "every bot is admitted",
                         {diagnostics::field("bots", bots_.size())});
        }
    }

    void stop() noexcept override {
        if (bots_.empty()) {
            return;
        }
        std::uint64_t admitted = 0;
        std::uint64_t unavailable = 0;
        std::uint64_t noticed = 0;
        std::uint64_t full = 0;
        std::uint64_t ticketInvalid = 0;
        std::uint64_t mirrored = 0;
        std::uint64_t stateDatagrams = 0;
        PredictionStatistics predicted;
        InterpolationStatistics interpolated;
        std::uint64_t handed = 0;
        for (Bot& bot : bots_) {
            handed += bot.source != nullptr ? 1 : 0;
            interpolated.blended += bot.client->interpolationStatistics().blended;
            interpolated.newest += bot.client->interpolationStatistics().newest;
            admitted += bot.client->admitted() ? 1 : 0;
            unavailable += bot.client->rejection() == network::RejectReason::Unavailable ? 1 : 0;
            noticed += bot.client->serverStopping() ? 1 : 0;
            full += bot.client->rejection() == network::RejectReason::Capacity ? 1 : 0;
            ticketInvalid += bot.client->rejection() == network::RejectReason::TicketInvalid ? 1 : 0;
            mirrored += bot.world->entityCount();
            stateDatagrams += bot.client->statistics().stateDatagrams;
            const PredictionStatistics kBot = bot.client->predictionStatistics();
            predicted.predictedTicks += kBot.predictedTicks;
            predicted.confirmed += kBot.confirmed;
            predicted.rollbacks += kBot.rollbacks;
            predicted.resimulatedTicks += kBot.resimulatedTicks;
            predicted.stalled += kBot.stalled;
            predicted.failedSteps += kBot.failedSteps;
            predicted.checksumsSent += kBot.checksumsSent;
            predicted.rollbackAlarms += kBot.rollbackAlarms;
            predicted.effectsDelivered += kBot.effectsDelivered;
            predicted.effectsSuppressed += kBot.effectsSuppressed;
            predicted.effectsCancelled += kBot.effectsCancelled;
            predicted.effectsDropped += kBot.effectsDropped;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kBotsSummary,
                     "bots totals",
                     {diagnostics::field("bots", bots_.size()),
                      diagnostics::field("admitted", admitted),
                      diagnostics::field("unavailable", unavailable),
                      diagnostics::field("serverStopping", noticed),
                      diagnostics::field("full", full),
                      diagnostics::field("ticketInvalid", ticketInvalid),
                      diagnostics::field("unpredicted", unpredicted_),
                      diagnostics::field("handed", handed),
                      diagnostics::field("sourceFailures", sourceFailures_),
                      diagnostics::field("mirroredEntities", mirrored),
                      diagnostics::field("stateDatagrams", stateDatagrams),
                      diagnostics::field("predictedTicks", predicted.predictedTicks),
                      diagnostics::field("confirmed", predicted.confirmed),
                      diagnostics::field("rollbacks", predicted.rollbacks),
                      diagnostics::field("resimulatedTicks", predicted.resimulatedTicks),
                      diagnostics::field("stalled", predicted.stalled),
                      diagnostics::field("failedSteps", predicted.failedSteps),
                      diagnostics::field("checksumsSent", predicted.checksumsSent),
                      diagnostics::field("rollbackAlarms", predicted.rollbackAlarms),
                      diagnostics::field("effectsDelivered", predicted.effectsDelivered),
                      diagnostics::field("effectsSuppressed", predicted.effectsSuppressed),
                      diagnostics::field("effectsCancelled", predicted.effectsCancelled),
                      diagnostics::field("effectsDropped", predicted.effectsDropped),
                      diagnostics::field("blended", interpolated.blended),
                      diagnostics::field("shownNewest", interpolated.newest)});
    }

private:
    /// The session `bot` asks for, from its place among the bots.
    std::vector<std::byte> sessionOf(const Bot& bot) const {
        std::vector<std::byte> session;
        if (sessionPrefix_.empty()) {
            return session;
        }
        const std::string kText = sessionPrefix_ + "-" + std::to_string(&bot - bots_.data());
        for (const char kCharacter : kText) {
            session.push_back(static_cast<std::byte>(kCharacter));
        }
        return session;
    }

    /// A new random heading for every float of the input, every sixty ticks.
    void steer(Bot& bot, std::uint64_t tick) {
        if (bot.source != nullptr) {
            // A source that fails leaves the last input in place.
            sourceFailures_ += bot.source->next(tick, bot.input).has_value() ? 0 : 1;
            return;
        }
        if (tick % 60 != 0) {
            return;
        }
        for (const WireField& field : plan_->input()->fields) {
            if (field.kind == WireKind::F32) {
                const float kValue = (bot.random.nextFloat() * 2.0F) - 1.0F;
                std::memcpy(bot.input.data() + field.offset, &kValue, sizeof kValue);
            } else if (field.kind == WireKind::F64) {
                const double kValue = (bot.random.nextDouble() * 2.0) - 1.0;
                std::memcpy(bot.input.data() + field.offset, &kValue, sizeof kValue);
            }
        }
    }

    const ReplicationPlan* plan_ = nullptr;
    network::Compatibility compatibility_;
    std::string endpoint_;
    std::vector<std::byte> ticket_;
    std::string sessionPrefix_;
    std::shared_ptr<const schema::SchemaRegistry> registry_;
    std::vector<Bot> bots_;
    std::uint32_t checksumInterval_ = 60;
    std::uint32_t rollbackAlarm_ = 30;
    bool divergenceDrill_ = false;
    /// Bots that would predict but could not have a predictor.
    std::uint64_t unpredicted_ = 0;
    bool allAdmitted_ = false;
    std::uint64_t sourceFailures_ = 0;
    diagnostics::Emitter emitter_;
};

result::Result<composition::ParticipantOwner> makeBots(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<BotsParticipant>();
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kCount, context.configuration().unsignedInteger("bots.count", 0));
    if (kCount > 4096) {
        return missing("bots.count is at most 4096");
    }
    if (kCount != 0) {
        RAWFRAME_TRY(participant->load(context, kCount));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace

network::Fingerprint tableFingerprint(const ReplicationTable& table) noexcept {
    base::Sha256 hasher;
    const auto kNumber = [&hasher](std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t index = 0; index < 8; ++index) {
            bytes[index] = static_cast<std::byte>((value >> (8U * (7U - index))) & 0xFFU);
        }
        hasher.update(bytes);
    };
    hasher.update("rawframe.replication.table.v1");
    kNumber(table.components.size());
    for (const ComponentCodec& codec : table.components) {
        kNumber(codec.component.value.high);
        kNumber(codec.component.value.low);
        kNumber(codec.size);
        kNumber(codec.fields.size());
        for (const WireField& field : codec.fields) {
            kNumber(field.offset);
            kNumber(static_cast<std::uint64_t>(field.kind));
        }
    }
    network::Fingerprint fingerprint;
    fingerprint.bytes = hasher.finish();
    return fingerprint;
}

void registerParticipants(composition::ParticipantRegistrar& registrar) noexcept {
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.replication.server",
        .factory = &makeServer,
        .scope = composition::LifetimeScope::World,
        .requiredCapabilities = kServerNeeds,
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "replication.server",
        .budgetOwner = "network",
        .hostPhases = composition::hostPhaseBit(composition::HostPhase::Ingress),
    });
    registrar.submit(composition::ParticipantDeclaration{
        .identity = "rawframe.replication.bots",
        .factory = &makeBots,
        .scope = composition::LifetimeScope::World,
        .providedCapabilities = kBotsProvide,
        .optionalCapabilities = kBotsMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "replication.bots",
        .budgetOwner = "network",
        .hostPhases = static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::Ingress) |
                                                 composition::hostPhaseBit(composition::HostPhase::Egress)),
    });
}

} // namespace rawframe::world_replication
