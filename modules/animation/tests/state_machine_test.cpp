// State machines: transitions as the document holds them, and as they
// play: conditions, blends along a curve, interruption, cooldowns, phase
// resets, and instant chains bounded.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kIdleClip{2, 1};
constexpr base::Bits128 kWalkClip{2, 2};
constexpr base::Bits128 kJumpClip{2, 3};
constexpr std::uint64_t kGrounded = 0x6a00000000000001ULL;
constexpr std::uint64_t kMove = 0x6a00000000000002ULL;
constexpr std::uint64_t kFootstep = 0x5f3a0c2d9e81b746ULL;
constexpr std::uint64_t kMachine = 0x2000000000000000ULL;

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

/// A second of the root held at `x`, looping or not.
std::shared_ptr<const Clip> held(double x, Loop loop, double duration = 1.0, std::vector<ClipEvent> events = {}) {
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = duration,
             .loop = loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {x, 0, 0}}}}},
             .events = std::move(events)});
}

std::vector<NamedClip> clips() {
    return {
        {kIdleClip, held(0.0, Loop::Loop)},
        {kWalkClip,
         held(1.0,
              Loop::Loop,
              1.0,
              {ClipEvent{.event = kFootstep, .name = "footstep", .time = 0.5, .relevance = Relevance::Simulation}})},
        {kJumpClip, held(5.0, Loop::Clamp, 0.5)}};
}

/// Idle, walking, and a jump from anywhere that lands back in idle.
Graph mover() {
    return Graph{
        .parameters = {Parameter{.name = "grounded", .id = kGrounded, .type = ParameterType::Bool, .initial = {1, 0}},
                       Parameter{.name = "move", .id = kMove, .minimum = 0.0, .maximum = 1.0}},
        .nodes = {GraphNode{.id = 0x1100000000000000ULL, .node = ClipNode{.clip = kIdleClip}},
                  GraphNode{.id = 0x1200000000000000ULL, .node = ClipNode{.clip = kWalkClip}},
                  GraphNode{.id = 0x1300000000000000ULL, .node = ClipNode{.clip = kJumpClip}},
                  GraphNode{.id = kMachine,
                            .node =
                                StateMachineNode{
                                    .states = {State{.name = "idle", .from = {.node = 0x1100000000000000ULL}},
                                               State{.name = "jump", .from = {.node = 0x1300000000000000ULL}},
                                               State{.name = "walk", .from = {.node = 0x1200000000000000ULL}}},
                                    .entry = "idle",
                                    .transitions = {Transition{.from = std::nullopt,
                                                               .to = "jump",
                                                               .priority = 5,
                                                               .cooldown = 1.0,
                                                               .resetPhase = true,
                                                               .conditions = {ParameterCondition{.parameter = kGrounded,
                                                                                                 .value = 0.0}}},
                                                    Transition{.from = "idle",
                                                               .to = "walk",
                                                               .priority = 1,
                                                               .duration = 0.25,
                                                               .curve = BlendCurve::CubicInOut,
                                                               .interruption = Interruption::ByHigherPriority,
                                                               .conditions = {ParameterCondition{
                                                                   .parameter = kMove,
                                                                   .comparison = Comparison::Greater,
                                                                   .value = 0.1}}},
                                                    Transition{.from = "jump",
                                                               .to = "idle",
                                                               .duration = 0.125,
                                                               .resetPhase = true,
                                                               .conditions = {FinishedCondition{}}},
                                                    Transition{.from = "walk",
                                                               .to = "idle",
                                                               .priority = 1,
                                                               .duration = 0.25,
                                                               .conditions = {ParameterCondition{.parameter = kMove,
                                                                                                 .comparison = Comparison::LessOrEqual,
                                                                                                 .value = 0.1},
                                                                              EventCondition{kFootstep}}}}}},
                  GraphNode{.id = 0x3000000000000000ULL, .node = OutputNode{.pose = {.node = kMachine}}}},
        .presentation = {}};
}

struct Player {
    GraphInstance instance;
    PoseEvaluator evaluator;
    std::vector<GraphEvent> events;

    explicit Player(const Graph& graph, const EvaluationLimits& limits = {})
        : instance{*CompiledGraph::compile(graph, rig(), kSkeletonId, clips(), {}, limits)} {
    }

    void set(std::string_view name, double value) {
        RAWFRAME_EXPECT(instance.set(*instance.graph().parameter(name), {value, 0.0}).has_value());
    }

    /// Where the root is after `seconds` more.
    double x(double seconds) {
        RAWFRAME_EXPECT(instance.advance(seconds, events));
        Pose pose;
        evaluator.evaluate(instance, pose);
        return pose.bones[0].translation[0];
    }

    /// The machine's step, the last; its clips are idle, jump, then walk.
    [[nodiscard]] std::size_t machine() const {
        return instance.graph().steps().size() - 1;
    }
};

} // namespace

RAWFRAME_TEST(AStateMachineHasOneText) {
    const auto kText = writeGraph(mover());
    RAWFRAME_EXPECT(kText.has_value());
    if (!kText.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(
        kText->contains("\"type\": \"rawframe/state_machine@1\",\n      \"params\": {\n        \"entry\": "
                        "\"idle\",\n        \"transitions\": [\n          {\n            \"to\": \"jump\",\n"
                        "            \"priority\": 5,\n            \"cooldown\": 1,\n            "
                        "\"resetPhase\": true,\n            \"conditions\": [\n              {\n"
                        "                \"parameter\": \"6a00000000000001\",\n                "
                        "\"comparison\": \"equal\",\n                \"value\": false\n"));
    RAWFRAME_EXPECT(kText->contains("\"curve\": \"cubic_in_out\",\n            \"interruption\": "
                                    "\"by_higher_priority\",\n"));
    RAWFRAME_EXPECT(kText->contains("{\n                \"finished\": true\n              }"));
    RAWFRAME_EXPECT(kText->contains("\"inputs\": {\n        \"idle\": {\n"));
    const auto kRead = readGraph(*kText);
    RAWFRAME_EXPECT(kRead.has_value() && *kRead == mover());
    const auto kHash = semanticHash(mover());
    Graph slower = mover();
    std::get<StateMachineNode>(slower.nodes[3].node).transitions[1].duration = 0.5;
    const auto kSlower = semanticHash(slower);
    RAWFRAME_EXPECT(kHash.has_value() && kSlower.has_value() && *kHash != *kSlower);
}

RAWFRAME_TEST(AStateMachineOutOfItsRulesIsRefused) {
    std::vector<Graph> wrong(13, mover());
    const auto kMachineOf = [](Graph& graph) -> StateMachineNode& {
        return std::get<StateMachineNode>(graph.nodes[3].node);
    };
    kMachineOf(wrong[0]).entry = "fall";
    kMachineOf(wrong[1]).transitions[1].to = "idle";
    kMachineOf(wrong[2]).transitions[1].to = "fall";
    kMachineOf(wrong[3]).transitions[1].duration = -1.0;
    kMachineOf(wrong[4]).transitions[1].conditions = {PhaseCondition{1.5}};
    kMachineOf(wrong[5]).transitions[1].conditions = {EventCondition{0}};
    kMachineOf(wrong[6]).transitions[0].conditions = {
        ParameterCondition{.parameter = kGrounded, .comparison = Comparison::Less, .value = 1.0}};
    kMachineOf(wrong[7]).transitions[0].conditions = {ParameterCondition{.parameter = kGrounded, .value = 0.5}};
    kMachineOf(wrong[8]).transitions[0].conditions = {ParameterCondition{.parameter = 0x99}};
    // A state's own transition and one from any state at one priority.
    kMachineOf(wrong[9]).transitions[1].priority = 5;
    std::swap(kMachineOf(wrong[10]).transitions[1], kMachineOf(wrong[10]).transitions[2]);
    std::swap(kMachineOf(wrong[11]).states[0], kMachineOf(wrong[11]).states[1]);
    kMachineOf(wrong[12]).transitions[1].cooldown = std::nan("");
    for (const Graph& kGraph : wrong) {
        RAWFRAME_EXPECT(refusedWith(writeGraph(kGraph), AnimationError::GraphInvalid));
    }
    RAWFRAME_EXPECT(refusedWith(writeGraph(mover(), {.maximumTransitions = 3}), AnimationError::OverLimit));
    RAWFRAME_EXPECT(refusedWith(writeGraph(mover(), {.maximumConditions = 1}), AnimationError::OverLimit));
}

RAWFRAME_TEST(TransitionsBlendAlongTheirCurve) {
    Player player{mover()};
    RAWFRAME_EXPECT(player.x(0.1) == 0.0);
    player.set("move", 0.5);
    RAWFRAME_EXPECT(player.x(0.0) == 0.0);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 2);
    // Halfway through a cubic ease, halfway there; then there.
    RAWFRAME_EXPECT(player.x(0.125) == 0.5);
    RAWFRAME_EXPECT(player.instance.transition(player.machine()).has_value());
    RAWFRAME_EXPECT(std::abs(player.x(0.0625) - 0.84375) < 1e-15);
    RAWFRAME_EXPECT(player.x(0.0625) == 1.0 && !player.instance.transition(player.machine()).has_value());
    // Walking back needs the move gone and a footstep heard.
    player.set("move", 0.0);
    RAWFRAME_EXPECT(player.x(0.1) == 1.0);
    const double kUntilStep = 0.5 - player.instance.playhead(2);
    RAWFRAME_EXPECT(player.x(kUntilStep) == 1.0 && player.instance.state(player.machine()) == 0);
}

RAWFRAME_TEST(AHigherPriorityInterruptsAndCooldownsHoldBack) {
    Player player{mover()};
    player.set("move", 1.0);
    RAWFRAME_EXPECT(player.x(0.1) == 0.0 && player.instance.transition(player.machine()).has_value());
    // Leaving the ground mid-blend: the jump, instantly, from its start.
    player.set("grounded", 0.0);
    RAWFRAME_EXPECT(player.x(0.1) == 5.0 && player.instance.state(player.machine()) == 1);
    RAWFRAME_EXPECT(!player.instance.transition(player.machine()).has_value() && player.instance.playhead(1) == 0.0);
    // Played out, it lands in idle, which starts over.
    RAWFRAME_EXPECT(player.x(0.5) == 5.0);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 0 && player.instance.playhead(0) == 0.0);
    // Still off the ground, the jump waits out its cooldown from landing:
    // until then idle walks off as the move says.
    RAWFRAME_EXPECT(player.x(0.5) != 5.0 && player.instance.state(player.machine()) == 2);
    player.x(0.49);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 2);
    player.x(0.02);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 1);
}

RAWFRAME_TEST(InstantTransitionsChainOnlySoFar) {
    Graph ping = mover();
    auto& machine = std::get<StateMachineNode>(ping.nodes[3].node);
    machine.transitions = {Transition{.from = "idle", .to = "walk"}, Transition{.from = "walk", .to = "idle"}};
    Player player{ping, {.maximumHops = 3}};
    player.x(0.0);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 2);
    player.x(0.0);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 0);
}

RAWFRAME_TEST(GameplayRequestsTransitionsForOneAdvance) {
    constexpr std::uint64_t kGo = 0x7e00000000000001ULL;
    constexpr std::uint64_t kStop = 0x7e00000000000002ULL;
    Graph asked = mover();
    auto& machine = std::get<StateMachineNode>(asked.nodes[3].node);
    machine.transitions = {Transition{.from = "idle", .to = "walk", .conditions = {EventCondition{kGo}}},
                           Transition{.from = "walk", .to = "idle", .conditions = {EventCondition{kStop}}}};
    Player player{asked, {.maximumRequests = 2}};
    RAWFRAME_EXPECT(player.instance.graph().step(kMachine) == player.machine());
    player.x(0.1);
    // A request no transition from here takes is gone after the advance:
    // it does not wait for the state that would take it.
    RAWFRAME_EXPECT(player.instance.request(kStop));
    player.x(0.1);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 0);
    RAWFRAME_EXPECT(player.instance.request(kGo));
    player.x(0.1);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 2);
    // Past the limit the oldest is dropped, and says so.
    RAWFRAME_EXPECT(player.instance.request(kStop) && player.instance.request(kGo) && !player.instance.request(kGo));
    player.x(0.1);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 2);
    RAWFRAME_EXPECT(player.instance.request(kStop));
    player.x(0.1);
    RAWFRAME_EXPECT(player.instance.state(player.machine()) == 0);
}
