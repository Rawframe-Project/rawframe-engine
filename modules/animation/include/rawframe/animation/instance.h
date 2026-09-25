#pragma once

// Playing a graph (SPEC-0035's evaluation contract): a graph compiled
// against one skeleton and its clips, shared by every entity that plays
// it, and each entity's own instance of it, advanced then posed.
//
// `advance` is the parameter and state phase: it moves every clip node's
// playhead, weighs each node by how much it reaches the output, and
// reports the events crossed by nodes weighed past the threshold, in the
// graph's own order. `evaluatePose` is the pose phase: it reads the
// instance and writes the pose, so instances pose in parallel. Both are
// deterministic: the same graph, parameters, and deltas give the same
// events and the same pose bits.
//
// Root motion (SPEC-0035, D134): when the skeleton declares a source, each
// advance also says how its clips moved the character, blended as their
// poses are, and `removeRootMotion` takes that motion out of the pose, so
// the World moves the entity by the one and draws the other where it is.

#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/sample.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rawframe::animation {

/// A parameter's place in its compiled graph.
struct ParameterIndex {
    std::uint32_t value = 0;

    friend constexpr auto operator<=>(const ParameterIndex&, const ParameterIndex&) noexcept = default;
};

/// A clip a graph may play, by its resource identity.
struct NamedClip {
    base::Bits128 id;
    std::shared_ptr<const Clip> clip;
};

/// A mask a graph may use, by its resource identity.
struct NamedMask {
    base::Bits128 id;
    std::shared_ptr<const Mask> mask;
};

/// SPEC-0035's named limit points for evaluation.
struct EvaluationLimits {
    /// An event fires only from a node weighed past this.
    double eventWeightThreshold = 1e-5;
    /// Events reported by one advance, across the graph.
    std::size_t maximumEvents = 256;
    /// Transitions one state machine takes in one advance, instant ones
    /// chaining into the next.
    std::size_t maximumHops = 4;
    /// Transition requests one instance holds until its next advance.
    std::size_t maximumRequests = 8;
};

class CompiledGraph {
public:
    /// Refuses (`GraphInvalid`) a graph that does not validate or holds a
    /// quarantined node, and (`BindingInvalid`) a clip node whose clip is
    /// not among `clips` or does not bind to the skeleton, a mask node
    /// whose mask is not among `masks` or is of another skeleton, or a
    /// stage naming a bone the skeleton lacks, or a two-bone tip without a
    /// parent and a grandparent; and (`GraphInvalid`) a field its clips
    /// animate as both `float` and `discrete`.
    [[nodiscard]] static result::Result<std::shared_ptr<const CompiledGraph>>
    compile(const Graph& graph,
            const Skeleton& skeleton,
            base::Bits128 skeletonId,
            std::span<const NamedClip> clips,
            std::span<const NamedMask> masks = {},
            const EvaluationLimits& limits = {});

    /// A parameter by its machine name, as a script names it.
    [[nodiscard]] std::optional<ParameterIndex> parameter(std::string_view name) const noexcept;
    /// A parameter by its identity, as replication and saves name it.
    [[nodiscard]] std::optional<ParameterIndex> parameter(std::uint64_t id) const noexcept;
    [[nodiscard]] const Parameter& declaration(ParameterIndex parameter) const noexcept {
        return parameters_[parameter.value];
    }
    [[nodiscard]] std::size_t parameterCount() const noexcept {
        return parameters_.size();
    }
    [[nodiscard]] const Pose& bindPose() const noexcept {
        return bind_;
    }
    /// A difference of nothing for every bone: where an additive clip's
    /// sampling starts.
    [[nodiscard]] const Pose& unchangedPose() const noexcept {
        return unchanged_;
    }
    /// Each bone's parent, in the skeleton's order; none for the root.
    [[nodiscard]] std::span<const std::optional<BoneIndex>> parents() const noexcept {
        return parents_;
    }
    /// The skeleton's root motion source; none keeps the motion in the pose.
    [[nodiscard]] const std::optional<RootMotionSource>& rootMotion() const noexcept {
        return rootMotion_;
    }

    /// A transition's condition as it plays.
    struct Test {
        Condition condition;
        /// A parameter condition's parameter.
        ParameterIndex parameter;
    };

    /// A transition as it plays, between a machine's states by their place.
    struct Move {
        /// None from any state.
        std::optional<std::size_t> from;
        std::size_t to = 0;
        std::int32_t priority = 0;
        double duration = 0.0;
        BlendCurve curve = BlendCurve::Linear;
        Interruption interruption = Interruption::None;
        double cooldown = 0.0;
        bool resetPhase = false;
        std::vector<Test> tests;
    };

    /// A state machine as it plays: its states are the step's inputs.
    struct Machine {
        std::size_t entry = 0;
        std::vector<Move> moves;
        /// Each state's clip steps, in step order: what its phase, finish,
        /// and events are read from.
        std::vector<std::vector<std::size_t>> clips;
    };

    /// One node as it plays: a clip, a blend of earlier nodes, or a state
    /// machine over them.
    struct Step {
        /// The graph node it plays, by its id.
        std::uint64_t node = 0;
        /// A clip node's clip; none for a blend.
        std::optional<BoundClip> clip;
        Loop loop = Loop::Clamp;
        /// A clip's speed: `speedParameter`'s value when it names one, else
        /// this literal.
        double speed = 1.0;
        std::optional<ParameterIndex> speedParameter;
        /// A blend's inputs: earlier steps, and their weights, each a
        /// parameter's value when it names one.
        std::vector<std::size_t> inputs;
        std::vector<double> weights;
        std::vector<std::optional<ParameterIndex>> weightParameters;
        /// A state machine's own; none for a clip or a blend.
        std::optional<Machine> machine;
        /// A mask's weight for each bone, its inputs inside then outside;
        /// empty for any other step.
        std::vector<double> mask;
        /// A blend space's place for each input (a line's uses the first
        /// number), empty for any other step; a plane's triangles, by
        /// their points' places; and its position, `positionParameter`'s
        /// value when it names one, else this literal.
        std::vector<std::array<double, 2>> points;
        std::vector<std::array<std::size_t, 3>> triangles;
        std::array<double, 2> position{};
        std::optional<ParameterIndex> positionParameter;
        /// Whether its pose is a difference (an additive clip, or nodes of
        /// them) rather than a pose; an additive step, whose first input is
        /// its base and the rest its layers.
        bool delta = false;
        bool additive = false;
        /// A blend's or blend space's phase sync, with a declared leader by
        /// its place among the inputs; a weighed one names none.
        bool phaseSync = false;
        std::optional<std::size_t> leader;
        /// A clip following a phase: the step whose sync it follows.
        std::optional<std::size_t> syncedBy;
        /// A clip's property tracks: each track's place in the clip, and
        /// its field's among the graph's properties.
        std::vector<std::pair<std::size_t, std::size_t>> propertyTracks;
        /// A clip's root translation and rotation tracks, by their place
        /// in it, when the skeleton declares root motion.
        std::optional<std::size_t> rootTranslation;
        std::optional<std::size_t> rootRotation;
    };

    /// A modifier stage as it plays: its bones by their place, and each
    /// number a literal or a parameter's value.
    struct Stage {
        struct Number {
            double literal = 0.0;
            std::optional<ParameterIndex> parameter;
        };
        /// Two-bone IK's tip, parent, and grandparent, or the one bone a
        /// look-at turns.
        std::array<BoneIndex, 3> bones{};
        bool lookAt = false;
        std::array<Number, 3> goal{};
        std::optional<std::array<Number, 3>> pole;
        std::array<double, 3> axis{};
        Number weight;
        Relevance relevance = Relevance::Presentation;
    };

    [[nodiscard]] std::span<const Stage> stages() const noexcept {
        return stages_;
    }

    /// A field the graph's clips animate (D139), and how.
    struct Property {
        PropertyBinding binding;
        Channel channel = Channel::Float;
    };

    /// Every field the graph's clips animate, in binding order, each once.
    [[nodiscard]] std::span<const Property> properties() const noexcept {
        return properties_;
    }

    /// The step playing the node of id `node`; none for a node the output
    /// does not reach, or no node.
    [[nodiscard]] std::optional<std::size_t> step(std::uint64_t node) const noexcept;

    /// Steps in evaluation order, inputs first; the last is the output's.
    [[nodiscard]] std::span<const Step> steps() const noexcept {
        return steps_;
    }
    [[nodiscard]] const EvaluationLimits& limits() const noexcept {
        return limits_;
    }

private:
    std::vector<Parameter> parameters_;
    std::vector<Step> steps_;
    std::vector<Stage> stages_;
    std::vector<Property> properties_;
    Pose bind_;
    Pose unchanged_;
    std::vector<std::optional<BoneIndex>> parents_;
    std::optional<RootMotionSource> rootMotion_;
    EvaluationLimits limits_;
};

/// An event a node crossed while advancing, and how much the node counted.
struct GraphEvent {
    std::uint64_t event = 0;
    Relevance relevance = Relevance::Presentation;
    bool reverse = false;
    double weight = 0.0;

    friend bool operator==(const GraphEvent&, const GraphEvent&) = default;
};

/// One entity's playing of a compiled graph: its parameters, each clip's
/// playhead, and each step's weight.
class GraphInstance {
public:
    explicit GraphInstance(std::shared_ptr<const CompiledGraph> graph);

    [[nodiscard]] const CompiledGraph& graph() const noexcept {
        return *graph_;
    }

    /// Sets a parameter, held within its bounds. Refuses (`GraphInvalid`)
    /// a value not of its type: a `bool` other than 0 or 1, an `int` not
    /// whole, or anything not finite.
    [[nodiscard]] result::Status set(ParameterIndex parameter, ParameterValue value);
    [[nodiscard]] ParameterValue get(ParameterIndex parameter) const noexcept {
        return values_[parameter.value];
    }

    /// Asks the graph's state machines for a transition on `event`
    /// (SPEC-0035's runtime transition request): during the next advance an
    /// event condition on it holds in every state, as one on an event the
    /// state's clips cross does, and the request is gone after it, taken or
    /// not. False when the limit's worth were already held and the oldest
    /// was dropped for it.
    bool request(std::uint64_t event);

    /// Moves every playhead by `delta` seconds of the World's time and
    /// appends the events crossed. False when more were crossed than the
    /// limits report.
    bool advance(double delta, std::vector<GraphEvent>& events);

    /// A step's playhead and weight, as the last advance left them.
    [[nodiscard]] double playhead(std::size_t step) const noexcept {
        return playheads_[step];
    }
    /// A state machine step's state, and the transition under way into it,
    /// if one is: its place among the machine's moves and seconds elapsed.
    [[nodiscard]] std::size_t state(std::size_t step) const noexcept {
        return machines_[step].current;
    }
    [[nodiscard]] std::optional<std::pair<std::size_t, double>> transition(std::size_t step) const noexcept;
    [[nodiscard]] double weight(std::size_t step) const noexcept {
        return weights_[step];
    }
    /// SPEC-0035's per-tick root delta: how the last advance moved and
    /// turned the character, in its own frame as it was before; the
    /// identity when the skeleton declares no root motion.
    [[nodiscard]] const Transform& rootMotion() const noexcept {
        return rootMotion_;
    }

private:
    friend class PoseEvaluator;

    /// A state machine's state, SPEC-0035's typed runtime state.
    struct MachineState {
        std::size_t current = 0;
        std::optional<std::size_t> move;
        /// The state a transition under way leaves.
        std::size_t source = 0;
        double elapsed = 0.0;
        /// Seconds since each state was last left.
        std::vector<double> sinceLeft;
    };

    void weigh();
    /// Each synced step's leader, from the shares the advance began with.
    void lead();
    /// Each blend's and machine's root motion from its inputs', as the
    /// last weigh shares them.
    void blendRootMotion();
    void runMachine(std::size_t step, double delta, std::span<const std::pair<std::size_t, std::uint64_t>> heard);
    [[nodiscard]] bool holds(const CompiledGraph::Test& test,
                             const CompiledGraph::Machine& machine,
                             std::size_t state,
                             std::span<const std::pair<std::size_t, std::uint64_t>> heard) const;
    /// Whether `event` is heard in `state`: crossed by one of its clips, or
    /// requested.
    [[nodiscard]] bool heardIn(std::uint64_t event,
                               const CompiledGraph::Machine& machine,
                               std::size_t state,
                               std::span<const std::pair<std::size_t, std::uint64_t>> heard) const;
    [[nodiscard]] std::size_t leader(const CompiledGraph::Machine& machine, std::size_t state) const;

    std::shared_ptr<const CompiledGraph> graph_;
    std::vector<ParameterValue> values_;
    std::vector<double> playheads_;
    std::vector<double> weights_;
    /// Each blend's and machine's inputs' shares of it, as the last
    /// advance weighed them.
    std::vector<std::vector<double>> shares_;
    /// Each clip step's speed at the last advance.
    std::vector<double> speeds_;
    std::vector<MachineState> machines_;
    /// Transition requests for the next advance, oldest first.
    std::vector<std::uint64_t> requests_;
    /// Each synced step's leader in the current advance, and every
    /// playhead as the advance began.
    std::vector<std::size_t> leaders_;
    std::vector<double> before_;
    /// Each step's root motion in the last advance, and the output's.
    std::vector<Transform> motions_;
    Transform rootMotion_;
};

/// The pose phase's working memory, one per thread that poses.
class PoseEvaluator {
public:
    /// Writes the instance's pose: the bind pose where nothing weighs, and
    /// before its first advance. Given `only` (a byte for each bone, as
    /// `boneSubset` makes it), bones whose byte is 0 keep their bind pose
    /// and cost nothing.
    void evaluate(const GraphInstance& instance, Pose& pose, std::span<const std::uint8_t> only = {});

    /// Each of the graph's properties as the instance plays it (D139), in
    /// `properties()` order: a `float` field weighed among the clips that
    /// animate it by how much each counts, a `discrete` one from the clip
    /// counting most (the first of equals); none where no clip counting
    /// anything animates it.
    void evaluateProperties(const GraphInstance& instance, std::vector<std::optional<double>>& values);

    /// SPEC-0035's modifier stages on a local pose the instance evaluated,
    /// in their declared order: all of them, or with `simulationOnly` only
    /// those of `Simulation` relevance. Goals are in the entity's own frame.
    /// Says how many stages turned something.
    std::size_t modify(const GraphInstance& instance, Pose& local, bool simulationOnly = false);

private:
    std::vector<Pose> poses_;
    Pose model_;
    std::vector<std::pair<double, double>> sums_;
};

/// Takes the root motion source's channels out of a local pose of the
/// graph's skeleton: its taken translation axes back to the bind's, and its
/// turn about the axis undone. Nothing without a source.
void removeRootMotion(const CompiledGraph& graph, Pose& local);

} // namespace rawframe::animation
