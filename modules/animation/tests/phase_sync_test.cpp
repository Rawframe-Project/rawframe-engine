// Phase sync (SPEC-0035, D136): a blend's or blend space's clip inputs
// play at one phase, a leader's, weighed or declared, and followers fire
// the events they cross on the way.

#include "rawframe/animation/errors.h"
#include "rawframe/animation/instance.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace rawframe;
using namespace rawframe::animation;

namespace {

bool refusedWith(const auto& outcome, AnimationError error) {
    return !outcome.has_value() && outcome.error().domain() == kAnimationDomain &&
           outcome.error().code() == code(error);
}

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-12;
}

constexpr base::Bits128 kSkeletonId{7, 7};
constexpr base::Bits128 kRoot{1, 1};
constexpr base::Bits128 kWalkId{2, 1};
constexpr base::Bits128 kRunId{2, 2};
constexpr std::uint64_t kStride = 0x5f3a0c2d9e81b746ULL;
constexpr std::uint64_t kPace = 0x6a00000000000001ULL;

Skeleton rig() {
    return Skeleton{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
}

/// A looping cycle of `duration` seconds with a stride at its middle.
std::shared_ptr<const Clip> cycle(double duration) {
    return std::make_shared<const Clip>(
        Clip{.skeleton = kSkeletonId,
             .duration = duration,
             .loop = Loop::Loop,
             .tracks = {Track{.bone = kRoot, .channel = Channel::Translation, .keys = {Key{.value = {}}}}},
             .events = {ClipEvent{.event = kStride, .name = "stride", .time = duration / 2.0}}});
}

/// A walk of a second (node 1) and a run of two (node 2), blended by
/// `pace` (the run's weight, the walk's being one) with `sync`.
Graph locomotion(std::optional<PhaseSync> sync) {
    return Graph{.parameters = {Parameter{.name = "pace", .id = kPace, .initial = {0.25, 0.0}}},
                 .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kWalkId}},
                           GraphNode{.id = 2, .node = ClipNode{.clip = kRunId, .speed = 3.0}},
                           GraphNode{.id = 3,
                                     .node = BlendNode{.inputs = {BlendInput{.name = "run",
                                                                             .from = {.node = 2},
                                                                             .weight = ParameterRef{kPace}},
                                                                  BlendInput{.name = "walk", .from = {.node = 1}}},
                                                       .phaseSync = std::move(sync)}},
                           GraphNode{.id = 4, .node = OutputNode{.pose = {.node = 3}}}},
                 .presentation = {}};
}

/// The walk's playhead, and the run's.
double walkAt(const GraphInstance& instance) {
    return instance.playhead(*instance.graph().step(1));
}
double runAt(const GraphInstance& instance) {
    return instance.playhead(*instance.graph().step(2));
}

GraphInstance playing(const Graph& graph) {
    const std::vector<NamedClip> kClips{{kWalkId, cycle(1.0)}, {kRunId, cycle(2.0)}};
    return GraphInstance{*CompiledGraph::compile(graph, rig(), kSkeletonId, kClips)};
}

} // namespace

RAWFRAME_TEST(PhaseSyncHasOneText) {
    for (const std::optional<PhaseSync>& kSync :
         {std::optional{PhaseSync{}}, std::optional{PhaseSync{.leader = PhaseLeader::Declared, .input = "run"}}}) {
        const auto kText = writeGraph(locomotion(kSync));
        RAWFRAME_EXPECT(kText.has_value() && readGraph(*kText) == locomotion(kSync));
    }
    const auto kDeclared = writeGraph(locomotion(PhaseSync{.leader = PhaseLeader::Declared, .input = "run"}));
    RAWFRAME_EXPECT(kDeclared.has_value() &&
                    kDeclared->contains("\"params\": {\n        \"leader\": \"run\",\n        \"phaseSync\": "
                                        "\"declared_leader\",\n        \"weights\": {"));
    // A leader it lacks, or a weighed one naming one.
    RAWFRAME_EXPECT(refusedWith(writeGraph(locomotion(PhaseSync{.leader = PhaseLeader::Declared, .input = "jog"})),
                                AnimationError::GraphInvalid));
    RAWFRAME_EXPECT(refusedWith(writeGraph(locomotion(PhaseSync{.leader = PhaseLeader::Weight, .input = "run"})),
                                AnimationError::GraphInvalid));
    // A declared leader that is no clip cannot be played.
    Graph nested = locomotion(PhaseSync{.leader = PhaseLeader::Declared, .input = "walk"});
    std::get<BlendNode>(nested.nodes[2].node).inputs[1].from.node = 5;
    nested.nodes.push_back(
        GraphNode{.id = 5, .node = BlendNode{.inputs = {BlendInput{.name = "only", .from = {.node = 1}}}}});
    const std::vector<NamedClip> kClips{{kWalkId, cycle(1.0)}, {kRunId, cycle(2.0)}};
    RAWFRAME_EXPECT(
        refusedWith(CompiledGraph::compile(nested, rig(), kSkeletonId, kClips), AnimationError::GraphInvalid));
}

RAWFRAME_TEST(FollowersPlayAtTheLeadersPhase) {
    // Unsynced, the run moves at its own speed of three.
    GraphInstance free = playing(locomotion(std::nullopt));
    std::vector<GraphEvent> events;
    RAWFRAME_EXPECT(free.advance(0.25, events));
    RAWFRAME_EXPECT(near(walkAt(free), 0.25) && near(runAt(free), 0.75));
    // Weighed: the walk leads (a share of four fifths); a quarter second
    // is a quarter of the way through both, the run's half second.
    GraphInstance weighed = playing(locomotion(PhaseSync{}));
    RAWFRAME_EXPECT(weighed.advance(0.25, events));
    RAWFRAME_EXPECT(near(walkAt(weighed), 0.25) && near(runAt(weighed), 0.5));
    // Past the middle, both stride once, the run at its own middle.
    events.clear();
    RAWFRAME_EXPECT(weighed.advance(0.5, events) && events.size() == 2);
    RAWFRAME_EXPECT(near(runAt(weighed), 1.5));
    // The run weighed more leads at its own speed: three quarters of a
    // second at three is past a whole run.
    RAWFRAME_EXPECT(weighed.set(ParameterIndex{0}, {4.0, 0.0}).has_value());
    RAWFRAME_EXPECT(weighed.advance(0.0, events));
    RAWFRAME_EXPECT(weighed.advance(0.5, events));
    RAWFRAME_EXPECT(near(runAt(weighed), 1.0) && near(walkAt(weighed), 0.5));
    // Declared: the run leads whatever it weighs.
    GraphInstance declared = playing(locomotion(PhaseSync{.leader = PhaseLeader::Declared, .input = "run"}));
    RAWFRAME_EXPECT(declared.advance(0.25, events));
    RAWFRAME_EXPECT(near(runAt(declared), 0.75) && near(walkAt(declared), 0.375));
}
