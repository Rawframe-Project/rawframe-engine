#include "rawframe/base/sha256.h"
#include "rawframe/composition/composition.h"
#include "rawframe/network/transport.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/random.h"
#include "rawframe/world_replication/client.h"
#include "rawframe/world_replication/errors.h"
#include "rawframe/world_replication/plan.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_replication/server.h"
#include "rawframe/world_runtime/simulation.h"

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace rawframe::world_replication {

namespace {

using diagnostics::EventIdentity;

constexpr EventIdentity kListening{"replication", "listening"};
constexpr EventIdentity kServerSummary{"replication", "server_summary"};
constexpr EventIdentity kBotsSummary{"replication", "bots_summary"};

constexpr std::string_view kServerNeeds[] = {world_runtime::kSimulation.name};
constexpr std::string_view kMaybe[] = {network::kTransport.name, kReplicationPlan.name};

/// The session bounds both sides use: a datagram fits one path MTU.
network::SessionProfile sessionProfile(std::size_t sessions) {
    return network::SessionProfile{.maximumSessions = sessions,
                                   .maximumPreAdmissionBytes = 4096,
                                   .maximumControlBuffer = std::size_t{1} << 20U,
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
                                    .maximumQueuedBytes = std::size_t{8} << 20U};
}

network::Compatibility compatibilityOf(const ReplicationPlan& plan) {
    return network::Compatibility{
        .protocol = network::protocolFingerprint(), .game = plan.game(), .schema = tableFingerprint(plan.table())};
}

std::unexpected<result::Error> missing(std::string_view why) {
    return result::fail(
        result::ErrorClass::FailedPrecondition, kReplicationDomain, code(ReplicationError::Malformed), why);
}

class ServerParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, std::string endpoint) {
        endpoint_ = std::move(endpoint);
        RAWFRAME_TRY_ASSIGN(simulation_, context.capability(world_runtime::kSimulation));
        if (!context.has(network::kTransport.name) || !context.has(kReplicationPlan.name)) {
            return missing("a replication server needs a transport and a game's replication plan");
        }
        RAWFRAME_TRY_ASSIGN(network::Transport * transport, context.capability(network::kTransport));
        RAWFRAME_TRY_ASSIGN(const ReplicationPlan* plan, context.capability(kReplicationPlan));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kConnections,
                            context.configuration().unsignedInteger("replication.maximum_connections", 64));
        if (kConnections == 0 || kConnections > 4096) {
            return missing("replication.maximum_connections is 1 to 4096");
        }
        RAWFRAME_TRY_ASSIGN(provider_, transport->provider(providerProfile(static_cast<std::size_t>(kConnections))));
        RAWFRAME_TRY_ASSIGN(
            sessions_,
            network::Sessions::server(
                *provider_,
                context.clock(),
                network::ServerSettings{.profile = sessionProfile(static_cast<std::size_t>(kConnections)),
                                        .expected = compatibilityOf(*plan),
                                        .tickRateTicks = simulation_->rate().ticks,
                                        .tickRateSeconds = simulation_->rate().seconds}));
        // SPEC-0013's steady egress objective by default; the budget is per
        // tick, so it follows the World's rate.
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kEgress,
                            context.configuration().unsignedInteger("replication.egress_bytes_per_second", 65'536));
        const world::TickRate kRate = simulation_->rate();
        const std::uint64_t kPerTick = kEgress > (std::uint64_t{1} << 30U) ? 0 : kEgress * kRate.seconds / kRate.ticks;
        if (kPerTick < 64) {
            return missing("replication.egress_bytes_per_second allows at least 64 bytes a tick, and at most 1 GiB/s");
        }
        RAWFRAME_TRY_ASSIGN(server_,
                            ReplicationServer::create(
                                *sessions_,
                                ServerReplicationSettings{.table = plan->table(),
                                                          .playerComponents = {plan->playerComponents().begin(),
                                                                               plan->playerComponents().end()},
                                                          .input = plan->input(),
                                                          .stateBytesPerTick = static_cast<std::size_t>(kPerTick)}));
        return simulation_->addSystems(*server_);
    }

    result::Status start(composition::ParticipantContext& context) noexcept override {
        emitter_ = context.emitter();
        if (sessions_ == nullptr) {
            return {};
        }
        RAWFRAME_TRY(sessions_->listen(network::Endpoint{endpoint_}));
        emitter_.log(diagnostics::Severity::Info,
                     kListening,
                     "replication is listening",
                     {diagnostics::field("endpoint", std::string_view{endpoint_})});
        return {};
    }

    void runHostPhase(composition::HostPhase, const composition::HostFrame&) noexcept override {
        if (server_ == nullptr || simulation_->world() == nullptr) {
            return;
        }
        if (simulation_->generation() != generation_) {
            generation_ = simulation_->generation();
            server_->forgetWorld();
        }
        server_->pump(*simulation_->world(), simulation_->tick());
    }

    void stop() noexcept override {
        if (server_ == nullptr) {
            return;
        }
        const ServerReplicationStatistics kStatistics = server_->statistics();
        emitter_.log(diagnostics::Severity::Info,
                     kServerSummary,
                     "replication server totals",
                     {diagnostics::field("stateDatagrams", kStatistics.stateDatagrams),
                      diagnostics::field("stateBytes", kStatistics.stateBytes),
                      diagnostics::field("recordsSent", kStatistics.recordsSent),
                      diagnostics::field("recordsHeld", kStatistics.recordsHeld),
                      diagnostics::field("recordsDeferred", kStatistics.recordsDeferred),
                      diagnostics::field("inputsConsumed", kStatistics.inputsConsumed),
                      diagnostics::field("inputsHeld", kStatistics.inputsHeld),
                      diagnostics::field("inputsNeutral", kStatistics.inputsNeutral),
                      diagnostics::field("inputsRefused", kStatistics.inputsRefused)});
    }

private:
    std::string endpoint_;
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

/// One headless client.
struct Bot {
    std::unique_ptr<network::Provider> provider;
    std::unique_ptr<network::Sessions> sessions;
    std::unique_ptr<world::World> world;
    /// Declared before the client that points at it, so it outlives it.
    std::unique_ptr<Predictor> predictor;
    std::unique_ptr<ReplicationClient> client;
    world::Pcg32 random;
    std::vector<std::byte> input;
    bool connecting = false;
    /// A client ticks at the server's rate: input goes out once per tick due
    /// since admission, not once per Host iteration.
    std::optional<execution::MonotonicInstant> admittedAt;
    std::uint64_t submitted = 0;
};

class BotsParticipant final : public composition::Participant {
public:
    result::Status load(composition::ParticipantContext& context, std::uint64_t count) {
        if (!context.has(network::kTransport.name) || !context.has(kReplicationPlan.name)) {
            return missing("bots need a transport and a game's replication plan");
        }
        RAWFRAME_TRY_ASSIGN(network::Transport * transport, context.capability(network::kTransport));
        RAWFRAME_TRY_ASSIGN(plan_, context.capability(kReplicationPlan));
        const composition::Configuration& configuration = context.configuration();
        endpoint_ = std::string{configuration.text("bots.endpoint").value_or("arena")};
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
                RAWFRAME_TRY_ASSIGN(bot.predictor, plan_->predictor());
                prediction = PredictionSettings{
                    .predictor = bot.predictor.get(),
                    .predicted = {plan_->predictedComponents().begin(), plan_->predictedComponents().end()}};
            }
            RAWFRAME_TRY_ASSIGN(bot.client,
                                ReplicationClient::create(*bot.sessions,
                                                          *bot.world,
                                                          ClientReplicationSettings{.table = plan_->table(),
                                                                                    .input = plan_->input(),
                                                                                    .prediction = prediction}));
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
        for (Bot& bot : bots_) {
            if (phase == composition::HostPhase::Ingress) {
                // The server may start listening after the bots start, so a
                // bot keeps trying until its connection is under way.
                if (!bot.connecting) {
                    bot.connecting = bot.client
                                         ->connect(network::Endpoint{endpoint_},
                                                   network::Hello{.compatibility = compatibilityOf(*plan_),
                                                                  .maximumDatagram = 1100,
                                                                  .maximumFrame = 4096})
                                         .has_value();
                } else {
                    bot.client->pump();
                }
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
    }

    void stop() noexcept override {
        if (bots_.empty()) {
            return;
        }
        std::uint64_t admitted = 0;
        std::uint64_t mirrored = 0;
        std::uint64_t stateDatagrams = 0;
        PredictionStatistics predicted;
        for (Bot& bot : bots_) {
            admitted += bot.client->admitted() ? 1 : 0;
            mirrored += bot.world->entityCount();
            stateDatagrams += bot.client->statistics().stateDatagrams;
            const PredictionStatistics kBot = bot.client->predictionStatistics();
            predicted.predictedTicks += kBot.predictedTicks;
            predicted.confirmed += kBot.confirmed;
            predicted.rollbacks += kBot.rollbacks;
            predicted.resimulatedTicks += kBot.resimulatedTicks;
            predicted.stalled += kBot.stalled;
            predicted.failedSteps += kBot.failedSteps;
        }
        emitter_.log(diagnostics::Severity::Info,
                     kBotsSummary,
                     "bots totals",
                     {diagnostics::field("bots", bots_.size()),
                      diagnostics::field("admitted", admitted),
                      diagnostics::field("mirroredEntities", mirrored),
                      diagnostics::field("stateDatagrams", stateDatagrams),
                      diagnostics::field("predictedTicks", predicted.predictedTicks),
                      diagnostics::field("confirmed", predicted.confirmed),
                      diagnostics::field("rollbacks", predicted.rollbacks),
                      diagnostics::field("resimulatedTicks", predicted.resimulatedTicks),
                      diagnostics::field("stalled", predicted.stalled),
                      diagnostics::field("failedSteps", predicted.failedSteps)});
    }

private:
    /// A new random heading for every float of the input, every sixty ticks.
    void steer(Bot& bot, std::uint64_t tick) {
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
    std::string endpoint_;
    std::shared_ptr<const schema::SchemaRegistry> registry_;
    std::vector<Bot> bots_;
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
        .optionalCapabilities = kMaybe,
        .lifecycle = {.stopBudget = execution::MonotonicDuration::fromMilliseconds(100)},
        .observabilityIdentity = "replication.bots",
        .budgetOwner = "network",
        .hostPhases = static_cast<std::uint16_t>(composition::hostPhaseBit(composition::HostPhase::Ingress) |
                                                 composition::hostPhaseBit(composition::HostPhase::Egress)),
    });
}

} // namespace rawframe::world_replication
