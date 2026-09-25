#include "rawframe/animation/instance.h"

#include "rawframe/animation/errors.h"
#include "rotation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace rawframe::animation {

namespace {

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::GraphInvalid), why);
}

std::unexpected<result::Error> unbound(std::string_view why) {
    return result::fail(
        result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::BindingInvalid), why);
}

const GraphNode& nodeOf(const Graph& graph, std::uint64_t id) {
    return *std::ranges::lower_bound(graph.nodes, id, {}, &GraphNode::id);
}

/// The nodes a node takes, in name order: a blend's inputs, a machine's
/// states.
std::vector<std::uint64_t> inputsOf(const GraphNode& node) {
    std::vector<std::uint64_t> made;
    if (const auto* kBlend = std::get_if<BlendNode>(&node.node)) {
        for (const BlendInput& input : kBlend->inputs) {
            made.push_back(input.from.node);
        }
    } else if (const auto* kMachine = std::get_if<StateMachineNode>(&node.node)) {
        for (const State& state : kMachine->states) {
            made.push_back(state.from.node);
        }
    }
    return made;
}

/// Graph nodes in evaluation order from the output's input down: each
/// after its inputs, which are taken in name order, each once.
std::vector<const GraphNode*> evaluationOrder(const Graph& graph) {
    const auto kOutput = std::ranges::find_if(graph.nodes, [](const GraphNode& node) {
        return std::holds_alternative<OutputNode>(node.node);
    });
    std::vector<const GraphNode*> order;
    std::vector<std::uint64_t> seen;
    std::vector<std::pair<const GraphNode*, std::size_t>> path{
        {&nodeOf(graph, std::get<OutputNode>(kOutput->node).pose.node), 0}};
    while (!path.empty()) {
        auto& [node, next] = path.back();
        const std::vector<std::uint64_t> kInputs = inputsOf(*node);
        if (next < kInputs.size()) {
            const std::uint64_t kInput = kInputs[next++];
            if (!std::ranges::contains(seen, kInput)) {
                seen.push_back(kInput);
                path.emplace_back(&nodeOf(graph, kInput), 0);
            }
            continue;
        }
        if (!std::ranges::contains(order, node)) {
            order.push_back(node);
        }
        path.pop_back();
    }
    return order;
}

double valueOf(const std::vector<ParameterValue>& values, double literal, std::optional<ParameterIndex> parameter) {
    return parameter.has_value() ? values[parameter->value][0] : literal;
}

/// How far a transition has blended, from its elapsed share of its
/// duration.
double blended(BlendCurve curve, double at) {
    at = std::clamp(at, 0.0, 1.0);
    return curve == BlendCurve::Linear ? at : at * at * (3.0 - (2.0 * at));
}

bool compared(Comparison comparison, double value, double with) {
    switch (comparison) {
    case Comparison::Equal:
        return value == with;
    case Comparison::NotEqual:
        return value != with;
    case Comparison::Less:
        return value < with;
    case Comparison::LessOrEqual:
        return value <= with;
    case Comparison::Greater:
        return value > with;
    case Comparison::GreaterOrEqual:
        return value >= with;
    }
    return false;
}

} // namespace

result::Result<std::shared_ptr<const CompiledGraph>> CompiledGraph::compile(const Graph& graph,
                                                                            const Skeleton& skeleton,
                                                                            base::Bits128 skeletonId,
                                                                            std::span<const NamedClip> clips,
                                                                            const EvaluationLimits& limits) {
    RAWFRAME_TRY(validate(graph));
    if (std::ranges::any_of(graph.nodes, [](const GraphNode& node) {
            return std::holds_alternative<QuarantinedNode>(node.node);
        })) {
        return invalid("a graph with a quarantined node cannot be played");
    }
    auto made = std::make_shared<CompiledGraph>();
    made->parameters_ = graph.parameters;
    made->bind_ = animation::bindPose(skeleton);
    for (const Bone& bone : skeleton.bones) {
        made->parents_.push_back(bone.parent);
    }
    made->limits_ = limits;
    const auto kParameter = [&graph](const Scalar& scalar) -> std::optional<ParameterIndex> {
        const auto* kRef = std::get_if<ParameterRef>(&scalar);
        if (kRef == nullptr) {
            return std::nullopt;
        }
        const auto kFound = std::ranges::find(graph.parameters, kRef->parameter, &Parameter::id);
        return ParameterIndex{static_cast<std::uint32_t>(kFound - graph.parameters.begin())};
    };
    const auto kLiteral = [](const Scalar& scalar) {
        const auto* kNumber = std::get_if<double>(&scalar);
        return kNumber != nullptr ? *kNumber : 0.0;
    };
    const std::vector<const GraphNode*> kOrder = evaluationOrder(graph);
    std::map<std::uint64_t, std::size_t> steps;
    for (const GraphNode* node : kOrder) {
        Step step;
        if (const auto* kClip = std::get_if<ClipNode>(&node->node)) {
            const auto kNamed = std::ranges::find(clips, kClip->clip, &NamedClip::id);
            if (kNamed == clips.end() || kNamed->clip == nullptr) {
                return unbound("a graph plays only clips it is given");
            }
            std::shared_ptr<const Clip> clip = kNamed->clip;
            if (kClip->loop.has_value() && *kClip->loop != clip->loop) {
                // A node's loop is the clip's as it plays there, and must be
                // one the clip could have had.
                auto looped = std::make_shared<Clip>(*clip);
                looped->loop = *kClip->loop;
                if (!animation::validate(*looped).has_value()) {
                    return unbound("a clip looped by its node has nothing at its duration");
                }
                clip = std::move(looped);
            }
            RAWFRAME_TRY_ASSIGN(BoundClip bound, BoundClip::bind(clip, skeleton, skeletonId));
            step.clip = std::move(bound);
            step.loop = clip->loop;
            step.speed = kLiteral(kClip->speed);
            step.speedParameter = kParameter(kClip->speed);
        } else if (const auto* kBlend = std::get_if<BlendNode>(&node->node)) {
            for (const BlendInput& input : kBlend->inputs) {
                step.inputs.push_back(steps.at(input.from.node));
                step.weights.push_back(kLiteral(input.weight));
                step.weightParameters.push_back(kParameter(input.weight));
            }
        } else {
            const auto& machine = std::get<StateMachineNode>(node->node);
            const auto kStateOf = [&machine](std::string_view name) {
                return static_cast<std::size_t>(std::ranges::find(machine.states, name, &State::name) -
                                                machine.states.begin());
            };
            Machine machineStep{.entry = kStateOf(machine.entry), .moves = {}, .clips = {}};
            for (const State& state : machine.states) {
                step.inputs.push_back(steps.at(state.from.node));
                // The clips the state reaches, each once, in step order.
                std::vector<std::size_t> reached;
                std::vector<std::size_t> pending{step.inputs.back()};
                while (!pending.empty()) {
                    const std::size_t kAt = pending.back();
                    pending.pop_back();
                    if (std::ranges::contains(reached, kAt)) {
                        continue;
                    }
                    reached.push_back(kAt);
                    std::ranges::copy(made->steps_[kAt].inputs, std::back_inserter(pending));
                }
                std::erase_if(reached, [&made](std::size_t each) {
                    return !made->steps_[each].clip.has_value();
                });
                std::ranges::sort(reached);
                machineStep.clips.push_back(std::move(reached));
            }
            for (const Transition& transition : machine.transitions) {
                Move move{.from = std::nullopt,
                          .to = kStateOf(transition.to),
                          .priority = transition.priority,
                          .duration = transition.duration,
                          .curve = transition.curve,
                          .interruption = transition.interruption,
                          .cooldown = transition.cooldown,
                          .resetPhase = transition.resetPhase,
                          .tests = {}};
                if (transition.from.has_value()) {
                    move.from = kStateOf(*transition.from);
                }
                for (const Condition& condition : transition.conditions) {
                    Test test{.condition = condition, .parameter = {}};
                    if (const auto* kCompared = std::get_if<ParameterCondition>(&condition)) {
                        test.parameter = *kParameter(ParameterRef{kCompared->parameter});
                    }
                    move.tests.push_back(test);
                }
                machineStep.moves.push_back(std::move(move));
            }
            step.machine = std::move(machineStep);
        }
        step.node = node->id;
        steps.emplace(node->id, made->steps_.size());
        made->steps_.push_back(std::move(step));
    }
    return std::shared_ptr<const CompiledGraph>{std::move(made)};
}

std::optional<ParameterIndex> CompiledGraph::parameter(std::string_view name) const noexcept {
    const auto kFound = std::ranges::lower_bound(parameters_, name, {}, &Parameter::name);
    if (kFound == parameters_.end() || kFound->name != name) {
        return std::nullopt;
    }
    return ParameterIndex{static_cast<std::uint32_t>(kFound - parameters_.begin())};
}

std::optional<ParameterIndex> CompiledGraph::parameter(std::uint64_t id) const noexcept {
    const auto kFound = std::ranges::find(parameters_, id, &Parameter::id);
    if (kFound == parameters_.end()) {
        return std::nullopt;
    }
    return ParameterIndex{static_cast<std::uint32_t>(kFound - parameters_.begin())};
}

GraphInstance::GraphInstance(std::shared_ptr<const CompiledGraph> graph)
    : graph_{std::move(graph)}, playheads_(graph_->steps().size(), 0.0), weights_(graph_->steps().size(), 0.0),
      shares_(graph_->steps().size()), speeds_(graph_->steps().size(), 0.0), machines_(graph_->steps().size()) {
    for (std::size_t at = 0; at < graph_->parameterCount(); ++at) {
        values_.push_back(graph_->declaration(ParameterIndex{static_cast<std::uint32_t>(at)}).initial);
    }
    for (std::size_t at = 0; at < graph_->steps().size(); ++at) {
        const std::optional<CompiledGraph::Machine>& machine = graph_->steps()[at].machine;
        if (machine.has_value()) {
            machines_[at].current = machine->entry;
            // Never left: no cooldown holds a state back at the start.
            machines_[at].sinceLeft.assign(machine->clips.size(), std::numeric_limits<double>::infinity());
        }
    }
}

std::optional<std::pair<std::size_t, double>> GraphInstance::transition(std::size_t step) const noexcept {
    const MachineState& machine = machines_[step];
    if (!machine.move.has_value()) {
        return std::nullopt;
    }
    return std::pair{*machine.move, machine.elapsed};
}

result::Status GraphInstance::set(ParameterIndex parameter, ParameterValue value) {
    if (parameter.value >= values_.size()) {
        return invalid("a parameter of the graph");
    }
    const Parameter& declared = graph_->declaration(parameter);
    const bool kFinite = std::isfinite(value[0]) && std::isfinite(value[1]);
    bool ofType = kFinite;
    switch (declared.type) {
    case ParameterType::Bool:
        ofType = ofType && (value[0] == 0.0 || value[0] == 1.0) && value[1] == 0.0;
        break;
    case ParameterType::Int:
        ofType = ofType && value[0] == std::trunc(value[0]) && value[1] == 0.0;
        break;
    case ParameterType::Float:
        ofType = ofType && value[1] == 0.0;
        break;
    case ParameterType::Vec2:
        break;
    }
    if (!ofType) {
        return invalid("a parameter is set to a value of its type");
    }
    if (declared.minimum.has_value()) {
        value[0] = std::max(value[0], *declared.minimum);
    }
    if (declared.maximum.has_value()) {
        value[0] = std::min(value[0], *declared.maximum);
    }
    values_[parameter.value] = value;
    return {};
}

void GraphInstance::weigh() {
    const std::span<const CompiledGraph::Step> kSteps = graph_->steps();
    // From the output down: a blend shares its own weight among its inputs
    // as their weights say, evenly when none weighs anything; a machine
    // gives it to its state, or splits it along a transition under way.
    std::ranges::fill(weights_, 0.0);
    weights_.back() = 1.0;
    for (std::size_t at = kSteps.size(); at-- > 0;) {
        const CompiledGraph::Step& step = kSteps[at];
        if (step.clip.has_value()) {
            continue;
        }
        std::vector<double>& shares = shares_[at];
        shares.assign(step.inputs.size(), 0.0);
        if (step.machine.has_value()) {
            const MachineState& machine = machines_[at];
            if (machine.move.has_value()) {
                const CompiledGraph::Move& move = step.machine->moves[*machine.move];
                const double kBlended = blended(move.curve, machine.elapsed / move.duration);
                shares[machine.source] = 1.0 - kBlended;
                shares[machine.current] = kBlended;
            } else {
                shares[machine.current] = 1.0;
            }
        } else {
            double total = 0.0;
            for (std::size_t input = 0; input < step.inputs.size(); ++input) {
                shares[input] = std::max(0.0, valueOf(values_, step.weights[input], step.weightParameters[input]));
                total += shares[input];
            }
            for (double& share : shares) {
                share = total > 0.0 ? share / total : 1.0 / static_cast<double>(step.inputs.size());
            }
        }
        for (std::size_t input = 0; input < step.inputs.size(); ++input) {
            weights_[step.inputs[input]] += weights_[at] * shares[input];
        }
    }
}

std::optional<std::size_t> CompiledGraph::step(std::uint64_t node) const noexcept {
    const auto kFound = std::ranges::find(steps_, node, &Step::node);
    if (kFound == steps_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(kFound - steps_.begin());
}

bool GraphInstance::request(std::uint64_t event) {
    const std::size_t kLimit = graph_->limits().maximumRequests;
    if (kLimit == 0) {
        return false;
    }
    const bool kKept = requests_.size() < kLimit;
    if (!kKept) {
        requests_.erase(requests_.begin());
    }
    requests_.push_back(event);
    return kKept;
}

bool GraphInstance::advance(double delta, std::vector<GraphEvent>& events) {
    const std::span<const CompiledGraph::Step> kSteps = graph_->steps();
    const EvaluationLimits& limits = graph_->limits();
    delta = std::isfinite(delta) ? delta : 0.0;
    weigh();
    // Every playhead moves, weighed or not, so a clip blended back in is
    // where its time says; only weighed ones fire.
    bool whole = true;
    std::vector<EventCrossing> crossed;
    std::vector<std::pair<std::size_t, std::uint64_t>> heard;
    for (std::size_t at = 0; at < kSteps.size(); ++at) {
        const CompiledGraph::Step& step = kSteps[at];
        if (!step.clip.has_value()) {
            continue;
        }
        const bool kFires = weights_[at] > limits.eventWeightThreshold;
        const std::size_t kRoom = kFires ? limits.maximumEvents - std::min(limits.maximumEvents, events.size()) : 0;
        crossed.clear();
        const Clip& clip = step.clip->clip();
        speeds_[at] = valueOf(values_, step.speed, step.speedParameter);
        const Advance kMoved = animation::advance(clip, playheads_[at], delta * speeds_[at], crossed, kRoom);
        playheads_[at] = kMoved.time;
        if (!kFires) {
            continue;
        }
        whole = whole && !kMoved.overflowed;
        for (const EventCrossing& each : crossed) {
            const ClipEvent& event = clip.events[each.event];
            events.push_back(GraphEvent{
                .event = event.event, .relevance = event.relevance, .reverse = each.reverse, .weight = weights_[at]});
            heard.emplace_back(at, event.event);
        }
    }
    // Then each machine, inner ones first, moves on what this advance
    // brought: its parameters, phases, finishes, and events.
    for (std::size_t at = 0; at < kSteps.size(); ++at) {
        if (kSteps[at].machine.has_value()) {
            runMachine(at, delta, heard);
        }
    }
    requests_.clear();
    weigh();
    return whole;
}

void GraphInstance::runMachine(std::size_t step,
                               double delta,
                               std::span<const std::pair<std::size_t, std::uint64_t>> heard) {
    const CompiledGraph::Machine& machine = *graph_->steps()[step].machine;
    MachineState& state = machines_[step];
    for (std::size_t each = 0; each < state.sinceLeft.size(); ++each) {
        if (each != state.current) {
            state.sinceLeft[each] += std::abs(delta);
        }
    }
    if (state.move.has_value()) {
        state.elapsed += std::abs(delta);
        if (state.elapsed >= machine.moves[*state.move].duration) {
            state.move.reset();
        }
    }
    for (std::size_t hop = 0; hop < graph_->limits().maximumHops; ++hop) {
        // A transition under way is replaced only as its interruption
        // policy allows.
        const CompiledGraph::Move* under = state.move.has_value() ? &machine.moves[*state.move] : nullptr;
        if (under != nullptr && under->interruption == Interruption::None) {
            return;
        }
        std::optional<std::size_t> taken;
        for (std::size_t at = 0; at < machine.moves.size(); ++at) {
            const CompiledGraph::Move& move = machine.moves[at];
            const bool kCandidate =
                (!move.from.has_value() || *move.from == state.current) && move.to != state.current &&
                state.sinceLeft[move.to] >= move.cooldown &&
                (under == nullptr || under->interruption == Interruption::ByAny || move.priority > under->priority) &&
                (!taken.has_value() || move.priority > machine.moves[*taken].priority);
            if (kCandidate && std::ranges::all_of(move.tests, [&](const CompiledGraph::Test& test) {
                    return holds(test, machine, state.current, heard);
                })) {
                taken = at;
            }
        }
        if (!taken.has_value()) {
            return;
        }
        const CompiledGraph::Move& move = machine.moves[*taken];
        state.source = state.current;
        state.sinceLeft[state.source] = 0.0;
        state.current = move.to;
        if (move.resetPhase) {
            for (const std::size_t kClip : machine.clips[move.to]) {
                playheads_[kClip] = 0.0;
            }
        }
        if (move.duration > 0.0) {
            state.move = *taken;
            state.elapsed = 0.0;
            return;
        }
        // Instant: the next may follow from where this one arrived.
        state.move.reset();
    }
}

bool GraphInstance::heardIn(std::uint64_t event,
                            const CompiledGraph::Machine& machine,
                            std::size_t state,
                            std::span<const std::pair<std::size_t, std::uint64_t>> heard) const {
    return std::ranges::contains(requests_, event) ||
           std::ranges::any_of(heard, [&](const std::pair<std::size_t, std::uint64_t>& each) {
               return each.second == event && std::ranges::contains(machine.clips[state], each.first);
           });
}

std::size_t GraphInstance::leader(const CompiledGraph::Machine& machine, std::size_t state) const {
    // The clip that counts most in the state, the first of equals.
    const std::vector<std::size_t>& clips = machine.clips[state];
    std::size_t made = clips.front();
    for (const std::size_t kClip : clips) {
        if (weights_[kClip] > weights_[made]) {
            made = kClip;
        }
    }
    return made;
}

bool GraphInstance::holds(const CompiledGraph::Test& test,
                          const CompiledGraph::Machine& machine,
                          std::size_t state,
                          std::span<const std::pair<std::size_t, std::uint64_t>> heard) const {
    if (const auto* kCompared = std::get_if<ParameterCondition>(&test.condition)) {
        return compared(kCompared->comparison, values_[test.parameter.value][0], kCompared->value);
    }
    if (const auto* kEvent = std::get_if<EventCondition>(&test.condition)) {
        return heardIn(kEvent->event, machine, state, heard);
    }
    if (machine.clips[state].empty()) {
        // A state with no clip has no phase to reach and nothing to finish.
        return false;
    }
    const std::size_t kLeader = leader(machine, state);
    const Clip& clip = graph_->steps()[kLeader].clip->clip();
    if (const auto* kPhase = std::get_if<PhaseCondition>(&test.condition)) {
        return playheads_[kLeader] / clip.duration >= kPhase->phase;
    }
    // Finished: a clip that does not loop, at the end it plays towards.
    return graph_->steps()[kLeader].loop == Loop::Clamp &&
           (speeds_[kLeader] >= 0.0 ? playheads_[kLeader] >= clip.duration : playheads_[kLeader] <= 0.0);
}

void PoseEvaluator::evaluate(const GraphInstance& instance, Pose& pose) {
    const CompiledGraph& graph = instance.graph();
    const std::span<const CompiledGraph::Step> kSteps = graph.steps();
    poses_.resize(kSteps.size());
    for (std::size_t at = 0; at < kSteps.size(); ++at) {
        const CompiledGraph::Step& step = kSteps[at];
        Pose& made = poses_[at];
        made = graph.bindPose();
        if (instance.weights_[at] == 0.0) {
            continue;
        }
        if (step.clip.has_value()) {
            step.clip->sample(instance.playheads_[at], made);
            continue;
        }
        // Weighted sums; each rotation first turned into the hemisphere of
        // the first, as q and -q are one rotation, then made unit.
        const std::vector<double>& shares = instance.shares_[at];
        for (std::size_t bone = 0; bone < made.bones.size(); ++bone) {
            Transform sum{.translation = {}, .rotation = {0.0, 0.0, 0.0, 0.0}, .scale = {0.0, 0.0, 0.0}};
            const std::array<double, 4>* first = nullptr;
            for (std::size_t input = 0; input < step.inputs.size(); ++input) {
                const double kShare = shares[input];
                if (kShare == 0.0) {
                    continue;
                }
                const Transform& from = poses_[step.inputs[input]].bones[bone];
                if (first == nullptr) {
                    first = &from.rotation;
                }
                const double kDot = (from.rotation[0] * (*first)[0]) + (from.rotation[1] * (*first)[1]) +
                                    (from.rotation[2] * (*first)[2]) + (from.rotation[3] * (*first)[3]);
                const double kSign = kDot < 0.0 ? -kShare : kShare;
                for (std::size_t each = 0; each < 3; ++each) {
                    sum.translation[each] += kShare * from.translation[each];
                    sum.scale[each] += kShare * from.scale[each];
                }
                for (std::size_t each = 0; each < 4; ++each) {
                    sum.rotation[each] += kSign * from.rotation[each];
                }
            }
            sum.rotation = normalized(sum.rotation);
            made.bones[bone] = sum;
        }
    }
    pose = poses_.back();
}

} // namespace rawframe::animation
