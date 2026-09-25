#include "rawframe/animation/instance.h"

#include "rawframe/animation/errors.h"
#include "rotation.h"

#include <algorithm>
#include <cmath>
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
        const auto* kBlend = std::get_if<BlendNode>(&node->node);
        if (kBlend != nullptr && next < kBlend->inputs.size()) {
            const std::uint64_t kInput = kBlend->inputs[next++].from.node;
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
        } else {
            for (const BlendInput& input : std::get<BlendNode>(node->node).inputs) {
                step.inputs.push_back(steps.at(input.from.node));
                step.weights.push_back(kLiteral(input.weight));
                step.weightParameters.push_back(kParameter(input.weight));
            }
        }
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
      shares_(graph_->steps().size()) {
    for (std::size_t at = 0; at < graph_->parameterCount(); ++at) {
        values_.push_back(graph_->declaration(ParameterIndex{static_cast<std::uint32_t>(at)}).initial);
    }
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

bool GraphInstance::advance(double delta, std::vector<GraphEvent>& events) {
    const std::span<const CompiledGraph::Step> kSteps = graph_->steps();
    const EvaluationLimits& limits = graph_->limits();
    // Weights from the output down: a blend shares its own among its
    // inputs as their weights say, evenly when none weighs anything.
    std::ranges::fill(weights_, 0.0);
    weights_.back() = 1.0;
    for (std::size_t at = kSteps.size(); at-- > 0;) {
        const CompiledGraph::Step& step = kSteps[at];
        if (step.clip.has_value()) {
            continue;
        }
        std::vector<double>& shares = shares_[at];
        shares.assign(step.inputs.size(), 0.0);
        double total = 0.0;
        for (std::size_t input = 0; input < step.inputs.size(); ++input) {
            shares[input] = std::max(0.0, valueOf(values_, step.weights[input], step.weightParameters[input]));
            total += shares[input];
        }
        for (std::size_t input = 0; input < step.inputs.size(); ++input) {
            shares[input] = total > 0.0 ? shares[input] / total : 1.0 / static_cast<double>(step.inputs.size());
            weights_[step.inputs[input]] += weights_[at] * shares[input];
        }
    }
    // Every playhead moves, weighed or not, so a clip blended back in is
    // where its time says; only weighed ones fire.
    bool whole = true;
    std::vector<EventCrossing> crossed;
    for (std::size_t at = 0; at < kSteps.size(); ++at) {
        const CompiledGraph::Step& step = kSteps[at];
        if (!step.clip.has_value()) {
            continue;
        }
        const bool kFires = weights_[at] > limits.eventWeightThreshold;
        const std::size_t kRoom = kFires ? limits.maximumEvents - std::min(limits.maximumEvents, events.size()) : 0;
        crossed.clear();
        const Clip& clip = step.clip->clip();
        const Advance kMoved = animation::advance(
            clip, playheads_[at], delta * valueOf(values_, step.speed, step.speedParameter), crossed, kRoom);
        playheads_[at] = kMoved.time;
        if (!kFires) {
            continue;
        }
        whole = whole && !kMoved.overflowed;
        for (const EventCrossing& each : crossed) {
            const ClipEvent& event = clip.events[each.event];
            events.push_back(GraphEvent{
                .event = event.event, .relevance = event.relevance, .reverse = each.reverse, .weight = weights_[at]});
        }
    }
    return whole;
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
