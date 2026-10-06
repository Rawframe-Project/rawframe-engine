#include "participant_shared.h"
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
constexpr EventIdentity kDivergenceCapture{"replication", "divergence_capture"};

constexpr std::string_view kServerNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {network::kTransport.name,
                                       kReplicationPlan.name,
                                       game_content::kGameContent.name,
                                       world_runtime::kPlayerPresence.name};
constexpr std::string_view kBotsProvide[] = {kClientWorlds.name};
constexpr std::string_view kBotsMaybe[] = {
    network::kTransport.name, kReplicationPlan.name, kInputSourcePlan.name, game_content::kGameContent.name};

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
                                                        .tickRateSeconds = simulation_->rate().seconds,
                                                        .lanes = lanesOf(*plan)}));
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
        RAWFRAME_TRY_ASSIGN(const bool kCapture,
                            captureAsked(context.configuration(), "replication.divergence_capture"));
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
                    .predicted = {plan->predictedComponents().begin(), plan->predictedComponents().end()},
                    .captureDivergences = kCapture,
                    .commandSizes = {plan->commandSizes().begin(), plan->commandSizes().end()}}));
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
        // What the ticks since the last pump sent, now they have committed.
        posted_.clear();
        plan_->takeMessages(posted_);
        for (const PostedMessage& message : posted_) {
            server_->post(message);
        }
        // What the players asked for, for the game's next tick (D425).
        commands_.clear();
        server_->takeCommands(commands_);
        if (!commands_.empty()) {
            plan_->deliverCommands(commands_);
        }
        // Sessions the game ended, after what it sent them (ADR-0073).
        ended_.clear();
        plan_->takeTerminations(ended_);
        for (const PostedTermination& ended : ended_) {
            static_cast<void>(server_->terminate(
                *simulation_->world(), ended.player, {.reason = network::TerminationReason::Game, .note = ended.note}));
        }
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
            // The server's side of a development capture, a value a record.
            for (std::size_t index = 0; index < divergence.committed.size(); ++index) {
                const std::string kValue = hexOf(divergence.committed[index]);
                emitter_.log(diagnostics::Severity::Info,
                             kDivergenceCapture,
                             "what the server committed at a diverged tick",
                             {diagnostics::field("connection", divergence.connection.value),
                              diagnostics::field("tick", divergence.tick),
                              diagnostics::field("received", divergence.received),
                              diagnostics::field("component", index),
                              diagnostics::field("value", std::string_view{kValue})});
            }
        }
        admitting_.clear();
        // Host phases run once the Host is active, so admission closed here
        // means it drains: the players hear so once.
        if (!noticed_ && !context_->admitting()) {
            noticed_ = true;
            server_->noticeStopping();
        }
        context_->reportConnections(server_->connections());
        mostConnections_ = std::max(mostConnections_, server_->connections());
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
                      diagnostics::field("admissionsRefused", refused_),
                      diagnostics::field("mostConnections", static_cast<std::uint64_t>(mostConnections_)),
                      diagnostics::field("messagesSent", kStatistics.messagesSent),
                      diagnostics::field("messagesUndelivered", kStatistics.messagesUndelivered),
                      diagnostics::field("terminated", kStatistics.terminated),
                      diagnostics::field("commandsTaken", kStatistics.commandsTaken),
                      diagnostics::field("commandsLimited", kStatistics.commandsLimited)});
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
    /// The most players connected at once.
    std::size_t mostConnections_ = 0;
    std::vector<PostedMessage> posted_;
    std::vector<ReceivedCommand> commands_;
    std::vector<PostedTermination> ended_;
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

} // namespace

/// Bytes as lower-case hexadecimal, for a capture's log.
std::string hexOf(std::span<const std::byte> bytes) {
    std::string made;
    made.reserve(bytes.size() * 2);
    for (const std::byte kByte : bytes) {
        made += "0123456789abcdef"[std::to_integer<unsigned>(kByte) >> 4U];
        made += "0123456789abcdef"[std::to_integer<unsigned>(kByte) & 0xFU];
    }
    return made;
}

/// SPEC-0041's two-sided capture is a development capability (D275): a
/// shipping build refuses the key that asks for it.
result::Result<bool> captureAsked(const composition::Configuration& configuration, std::string_view key) {
    const auto kAsked = configuration.text(key);
    if (kAsked.has_value() && (RAWFRAME_SHIPPING || (*kAsked != "true" && *kAsked != "false"))) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::FailedPrecondition,
                         kReplicationDomain,
                         code(ReplicationError::Malformed),
                         "a divergence capture is true or false, and only in a development build")
                .error()
                .withContext("key", std::string{key})};
    }
    return kAsked == "true";
}

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

/// The event lanes both sides declare: the engine's (D267), the game's when
/// it sends messages (D266), its bound checked when the game loaded, and
/// its command lane when it has commands, bound by the largest (D425).
std::vector<network::EventLaneDeclaration> lanesOf(const ReplicationPlan& plan) {
    std::vector<network::EventLaneDeclaration> lanes = {engineLane()};
    if (plan.messageRecord() != 0) {
        lanes.push_back(gameMessageLane(plan.messageRecord()));
    }
    if (const std::span<const std::size_t> kSizes = plan.commandSizes(); !kSizes.empty()) {
        lanes.push_back(gameCommandLane(*std::ranges::max_element(kSizes)));
    }
    return lanes;
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
