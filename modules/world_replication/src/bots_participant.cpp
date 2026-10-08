// The bots participant (D202, D362, D363): headless clients that each
// connect a World of their own to a server, and the process's own local
// players, played from its devices, shown and presented.

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
#include "rawframe/world_replication/records.h"
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

constexpr EventIdentity kRollbackAlarm{"replication", "rollback_rate_alarm"};
constexpr EventIdentity kBotsSummary{"replication", "bots_summary"};
constexpr EventIdentity kBotsAdmitted{"replication", "bots_admitted"};
constexpr EventIdentity kChecksumCapture{"replication", "checksum_capture"};

/// A client's game messages for presentation to read, as EffectQueue keeps
/// effects: the latest kMessagesKept, each read once by each reader's count.
class MessageQueue final : public MessageSink {
public:
    static constexpr std::size_t kMessagesKept = 1024;

    void deliver(ReceivedMessage message) noexcept override {
        messages_[count_ % kMessagesKept] = std::move(message);
        ++count_;
    }

    std::uint64_t read(std::uint64_t seen, std::vector<ReceivedMessage>& into) const noexcept {
        into.clear();
        const std::uint64_t kOldest = count_ > kMessagesKept ? count_ - kMessagesKept : 0;
        for (std::uint64_t number = std::max(seen, kOldest); number < count_; ++number) {
            into.push_back(messages_[number % kMessagesKept]);
        }
        return count_;
    }

private:
    std::vector<ReceivedMessage> messages_ = std::vector<ReceivedMessage>(kMessagesKept);
    std::uint64_t count_ = 0;
};

/// A client's effect events for presentation to read: the latest
/// kEffectsKept, each numbered in order, so every reader reads each once
/// by its own count, and a client nothing presents holds a bounded few.
class EffectQueue final : public EffectSink {
public:
    static constexpr std::size_t kEffectsKept = 256;

    void deliver(const PredictedEffect& effect) noexcept override {
        push({.effect = effect});
    }

    void cancel(const PredictedEffect& effect) noexcept override {
        push({.effect = effect, .cancelled = true});
    }

    std::uint64_t read(std::uint64_t seen, std::vector<EffectEvent>& into) const noexcept {
        into.clear();
        const std::uint64_t kOldest = count_ > kEffectsKept ? count_ - kEffectsKept : 0;
        for (std::uint64_t number = std::max(seen, kOldest); number < count_; ++number) {
            into.push_back(events_[number % kEffectsKept]);
        }
        return count_;
    }

private:
    void push(const EffectEvent& event) noexcept {
        events_[count_ % kEffectsKept] = event;
        ++count_;
    }

    std::array<EffectEvent, kEffectsKept> events_{};
    std::uint64_t count_ = 0;
};

/// One headless client.
struct Bot {
    std::unique_ptr<network::Provider> provider;
    std::unique_ptr<network::Sessions> sessions;
    std::unique_ptr<world::World> world;
    /// Declared before the client that points at them, so they outlive it.
    std::unique_ptr<Predictor> predictor;
    std::unique_ptr<EffectQueue> effects;
    std::unique_ptr<MessageQueue> messages;
    std::unique_ptr<ReplicationClient> client;
    world::Pcg32 random;
    /// The game's input mapping with a hand on its controls, when the game
    /// declares one; random input otherwise.
    std::unique_ptr<InputSource> source;
    std::vector<std::byte> input;
    bool connecting = false;
    /// Asked again after a refusal as unavailable (D379): how often so far,
    /// and not before when.
    std::uint64_t retries = 0;
    std::optional<execution::MonotonicInstant> retryAt;
    /// The last refusal heard, kept while it asks again; none once admitted.
    std::optional<network::RejectReason> refusal;
    /// A client ticks at the server's rate: input goes out once per tick due
    /// since admission, not once per Host iteration, and never more than one
    /// input window past the newest server tick it has heard of, so it does
    /// not run ahead of a server slowed with it in one process (D522).
    std::optional<execution::MonotonicInstant> admittedAt;
    std::optional<std::uint64_t> firstHeard;
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

    std::uint64_t readEffects(std::size_t index, std::uint64_t seen, std::vector<EffectEvent>& into) noexcept override {
        if (index >= bots_.size() || bots_[index].effects == nullptr) {
            into.clear();
            return seen;
        }
        return bots_[index].effects->read(seen, into);
    }

    std::uint64_t
    readMessages(std::size_t index, std::uint64_t seen, std::vector<ReceivedMessage>& into) noexcept override {
        if (index >= bots_.size() || bots_[index].messages == nullptr) {
            into.clear();
            return seen;
        }
        return bots_[index].messages->read(seen, into);
    }

    [[nodiscard]] std::optional<std::size_t> playerClient() const noexcept override {
        return player_ && !bots_.empty() ? std::optional<std::size_t>{0} : std::nullopt;
    }

    [[nodiscard]] std::size_t localPlayers() const noexcept override {
        return localPlayers_;
    }

    /// The first `count` clients the process's local players (D362).
    void showLocally(std::size_t count) noexcept {
        localPlayers_ = count;
    }

    /// `count` clients; with `player`, the first is the process's own
    /// player, played from the devices its host lends.
    result::Status load(composition::ParticipantContext& context, std::uint64_t count, bool player) {
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
        const std::string_view kTicket = configuration.text("bots.ticket").value_or("");
        for (const char kCharacter : kTicket) {
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
        RAWFRAME_TRY_ASSIGN(captureChecksums_, captureAsked(configuration, "bots.divergence_capture"));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kAlarmLimit, configuration.unsignedInteger("bots.rollback_alarm", 30));
        if (kAlarmLimit > 1'000'000) {
            return missing("bots.rollback_alarm is at most a million rollbacks a second");
        }
        rollbackAlarm_ = static_cast<std::uint32_t>(kAlarmLimit);
        RAWFRAME_TRY_ASSIGN(retries_, configuration.unsignedInteger("bots.retries", 10));
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kRetryMs, configuration.unsignedInteger("bots.retry_ms", 1000));
        if (retries_ > 1000 || kRetryMs == 0 || kRetryMs > 60'000) {
            return missing("bots.retries is at most 1000, and bots.retry_ms 1 to 60000");
        }
        retryAfter_ = execution::MonotonicDuration::fromMilliseconds(static_cast<std::int64_t>(kRetryMs));
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
            RAWFRAME_TRY_ASSIGN(bot.sessions,
                                network::Sessions::client(*bot.provider,
                                                          context.clock(),
                                                          {.profile = sessionProfile(1), .lanes = lanesOf(*plan_)}));
            if (plan_->messageRecord() != 0) {
                bot.messages = std::make_unique<MessageQueue>();
            }
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
                        .divergenceDrill = divergenceDrill_,
                        .captureChecksums = captureChecksums_};
                } else {
                    ++unpredicted_;
                }
            }
            // The process's local players play from the lent devices, each
            // from those paired to it (D363).
            if (player && index < localPlayers_) {
                player_ = true;
                if (sources == nullptr) {
                    return missing("bots.player needs the game's input sources");
                }
                RAWFRAME_TRY_ASSIGN(bot.source, sources->playerSource(index));
            } else if (sources != nullptr) {
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
                                                                    .interpolation = interpolation,
                                                                    .messages = bot.messages.get()}));
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
                // A server refuses as unavailable while it is not active or
                // not healthy (D212) and admits again once it is: a bot
                // refused so asks again a while later, a bounded number of
                // times (D379).
                if (const auto kRefusal = bot.client->rejection()) {
                    bot.refusal = kRefusal;
                }
                if (bot.connecting && bot.client->rejection() == network::RejectReason::Unavailable &&
                    bot.retries < retries_) {
                    if (!bot.retryAt) {
                        bot.retryAt = frame.now + retryAfter_;
                    } else if (frame.now >= *bot.retryAt) {
                        bot.retryAt.reset();
                        ++bot.retries;
                        ++retried_;
                        bot.connecting = false;
                    }
                }
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
                const std::uint64_t kHeard = bot.client->serverTick();
                if (!bot.firstHeard && kHeard != 0) {
                    bot.firstHeard = kHeard;
                }
                const std::uint64_t kDue =
                    std::min(1 + (kElapsed / 1'000'000U * accept.tickRateTicks / (1'000U * accept.tickRateSeconds)),
                             1 + kMaximumInputWindow + (bot.firstHeard ? kHeard - *bot.firstHeard : 0));
                // A frame covering many ticks samples each, as many as one
                // input window carries, and sends them in one window: a
                // player drawn at a few frames a second keeps its input at
                // the server's pace (D522). A bot further behind catches up
                // over the frames after.
                bool sampled = false;
                for (std::size_t burst = 0; burst < kMaximumInputWindow && bot.submitted < kDue;
                     ++burst, ++bot.submitted) {
                    steer(bot, bot.submitted);
                    static_cast<void>(bot.client->sampleInput(bot.input));
                    sampled = true;
                    // What the sample asked for with that input, after it
                    // (D425).
                    if (bot.source != nullptr) {
                        commands_.clear();
                        bot.source->takeCommands(commands_);
                        if (!commands_.empty()) {
                            static_cast<void>(bot.client->sendInputWindow());
                            sampled = false;
                        }
                        for (const PostedCommand& command : commands_) {
                            static_cast<void>(bot.client->sendCommand(command));
                        }
                    }
                }
                if (sampled) {
                    static_cast<void>(bot.client->sendInputWindow());
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
        std::uint64_t messagesReceived = 0;
        std::uint64_t commandsSent = 0;
        std::uint64_t windowsSent = 0;
        std::uint64_t heldBack = 0;
        std::uint64_t terminated = 0;
        PredictionStatistics predicted;
        InterpolationStatistics interpolated;
        std::uint64_t handed = 0;
        // The clients' side of a development capture: each bot's latest
        // records, a value a record, matched to the server's by tick and
        // checksum.
        for (std::size_t index = 0; index < bots_.size() && captureChecksums_; ++index) {
            for (const ChecksumCapture& kept : bots_[index].client->checksumCaptures()) {
                for (std::size_t component = 0; component < kept.values.size(); ++component) {
                    const std::string kValue = hexOf(kept.values[component]);
                    emitter_.log(diagnostics::Severity::Info,
                                 kChecksumCapture,
                                 "what a bot hashed for a checksum record",
                                 {diagnostics::field("bot", index),
                                  diagnostics::field("tick", kept.tick),
                                  diagnostics::field("checksum", kept.checksum),
                                  diagnostics::field("component", component),
                                  diagnostics::field("value", std::string_view{kValue})});
                }
            }
        }
        for (Bot& bot : bots_) {
            handed += bot.source != nullptr ? 1 : 0;
            interpolated.blended += bot.client->interpolationStatistics().blended;
            interpolated.newest += bot.client->interpolationStatistics().newest;
            admitted += bot.client->admitted() ? 1 : 0;
            // A bot asking again counts by the refusal it last heard.
            const std::optional<network::RejectReason> kRefusal = bot.client->admitted() ? std::nullopt
                                                                  : bot.client->rejection().has_value()
                                                                      ? bot.client->rejection()
                                                                      : bot.refusal;
            unavailable += kRefusal == network::RejectReason::Unavailable ? 1 : 0;
            noticed += bot.client->serverStopping() ? 1 : 0;
            full += kRefusal == network::RejectReason::Capacity ? 1 : 0;
            ticketInvalid += kRefusal == network::RejectReason::TicketInvalid ? 1 : 0;
            mirrored += bot.world->entityCount();
            stateDatagrams += bot.client->statistics().stateDatagrams;
            messagesReceived += bot.client->statistics().messagesReceived;
            commandsSent += bot.client->statistics().commandsSent;
            windowsSent += bot.client->statistics().inputWindowsSent;
            heldBack += bot.client->statistics().samplesHeldBack;
            terminated += bot.client->termination().has_value() ? 1 : 0;
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
                      diagnostics::field("messagesReceived", messagesReceived),
                      diagnostics::field("commandsSent", commandsSent),
                      diagnostics::field("terminated", terminated),
                      diagnostics::field("blended", interpolated.blended),
                      diagnostics::field("shownNewest", interpolated.newest),
                      diagnostics::field("retried", retried_),
                      // Input windows sent, one a frame however many ticks it
                      // samples, and samples left unlabelled to bring an early
                      // client back (D317, D522).
                      diagnostics::field("inputWindowsSent", windowsSent),
                      diagnostics::field("samplesHeldBack", heldBack)});
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
    std::vector<PostedCommand> commands_;
    /// Whether the first bot is the process's own player.
    bool player_ = false;
    std::uint32_t checksumInterval_ = 60;
    std::uint32_t rollbackAlarm_ = 30;
    /// Refusals as unavailable each bot asks again after, how long after,
    /// and the askings again of all (D379).
    std::uint64_t retries_ = 10;
    execution::MonotonicDuration retryAfter_ = execution::MonotonicDuration::fromSeconds(1);
    std::uint64_t retried_ = 0;
    bool divergenceDrill_ = false;
    bool captureChecksums_ = false;
    /// Bots that would predict but could not have a predictor.
    std::uint64_t unpredicted_ = 0;
    bool allAdmitted_ = false;
    std::uint64_t sourceFailures_ = 0;
    std::size_t localPlayers_ = 1;
    diagnostics::Emitter emitter_;
};

} // namespace

result::Result<composition::ParticipantOwner> makeBots(composition::ParticipantContext& context) noexcept {
    auto participant = std::make_unique<BotsParticipant>();
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kCount, context.configuration().unsignedInteger("bots.count", 0));
    if (kCount > 4096) {
        return missing("bots.count is at most 4096");
    }
    const auto kPlayer = context.configuration().text("bots.player");
    if (kPlayer.has_value() && *kPlayer != "true" && *kPlayer != "false") {
        return missing("bots.player is true or false");
    }
    const bool kPlaying = kPlayer == "true";
    // Split-screen (D362): how many of the first clients are local players,
    // shown and presented; one unless told. With the process's player, they
    // are all played from its devices (D363), and the bots come after them;
    // without, they are the first bots.
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kLocal, context.configuration().unsignedInteger("bots.local_players", 1));
    const std::uint64_t kClients = kCount + (kPlaying ? kLocal : 0);
    if (kLocal < 1 || kLocal > kMaximumLocalPlayers || (kClients != 0 && kLocal > kClients)) {
        return missing("bots.local_players is 1 to 4, and no more than the clients");
    }
    participant->showLocally(static_cast<std::size_t>(kLocal));
    if (kClients != 0) {
        RAWFRAME_TRY(participant->load(context, kClients, kPlaying));
    }
    return composition::ParticipantOwner{participant.release()};
}

} // namespace rawframe::world_replication
