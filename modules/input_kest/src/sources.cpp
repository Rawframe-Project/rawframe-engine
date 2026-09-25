#include "rawframe/input_kest/sources.h"

#include "rawframe/input/actions.h"
#include "rawframe/input_kest/errors.h"
#include "rawframe/world/random.h"
#include "rawframe/world_kest/game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

namespace rawframe::input_kest {

namespace {

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, InputKestError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kInputKestDomain, code(error), why).error()};
}

result::Result<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::unexpected<result::Error>{
            refuse(result::ErrorClass::NotFound, InputKestError::BadSample, "a file the game names cannot be read")
                .error()
                .withContext("path", path.string())};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
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

input::DeviceId deviceFor(input::DeviceClass device) noexcept {
    switch (device) {
    case input::DeviceClass::Keyboard:
        return kKeyboard;
    case input::DeviceClass::Mouse:
        return kMouse;
    case input::DeviceClass::Gamepad:
        return kGamepad;
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
};

class BotSource final : public world_replication::InputSource {
public:
    result::Status build(const Shared& shared, std::uint64_t seed) {
        RAWFRAME_TRY_ASSIGN(mapper_, input::Mapper::create(shared.actions, {.players = 1}));
        RAWFRAME_TRY(mapper_->pair(kKeyboard, input::DeviceClass::Keyboard, {}));
        RAWFRAME_TRY(mapper_->pair(kMouse, input::DeviceClass::Mouse, {}));
        RAWFRAME_TRY(mapper_->pair(kGamepad, input::DeviceClass::Gamepad, {}));
        // A bot is in every context the set declares, in the order declared.
        for (std::size_t context = 0; context < shared.actions.contexts.size(); ++context) {
            RAWFRAME_TRY(mapper_->activate({}, context));
        }
        hand_.emplace(shared.actions, seed);
        doors_ = InputDoorContext{.mapper = mapper_.get(), .player = {}};
        kest::DoorTable table;
        RAWFRAME_TRY(kest::addStandardMath(table));
        RAWFRAME_TRY(addInputDoors(table, &doors_));
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
        hand_->act(*mapper_);
        mapper_->commit(tick);
        mapper_->endFrame();
        std::ranges::fill(input, std::byte{0});
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

private:
    std::unique_ptr<input::Mapper> mapper_;
    std::optional<Hand> hand_;
    InputDoorContext doors_;
    std::unique_ptr<kest::Machine> machine_;
    kest::Entry entry_;
    std::vector<kest::Value> frame_;
    std::string element_;
};

class Sources final : public world_replication::InputSourcePlan {
public:
    explicit Sources(Shared shared) noexcept : shared_(std::move(shared)) {
    }

    result::Result<std::unique_ptr<world_replication::InputSource>> botSource(std::uint64_t seed) override {
        auto source = std::make_unique<BotSource>();
        RAWFRAME_TRY(source->build(shared_, seed));
        return std::unique_ptr<world_replication::InputSource>{std::move(source)};
    }

private:
    Shared shared_;
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

result::Result<std::unique_ptr<world_replication::InputSourcePlan>> makeInputSources(const SourceSettings& settings) {
    const std::filesystem::path kGamePath{settings.game};
    RAWFRAME_TRY_ASSIGN(const std::string kText, readFile(kGamePath));
    RAWFRAME_TRY_ASSIGN(const world_kest::GameDescription kGame, world_kest::parseGame(kText));
    if (!kGame.controls) {
        return refuse(result::ErrorClass::NotFound, InputKestError::NoControls, "the game declares no controls");
    }
    const std::filesystem::path kBeside = kGamePath.parent_path();
    Shared shared;
    RAWFRAME_TRY_ASSIGN(const std::string kActions, readFile(kBeside / kGame.controls->actions));
    auto actions = input::readActionSet(kActions);
    if (!actions.has_value()) {
        return std::unexpected<result::Error>{
            std::move(actions).error().withContext("path", (kBeside / kGame.controls->actions).string())};
    }
    shared.actions = std::move(*actions);
    const std::string kProgram = (kBeside / kGame.controls->program).string();
    std::string report;
    auto program = kest::Program::compileFile(kProgram, settings.compile, &report);
    if (!program.has_value()) {
        return std::unexpected<result::Error>{
            std::move(program).error().withContext("path", kProgram).withContext("report", report)};
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
    return std::unique_ptr<world_replication::InputSourcePlan>{new Sources{std::move(shared)}};
}

} // namespace rawframe::input_kest
