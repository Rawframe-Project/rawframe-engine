#include "rawframe/input_kest/sources.h"

#include "rawframe/input/actions.h"
#include "rawframe/input/navigation.h"
#include "rawframe/input/pairing.h"
#include "rawframe/input_kest/errors.h"
#include "rawframe/world/random.h"
#include "rawframe/world_kest/game_files.h"
#include "rawframe/world_replication/client_worlds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace rawframe::input_kest {

namespace {

constexpr diagnostics::EventIdentity kInputSummary{"input", "input_summary"};

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, InputKestError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kInputKestDomain, code(error), why).error()};
}

// The doors: one player's committed actions, by action identity.

template <typename Read> void actionDoor(kest::DoorCall& call, void* context) noexcept {
    const auto& doors = *static_cast<const InputDoorContext*>(context);
    const auto kAction = doors.mapper->actions().actionWithId(static_cast<std::uint64_t>(call.integer(0)));
    if (!kAction) {
        call.fail("the game's action set has no action of that identity");
        return;
    }
    Read{}(call, *doors.mapper, doors.player, *kAction);
}

struct ReadOn {
    void operator()(kest::DoorCall& call,
                    const input::Mapper& mapper,
                    input::PlayerSlot player,
                    std::size_t action) const noexcept {
        call.answerBoolean(mapper.committed(player, action).on);
    }
};
struct ReadPressed {
    void operator()(kest::DoorCall& call,
                    const input::Mapper& mapper,
                    input::PlayerSlot player,
                    std::size_t action) const noexcept {
        call.answerBoolean(mapper.pressedThisTick(player, action));
    }
};
struct ReadReleased {
    void operator()(kest::DoorCall& call,
                    const input::Mapper& mapper,
                    input::PlayerSlot player,
                    std::size_t action) const noexcept {
        call.answerBoolean(mapper.releasedThisTick(player, action));
    }
};
struct ReadX {
    void operator()(kest::DoorCall& call,
                    const input::Mapper& mapper,
                    input::PlayerSlot player,
                    std::size_t action) const noexcept {
        call.answerReal(mapper.committed(player, action).x);
    }
};
struct ReadY {
    void operator()(kest::DoorCall& call,
                    const input::Mapper& mapper,
                    input::PlayerSlot player,
                    std::size_t action) const noexcept {
        call.answerReal(mapper.committed(player, action).y);
    }
};

constexpr std::array<kest::Parameter, 1> kActionTakes = {kest::Slot::U64};
constexpr std::array<kest::Parameter, 1> kTruthGives = {kest::Slot::Bool};
constexpr std::array<kest::Parameter, 1> kAxisGives = {kest::Slot::F32};

/// The bot's devices.
constexpr input::DeviceId kKeyboard{1};
constexpr input::DeviceId kMouse{2};
constexpr input::DeviceId kGamepad{3};
constexpr input::DeviceId kTouch{4};

input::DeviceId deviceFor(input::DeviceClass device) noexcept {
    switch (device) {
    case input::DeviceClass::Keyboard:
        return kKeyboard;
    case input::DeviceClass::Mouse:
        return kMouse;
    case input::DeviceClass::Gamepad:
        return kGamepad;
    case input::DeviceClass::Touch:
        return kTouch;
    }
    return {};
}

/// A bot's hand on the controls the action set binds: now and then it
/// presses or lets go of a key or button, tilts a stick or a trigger, or
/// moves the mouse or wheel, from its own seeded stream.
class Hand {
public:
    Hand(const input::ActionSet& set, std::uint64_t seed)
        : random_(world::deriveStream(world::RootSeed{seed}, "rawframe.input_kest.bots", "hand")) {
        for (const input::Action& action : set.actions) {
            for (const input::Binding& binding : action.bindings) {
                for (const input::Control kControl : binding.controls) {
                    if (kControl.valid() && !std::ranges::contains(controls_, kControl)) {
                        controls_.push_back(kControl);
                    }
                }
            }
        }
        std::ranges::sort(controls_);
    }

    void act(input::Mapper& mapper) {
        // About one change every six ticks: a tenth of a second at 60 Hz.
        if (controls_.empty() || random_.nextU32() % 6 != 0) {
            return;
        }
        const input::Control kControl = controls_[random_.nextU32() % controls_.size()];
        input::ControlEvent event{.device = deviceFor(kControl.device), .control = kControl};
        const auto kSigned = [this] {
            return (random_.nextFloat() * 2.0F) - 1.0F;
        };
        switch (input::shapeOf(kControl)) {
        case input::ControlShape::Digital: {
            const auto kHeld = std::ranges::find(held_, kControl);
            event.x = kHeld == held_.end() ? 1.0F : 0.0F;
            if (kHeld == held_.end()) {
                held_.push_back(kControl);
            } else {
                held_.erase(kHeld);
            }
            break;
        }
        case input::ControlShape::Axis1:
            event.x = input::relative(kControl) ? std::copysign(1.0F, kSigned()) : random_.nextFloat();
            break;
        case input::ControlShape::Axis2:
            if (input::relative(kControl)) {
                event.x = kSigned() * 20.0F;
                event.y = kSigned() * 20.0F;
            } else {
                // Anywhere in the stick's circle, now and then let go.
                const float kAngle = random_.nextFloat() * 6.2831853F;
                const float kReach = random_.nextU32() % 4 == 0 ? 0.0F : std::sqrt(random_.nextFloat());
                event.x = std::cos(kAngle) * kReach;
                event.y = std::sin(kAngle) * kReach;
            }
            break;
        }
        mapper.submit(event);
    }

private:
    world::Pcg32 random_;
    std::vector<input::Control> controls_;
    std::vector<input::Control> held_;
};

/// What every source of one game shares.
struct Shared {
    input::ActionSet actions;
    std::shared_ptr<const kest::Program> program;
    std::string element;
    std::string entry;
    kest::MachineLimits limits;
    std::size_t inputSize = 0;
    input::Feed* feed = nullptr;
    std::optional<input::PairingPolicy> pairing;
    const view::PlayerViews* views = nullptr;
    const view::UiPointing* pointing = nullptr;
    view::UiTyping* typing = nullptr;
    /// The UI's navigation, and the first local player's actions with the
    /// engine's navigation actions added, where they are (D430).
    const view::UiNavigation* navigation = nullptr;
    std::optional<input::ActionSet> navigable;
    world_localization::GameText* text = nullptr;
    input::Navigation navigationActions;
    diagnostics::Emitter emitter;
    /// The game's commands the sample program lays out (D425).
    std::vector<world_kest::CommandKind> commands;
    /// Each effect kind's haptic output and how it is felt, by kind.
    std::vector<std::optional<std::pair<std::size_t, input::Haptic>>> felt;
};

/// The lent devices shared out among the local players (D363): the
/// client's feed routed, as any player's source next ticks, into each
/// player's own by the pairing table, made with as many players as have
/// sources then.
struct Routing {
    input::Feed* client = nullptr;
    std::optional<input::PairingPolicy> policy;
    std::optional<input::Pairing> pairing;
    std::vector<std::unique_ptr<input::Feed>> feeds;
    std::vector<input::Feed*> players;

    void route() {
        if (!pairing.has_value()) {
            // A single player's devices merged, local players' keyboard
            // first, unless the client names its policy.
            pairing.emplace(
                policy.value_or(feeds.size() > 1 ? input::PairingPolicy::KeyboardFirst : input::PairingPolicy::Merged),
                feeds.size());
        }
        client->route(*pairing, players);
    }
};

/// A player's controls through the game's mapping and sample function: a
/// bot's hand on its own devices, or the lent devices of the process's
/// player, which pair themselves as they connect.
class Source final : public world_replication::InputSource {
public:
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    /// A local player's source says what its devices gave and what its
    /// mapper did with them: what a test that clicks and types reads when
    /// a press reaches the game it should not have.
    ~Source() override {
        if (local_ && mapper_ != nullptr) {
            const input::MapperStatistics& kMapped = mapper_->statistics();
            emitter_.log(diagnostics::Severity::Info,
                         kInputSummary,
                         "what a local player's devices gave",
                         {diagnostics::field("player", player_),
                          diagnostics::field("actionPresses", kMapped.actionPresses),
                          diagnostics::field("uiTaken", kMapped.uiTaken),
                          diagnostics::field("releases", kMapped.releases),
                          diagnostics::field("droppedEvents", kMapped.droppedEvents),
                          diagnostics::field("unpairedEvents", kMapped.unpairedEvents),
                          diagnostics::field("feedDropped", feed_ != nullptr ? feed_->dropped() : 0)});
        }
    }

    /// A bot's source with a seed, which sees no view; local player
    /// `player`'s without, its devices those `routing` gives it in `feed`.
    result::Status build(const Shared& shared,
                         std::optional<std::uint64_t> seed,
                         std::shared_ptr<Routing> routing = nullptr,
                         input::Feed* feed = nullptr,
                         std::size_t player = 0) {
        // The first local player navigates the UI (D430); bots and other
        // players have the game's actions alone.
        const bool kNavigates = !seed.has_value() && player == 0 && shared.navigable.has_value();
        RAWFRAME_TRY_ASSIGN(mapper_,
                            input::Mapper::create(kNavigates ? *shared.navigable : shared.actions, {.players = 1}));
        local_ = !seed.has_value();
        player_ = player;
        emitter_ = shared.emitter;
        if (seed.has_value()) {
            RAWFRAME_TRY(mapper_->pair(kKeyboard, input::DeviceClass::Keyboard, {}));
            RAWFRAME_TRY(mapper_->pair(kMouse, input::DeviceClass::Mouse, {}));
            RAWFRAME_TRY(mapper_->pair(kGamepad, input::DeviceClass::Gamepad, {}));
            RAWFRAME_TRY(mapper_->pair(kTouch, input::DeviceClass::Touch, {}));
            hand_.emplace(shared.actions, *seed);
        } else {
            routing_ = std::move(routing);
            feed_ = feed;
        }
        // A player is in every context the set declares, in the order
        // declared, until a game can switch them.
        for (std::size_t context = 0; context < mapper_->actions().contexts.size(); ++context) {
            RAWFRAME_TRY(mapper_->activate({}, context));
        }
        // The navigation actions claim their controls only while the UI
        // holds focus.
        if (kNavigates) {
            navigation_ = shared.navigation;
            navigationActions_ = shared.navigationActions;
            mapper_->setEnabled({}, navigationActions_.context, false);
            ui_.navigation = navigation_;
        }
        doors_ = InputDoorContext{.mapper = mapper_.get(), .player = {}};
        kest::DoorTable table;
        RAWFRAME_TRY(kest::addStandardMath(table));
        RAWFRAME_TRY(addInputDoors(table, &doors_));
        view_ = ViewDoorContext{.views = seed.has_value() ? nullptr : shared.views, .player = player};
        RAWFRAME_TRY(addViewDoors(table, &view_));
        // The mouse is the first local player's; so is what fields give.
        if (!seed.has_value() && player == 0) {
            ui_.pointing = shared.pointing;
            submissions_ = shared.typing;
        }
        typing_ = seed.has_value() ? nullptr : shared.typing;
        RAWFRAME_TRY(addUiDoors(table, &ui_));
        text_ = TextDoorContext{.text = seed.has_value() ? nullptr : shared.text};
        RAWFRAME_TRY(addTextDoors(table, &text_));
        for (const world_kest::CommandKind& command : shared.commands) {
            auto kind = std::make_unique<CommandDoorContext::Kind>();
            kind->command = command;
            commands_.kinds.push_back(std::move(kind));
        }
        RAWFRAME_TRY(addCommandDoors(table, commands_));
        // The UI as the topmost routing node (D421): a press it takes is the
        // sample's to read, never an action's.
        if (const view::UiPointing* pointing = seed.has_value() ? nullptr : shared.pointing) {
            mapper_->setPointerTaker([this, pointing](input::PlayerSlot /*slot*/, float x, float y) {
                const std::optional<std::int64_t> kCode = pointing->press(x, y);
                if (kCode.has_value() && ui_.pressed == 0) {
                    ui_.pressed = *kCode;
                }
                return kCode.has_value();
            });
        }
        RAWFRAME_TRY_ASSIGN(machine_, kest::Machine::start(shared.program, table, kest::Trust::Trusted, shared.limits));
        auto entry = machine_->entry(shared.entry);
        if (!entry.has_value()) {
            return refuse(result::ErrorClass::InvalidArgument,
                          InputKestError::BadSample,
                          "the sample program has no function of the entry's name");
        }
        entry_ = *entry;
        frame_.assign(std::max<std::size_t>(entry_.frameSlots, 1), kest::Value{});
        element_ = shared.element;
        return {};
    }

    result::Status next(std::uint64_t tick, std::span<std::byte> input) override {
        ui_.pressed = 0;
        commands_.sent.clear();
        // A field's text given, one a tick, and the keyboard's actions gated
        // while a field holds focus (SPEC-0029, D426).
        ui_.submitted = 0;
        typedOf({}, ui_.typed);
        if (submissions_ != nullptr) {
            if (std::optional<view::Submitted> given = submissions_->takeSubmitted()) {
                ui_.submitted = given->press;
                typedOf(given->text, ui_.typed);
            }
        }
        const bool kNavigating = navigation_ != nullptr && navigation_->focused();
        if (navigation_ != nullptr) {
            mapper_->setEnabled({}, navigationActions_.context, kNavigating);
        }
        if (hand_.has_value()) {
            hand_->act(*mapper_);
        } else {
            routing_->route();
            // The host places the text gate among the keys (D430); here it
            // only ends as the UI is now.
            feed_->deliver(*mapper_, {});
            if (typing_ != nullptr) {
                mapper_->setTextEditing(typing_->editing());
            }
        }
        mapper_->commit(tick);
        if (kNavigating) {
            navigate();
        }
        mapper_->endFrame();
        // The sample starts from the input it made the tick before, nought
        // at first: a value it keeps there, as a view's heading, is whole in
        // every command, so a command the server holds over for a late one
        // repeats it rather than losing a turn (D525).
        RAWFRAME_TRY_ASSIGN(const kest::Value kLent, machine_->lend(input.data(), 1, element_, input.size()));
        std::ranges::fill(frame_, kest::Value{});
        frame_[0] = kLent;
        auto outcome = machine_->call(entry_, frame_);
        machine_->endLend(kLent);
        if (outcome.isCancelled() || outcome.isError()) {
            return refuse(
                result::ErrorClass::FailedPrecondition, InputKestError::SampleFailed, "the sample function refused");
        }
        return {};
    }

    /// The navigation actions pressed this tick, to the UI in their order:
    /// a node activated is the sample's press, unless a pointer's came
    /// first (D430).
    void navigate() {
        constexpr std::array<view::NavigationMove, 6> kMoves = {view::NavigationMove::Up,
                                                                view::NavigationMove::Down,
                                                                view::NavigationMove::Left,
                                                                view::NavigationMove::Right,
                                                                view::NavigationMove::Next,
                                                                view::NavigationMove::Previous};
        for (std::size_t each = 0; each < input::kNavigationActions; ++each) {
            if (!mapper_->pressedThisTick({}, navigationActions_.actions[each])) {
                continue;
            }
            switch (static_cast<input::NavigationAction>(each)) {
            case input::NavigationAction::Activate:
                if (const std::optional<std::int64_t> kCode = navigation_->activate();
                    kCode.has_value() && ui_.pressed == 0) {
                    ui_.pressed = *kCode;
                }
                break;
            case input::NavigationAction::Dismiss:
                navigation_->dismiss();
                break;
            default:
                static_cast<void>(navigation_->move(kMoves[each]));
                break;
            }
        }
    }

    void takeCommands(std::vector<world_replication::PostedCommand>& into) override {
        std::ranges::move(commands_.sent, std::back_inserter(into));
        commands_.sent.clear();
    }

    /// The mapper, which the player's haptics share.
    [[nodiscard]] std::shared_ptr<input::Mapper> mapper() const noexcept {
        return mapper_;
    }

private:
    std::shared_ptr<input::Mapper> mapper_;
    std::optional<Hand> hand_;
    std::shared_ptr<Routing> routing_;
    input::Feed* feed_ = nullptr;
    InputDoorContext doors_;
    ViewDoorContext view_;
    UiDoorContext ui_;
    TextDoorContext text_;
    bool local_ = false;
    std::size_t player_ = 0;
    diagnostics::Emitter emitter_;
    view::UiTyping* typing_ = nullptr;
    view::UiTyping* submissions_ = nullptr;
    const view::UiNavigation* navigation_ = nullptr;
    input::Navigation navigationActions_;
    CommandDoorContext commands_;
    std::unique_ptr<kest::Machine> machine_;
    kest::Entry entry_;
    std::vector<kest::Value> frame_;
    std::string element_;
};

class Sources final : public InputSources {
public:
    explicit Sources(Shared shared) noexcept : shared_(std::move(shared)) {
    }

    result::Result<std::unique_ptr<world_replication::InputSource>> botSource(std::uint64_t seed) override {
        auto source = std::make_unique<Source>();
        RAWFRAME_TRY(source->build(shared_, seed));
        return std::unique_ptr<world_replication::InputSource>{std::move(source)};
    }

    result::Result<std::unique_ptr<world_replication::InputSource>> playerSource(std::size_t player) override {
        if (shared_.feed == nullptr) {
            return refuse(result::ErrorClass::NotFound, InputKestError::NoDevices, "the host lends no devices");
        }
        if (player >= world_replication::kMaximumLocalPlayers) {
            return refuse(
                result::ErrorClass::InvalidArgument, InputKestError::NoDevices, "past the local players there may be");
        }
        if (routing_ == nullptr) {
            routing_ = std::make_shared<Routing>();
            routing_->client = shared_.feed;
            routing_->policy = shared_.pairing;
        }
        if (player < routing_->feeds.size() && routing_->feeds[player] != nullptr) {
            return refuse(
                result::ErrorClass::AlreadyExists, InputKestError::NoDevices, "the player's devices have a source");
        }
        while (routing_->feeds.size() <= player) {
            routing_->feeds.push_back(nullptr);
        }
        routing_->feeds[player] = std::make_unique<input::Feed>();
        routing_->players.clear();
        for (const std::unique_ptr<input::Feed>& feed : routing_->feeds) {
            routing_->players.push_back(feed.get());
        }
        auto source = std::make_unique<Source>();
        RAWFRAME_TRY(source->build(shared_, std::nullopt, routing_, routing_->feeds[player].get(), player));
        if (player == 0) {
            player_ = source->mapper();
        }
        return std::unique_ptr<world_replication::InputSource>{std::move(source)};
    }

    [[nodiscard]] bool feelsEffects() const noexcept override {
        return std::ranges::any_of(shared_.felt, [](const auto& each) {
            return each.has_value();
        });
    }

    std::optional<std::size_t> feelEffect(std::uint32_t kind) override {
        if (player_ == nullptr || kind >= shared_.felt.size() || !shared_.felt[kind].has_value()) {
            return std::nullopt;
        }
        const auto& [kHaptic, kFelt] = *shared_.felt[kind];
        commands_.clear();
        player_->feel({}, kHaptic, kFelt, commands_);
        for (const input::HapticCommand& command : commands_) {
            shared_.feed->feel(command);
        }
        return commands_.size();
    }

private:
    Shared shared_;
    /// The lent devices' routing among the local players' sources.
    std::shared_ptr<Routing> routing_;
    /// The first player's mapper, shared with its source: which devices it
    /// has. Effects are felt on its devices.
    std::shared_ptr<input::Mapper> player_;
    std::vector<input::HapticCommand> commands_;
};

} // namespace

result::Status addInputDoors(kest::DoorTable& doors, const InputDoorContext* context) {
    void* const kContext = const_cast<InputDoorContext*>(context);
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Input.on",
                                      .function = &actionDoor<ReadOn>,
                                      .context = kContext,
                                      .takes = kActionTakes,
                                      .gives = kTruthGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Input.pressed",
                                      .function = &actionDoor<ReadPressed>,
                                      .context = kContext,
                                      .takes = kActionTakes,
                                      .gives = kTruthGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Input.released",
                                      .function = &actionDoor<ReadReleased>,
                                      .context = kContext,
                                      .takes = kActionTakes,
                                      .gives = kTruthGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Input.x",
                                      .function = &actionDoor<ReadX>,
                                      .context = kContext,
                                      .takes = kActionTakes,
                                      .gives = kAxisGives,
                                      .safeForUntrusted = true}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Input.y",
                                      .function = &actionDoor<ReadY>,
                                      .context = kContext,
                                      .takes = kActionTakes,
                                      .gives = kAxisGives,
                                      .safeForUntrusted = true}));
    return {};
}

result::Result<std::unique_ptr<InputSources>> makeInputSources(const SourceSettings& settings) {
    if (settings.game == nullptr || !settings.game->named()) {
        return refuse(result::ErrorClass::NotFound, InputKestError::NoControls, "no game is named");
    }
    const world_kest::GameDescription& kGame = settings.game->description();
    if (!kGame.controls) {
        return refuse(result::ErrorClass::NotFound, InputKestError::NoControls, "the game declares no controls");
    }
    Shared shared;
    RAWFRAME_TRY_ASSIGN(const std::string_view kActions, settings.game->document(kGame.controls->actions));
    auto actions = input::readActionSet(kActions);
    if (!actions.has_value()) {
        return std::unexpected<result::Error>{std::move(actions).error().withContext("name", kGame.controls->actions)};
    }
    shared.actions = std::move(*actions);
    for (const world_kest::GameEffect& effect : kGame.effects) {
        if (!effect.felt.has_value()) {
            shared.felt.emplace_back();
            continue;
        }
        const auto kHaptic = shared.actions.hapticNamed(effect.felt->haptic);
        if (!kHaptic.has_value()) {
            return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                         InputKestError::UnknownHaptic,
                                                         "an effect is felt by a haptic output the actions lack")
                                                      .error()
                                                      .withContext("effect", effect.name)
                                                      .withContext("haptic", effect.felt->haptic)};
        }
        shared.felt.emplace_back(std::pair{*kHaptic,
                                           input::Haptic{.amplitude = effect.felt->amplitude,
                                                         .frequency = effect.felt->frequency,
                                                         .milliseconds = effect.felt->milliseconds}});
    }
    std::string report;
    auto program = settings.game->compile(kGame.controls->program, settings.compile, &report);
    if (!program.has_value()) {
        return std::unexpected<result::Error>{
            std::move(program).error().withContext("program", kGame.controls->program).withContext("report", report)};
    }
    shared.program = std::move(*program);
    const auto kInput = std::ranges::find(kGame.components, kGame.input, &world_kest::GameComponent::name);
    if (kInput == kGame.components.end()) {
        return refuse(
            result::ErrorClass::InvalidArgument, InputKestError::BadSample, "the input line names no component");
    }
    shared.element = kInput->kestType;
    RAWFRAME_TRY_ASSIGN(const kest::TypeLayout kLayout, shared.program->layout(shared.element));
    if (kLayout.size != settings.inputSize) {
        return refuse(result::ErrorClass::InvalidArgument,
                      InputKestError::BadSample,
                      "the sample program's input type is not the size of the game's input component");
    }
    shared.entry = kGame.controls->entry;
    shared.limits = settings.limits;
    shared.inputSize = settings.inputSize;
    shared.feed = settings.feed;
    shared.pairing = settings.pairing;
    shared.views = settings.views;
    shared.pointing = settings.pointing;
    shared.typing = settings.typing;
    shared.text = settings.text;
    if (settings.navigation != nullptr) {
        shared.navigation = settings.navigation;
        shared.navigable = shared.actions;
        RAWFRAME_TRY_ASSIGN(shared.navigationActions, input::addNavigation(*shared.navigable));
    }
    shared.emitter = settings.emitter;
    RAWFRAME_TRY_ASSIGN(shared.commands, world_kest::commandKindsOf(kGame, *shared.program, false));
    return std::unique_ptr<InputSources>{new Sources{std::move(shared)}};
}

} // namespace rawframe::input_kest
