#include "rawframe/animation/graph.h"

#include "graph_parts.h"
#include "rawframe/animation/errors.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace rawframe::animation {

using document::Value;

std::unexpected<result::Error> graphInvalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::GraphInvalid), why);
}

std::unexpected<result::Error> graphOverLimit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kAnimationDomain, code(AnimationError::OverLimit), why);
}

const Parameter* parameterOf(const Graph& graph, std::uint64_t id) {
    const auto kFound = std::ranges::find(graph.parameters, id, &Parameter::id);
    return kFound == graph.parameters.end() ? nullptr : &*kFound;
}

Value hexValue(std::uint64_t id) {
    return Value::string(hexOf(id));
}

Value connectionValue(const Connection& connection) {
    Value made = Value::object();
    made.add("node", hexValue(connection.node));
    made.add("output", Value::string(connection.output));
    return made;
}

std::optional<Connection> connectionOf(const Value& value) {
    const std::optional<std::uint64_t> kNode =
        hasMembers(value, {"node", "output"}) ? bits64Of(value.find("node")) : std::nullopt;
    if (!kNode.has_value() || value.find("output")->kind() != Value::Kind::String) {
        return std::nullopt;
    }
    return Connection{.node = *kNode, .output = *value.find("output")->text()};
}

bool scalarInForm(const Graph& graph, const Scalar& scalar, bool negative) {
    if (const auto* kLiteral = std::get_if<double>(&scalar)) {
        return std::isfinite(*kLiteral) && (negative || *kLiteral >= 0.0);
    }
    const Parameter* kParameter = parameterOf(graph, std::get<ParameterRef>(scalar).parameter);
    return kParameter != nullptr && kParameter->type == ParameterType::Float;
}

Value scalarValue(const Scalar& scalar) {
    if (const auto* kLiteral = std::get_if<double>(&scalar)) {
        return Value::real(*kLiteral);
    }
    Value made = Value::object();
    made.add("parameter", hexValue(std::get<ParameterRef>(scalar).parameter));
    return made;
}

std::optional<Scalar> scalarOf(const Value& value) {
    if (value.kind() == Value::Kind::Number) {
        const std::optional<double> kNumber = numberOf(&value);
        return kNumber.has_value() ? std::optional<Scalar>{*kNumber} : std::nullopt;
    }
    const std::optional<std::uint64_t> kId =
        hasMembers(value, {"parameter"}) ? bits64Of(value.find("parameter")) : std::nullopt;
    return kId.has_value() ? std::optional<Scalar>{ParameterRef{*kId}} : std::nullopt;
}

namespace {

constexpr std::string_view kClipType = "rawframe/clip@1";
constexpr std::string_view kBlendType = "rawframe/blend@1";
constexpr std::string_view kStateMachineType = "rawframe/state_machine@1";
constexpr std::string_view kOutputType = "rawframe/output@1";
constexpr std::string_view kMaskType = "rawframe/mask@1";
constexpr std::string_view kLineSpaceType = "rawframe/blend_space_1d@1";
constexpr std::string_view kPlaneSpaceType = "rawframe/blend_space_2d@1";

/// Every type this engine knows; a node of any other is quarantined.
constexpr std::array<std::string_view, 7> kKnownTypes{
    kClipType, kBlendType, kStateMachineType, kOutputType, kMaskType, kLineSpaceType, kPlaneSpaceType};

constexpr std::array<std::string_view, 4> kTypes{"bool", "int", "float", "vec2"};
constexpr std::array<std::string_view, 3> kReplications{"server_authoritative", "client_predicted", "local"};
constexpr std::array<std::string_view, 2> kLoops{"clamp", "loop"};

/// SPEC-0028's `<namespace>/<name>@<major>[.<minor>]`, the namespace
/// `rawframe` or a package's `publisher/package`.
bool typeIdInForm(std::string_view type) {
    const std::size_t kAt = type.find('@');
    if (kAt == std::string_view::npos) {
        return false;
    }
    const std::string_view kPath = type.substr(0, kAt);
    const std::string_view kVersion = type.substr(kAt + 1);
    std::size_t segments = 0;
    for (std::size_t start = 0; start <= kPath.size(); ++segments) {
        const std::size_t kEnd = std::min(kPath.find('/', start), kPath.size());
        if (!machineName(kPath.substr(start, kEnd - start))) {
            return false;
        }
        start = kEnd + 1;
    }
    const auto kNumber = [](std::string_view digits) {
        return !digits.empty() && digits.size() <= 9 && (digits == "0" || digits.front() != '0') &&
               std::ranges::all_of(digits, [](char each) {
                   return each >= '0' && each <= '9';
               });
    };
    const std::size_t kDot = kVersion.find('.');
    const bool kVersioned = kDot == std::string_view::npos
                                ? kNumber(kVersion)
                                : kNumber(kVersion.substr(0, kDot)) && kNumber(kVersion.substr(kDot + 1));
    return (segments == 2 || segments == 3) && kVersioned;
}

const GraphNode* nodeOf(const Graph& graph, std::uint64_t id) {
    const auto kFound = std::ranges::lower_bound(graph.nodes, id, {}, &GraphNode::id);
    return kFound == graph.nodes.end() || kFound->id != id ? nullptr : &*kFound;
}

/// To a node there, by an output it has.
bool connectionInForm(const Graph& graph, const Connection& connection) {
    const GraphNode* kNode = nodeOf(graph, connection.node);
    if (kNode == nullptr) {
        return false;
    }
    // A quarantined node's outputs are unknown until its type is.
    return std::holds_alternative<QuarantinedNode>(kNode->node) ? machineName(connection.output)
                                                                : connection.output == "pose";
}

std::vector<const Connection*> connectionsOf(const GraphNode& node) {
    std::vector<const Connection*> made;
    if (const auto* kBlend = std::get_if<BlendNode>(&node.node)) {
        for (const BlendInput& input : kBlend->inputs) {
            made.push_back(&input.from);
        }
    } else if (const auto* kMachine = std::get_if<StateMachineNode>(&node.node)) {
        for (const State& state : kMachine->states) {
            made.push_back(&state.from);
        }
    } else if (const auto* kOutput = std::get_if<OutputNode>(&node.node)) {
        made.push_back(&kOutput->pose);
    } else if (const auto* kMask = std::get_if<MaskNode>(&node.node)) {
        made.push_back(&kMask->inside);
        made.push_back(&kMask->outside);
    } else if (const auto* kLine = std::get_if<BlendSpace1DNode>(&node.node)) {
        for (const BlendSpacePoint& point : kLine->points) {
            made.push_back(&point.from);
        }
    } else if (const auto* kPlane = std::get_if<BlendSpace2DNode>(&node.node)) {
        for (const BlendSpacePoint& point : kPlane->points) {
            made.push_back(&point.from);
        }
    }
    return made;
}

result::Status parametersInForm(const Graph& graph) {
    std::vector<std::uint64_t> ids;
    for (std::size_t at = 0; at < graph.parameters.size(); ++at) {
        const Parameter& parameter = graph.parameters[at];
        if (!machineName(parameter.name) || (at > 0 && !(graph.parameters[at - 1].name < parameter.name)) ||
            parameter.id == 0) {
            return graphInvalid("a graph's parameters have an identity and a machine name, once each, in name order");
        }
        ids.push_back(parameter.id);
        const double kInitial = parameter.initial[0];
        const bool kRanged = parameter.type == ParameterType::Int || parameter.type == ParameterType::Float;
        bool inForm = finite(parameter.initial) && (kRanged || (!parameter.minimum && !parameter.maximum));
        switch (parameter.type) {
        case ParameterType::Bool:
            inForm = inForm && (kInitial == 0.0 || kInitial == 1.0) && parameter.initial[1] == 0.0;
            break;
        case ParameterType::Int:
            for (const std::optional<double>& bound : {std::optional{kInitial}, parameter.minimum, parameter.maximum}) {
                inForm = inForm && (!bound || (*bound == std::trunc(*bound) && std::abs(*bound) < kIntLimit));
            }
            [[fallthrough]];
        case ParameterType::Float:
            inForm = inForm && parameter.initial[1] == 0.0 &&
                     (!parameter.minimum || (std::isfinite(*parameter.minimum) && *parameter.minimum <= kInitial)) &&
                     (!parameter.maximum || (std::isfinite(*parameter.maximum) && kInitial <= *parameter.maximum));
            break;
        case ParameterType::Vec2:
            break;
        }
        if (!inForm) {
            return graphInvalid("a parameter's default is of its type, within its minimum and maximum, which only int "
                                "and float parameters have");
        }
    }
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end()) {
        return graphInvalid("a graph has each parameter identity once");
    }
    return {};
}

result::Status acyclic(const Graph& graph) {
    // 0 unseen, 1 on the path, 2 done; a node met again on the path closes
    // a cycle.
    std::vector<std::uint8_t> marks(graph.nodes.size(), 0);
    std::vector<std::pair<std::size_t, std::size_t>> path;
    for (std::size_t root = 0; root < graph.nodes.size(); ++root) {
        if (marks[root] != 0) {
            continue;
        }
        path.emplace_back(root, 0);
        marks[root] = 1;
        while (!path.empty()) {
            auto& [at, next] = path.back();
            const std::vector<const Connection*> kFrom = connectionsOf(graph.nodes[at]);
            if (next == kFrom.size()) {
                marks[at] = 2;
                path.pop_back();
                continue;
            }
            const auto kTo = static_cast<std::size_t>(nodeOf(graph, kFrom[next++]->node) - graph.nodes.data());
            if (marks[kTo] == 1) {
                return graphInvalid("a graph has no cycle");
            }
            if (marks[kTo] == 0) {
                marks[kTo] = 1;
                path.emplace_back(kTo, 0);
            }
        }
    }
    return {};
}

Value parameterValue(const Parameter& parameter) {
    Value made = Value::object();
    made.add("parameter", hexValue(parameter.id));
    made.add("type", Value::string(std::string{kTypes[static_cast<std::size_t>(parameter.type)]}));
    switch (parameter.type) {
    case ParameterType::Bool:
        made.add("default", Value::boolean(parameter.initial[0] != 0.0));
        break;
    case ParameterType::Int:
        made.add("default", Value::integer(static_cast<std::int64_t>(parameter.initial[0])));
        break;
    case ParameterType::Float:
        made.add("default", Value::real(parameter.initial[0]));
        break;
    case ParameterType::Vec2:
        made.add("default", arrayOf({parameter.initial[0], parameter.initial[1], 0.0, 0.0}, 2));
        break;
    }
    const auto kBound = [&parameter](double bound) {
        return parameter.type == ParameterType::Int ? Value::integer(static_cast<std::int64_t>(bound))
                                                    : Value::real(bound);
    };
    if (parameter.minimum.has_value()) {
        made.add("minimum", kBound(*parameter.minimum));
    }
    if (parameter.maximum.has_value()) {
        made.add("maximum", kBound(*parameter.maximum));
    }
    made.add("replication", Value::string(std::string{kReplications[static_cast<std::size_t>(parameter.replication)]}));
    return made;
}

result::Result<Parameter> parameterOf(const std::string& name, const Value& value) {
    const std::optional<std::size_t> kType = placeOf(kTypes, value.find("type"));
    const std::optional<std::size_t> kReplication = placeOf(kReplications, value.find("replication"));
    const std::optional<std::uint64_t> kId = bits64Of(value.find("parameter"));
    const Value* initial = value.find("default");
    const Value* minimum = value.find("minimum");
    const Value* maximum = value.find("maximum");
    const std::size_t kMembers = 4 + (minimum != nullptr ? 1 : 0) + (maximum != nullptr ? 1 : 0);
    if (value.kind() != Value::Kind::Object || value.names().size() != kMembers || !kType.has_value() ||
        !kReplication.has_value() || !kId.has_value() || initial == nullptr) {
        return graphInvalid("a parameter is its identity, a type, a default, an optional minimum and maximum, and a "
                            "replication class");
    }
    Parameter made{.name = name,
                   .id = *kId,
                   .type = static_cast<ParameterType>(*kType),
                   .replication = static_cast<Replication>(*kReplication)};
    std::optional<ParameterValue> kInitial;
    if (made.type == ParameterType::Bool && initial->truth().has_value()) {
        kInitial = ParameterValue{*initial->truth() ? 1.0 : 0.0, 0.0};
    } else if (made.type == ParameterType::Vec2) {
        const auto kNumbers = numbersOf(initial, 2);
        if (kNumbers.has_value()) {
            kInitial = ParameterValue{(*kNumbers)[0], (*kNumbers)[1]};
        }
    } else if (made.type != ParameterType::Bool && numberOf(initial).has_value()) {
        kInitial = ParameterValue{*numberOf(initial), 0.0};
    }
    if (!kInitial.has_value() || (minimum != nullptr && !numberOf(minimum).has_value()) ||
        (maximum != nullptr && !numberOf(maximum).has_value())) {
        return graphInvalid("a parameter's default is of its type, and its bounds numbers");
    }
    made.initial = *kInitial;
    if (minimum != nullptr) {
        made.minimum = numberOf(minimum);
    }
    if (maximum != nullptr) {
        made.maximum = numberOf(maximum);
    }
    return made;
}

/// A known node's record: type, params, inputs.
Value nodeValue(const Graph& graph, const GraphNode& node) {
    Value params = Value::object();
    Value inputs = Value::object();
    std::string_view type;
    if (const auto* kClip = std::get_if<ClipNode>(&node.node)) {
        type = kClipType;
        params.add("clip", Value::string(hexOf(kClip->clip)));
        if (kClip->loop.has_value()) {
            params.add("loop", Value::string(std::string{kLoops[static_cast<std::size_t>(*kClip->loop)]}));
        }
        if (kClip->speed != Scalar{1.0}) {
            params.add("speed", scalarValue(kClip->speed));
        }
    } else if (const auto* kBlend = std::get_if<BlendNode>(&node.node)) {
        type = kBlendType;
        addPhaseSync(kBlend->phaseSync, params);
        Value weights = Value::object();
        for (const BlendInput& input : kBlend->inputs) {
            inputs.add(input.name, connectionValue(input.from));
            if (input.weight != Scalar{1.0}) {
                weights.add(input.name, scalarValue(input.weight));
            }
        }
        if (!weights.items().empty()) {
            params.add("weights", std::move(weights));
        }
    } else if (const auto* kMachine = std::get_if<StateMachineNode>(&node.node)) {
        type = kStateMachineType;
        params = stateMachineParams(graph, *kMachine);
        inputs = stateMachineInputs(*kMachine);
    } else if (const auto* kMask = std::get_if<MaskNode>(&node.node)) {
        type = kMaskType;
        params.add("mask", Value::string(hexOf(kMask->mask)));
        inputs.add("inside", connectionValue(kMask->inside));
        inputs.add("outside", connectionValue(kMask->outside));
    } else if (const auto* kLine = std::get_if<BlendSpace1DNode>(&node.node)) {
        type = kLineSpaceType;
        params = blendSpaceParams(*kLine);
        inputs = blendSpaceInputs(kLine->points);
    } else if (const auto* kPlane = std::get_if<BlendSpace2DNode>(&node.node)) {
        type = kPlaneSpaceType;
        params = blendSpaceParams(*kPlane);
        inputs = blendSpaceInputs(kPlane->points);
    } else {
        type = kOutputType;
        inputs.add("pose", connectionValue(std::get<OutputNode>(node.node).pose));
    }
    Value made = Value::object();
    made.add("type", Value::string(std::string{type}));
    made.add("params", std::move(params));
    made.add("inputs", std::move(inputs));
    return made;
}

/// A clip node's params: its clip, and optionally its loop and speed.
result::Result<ClipNode> clipNodeOf(const Value& params, const Value& inputs) {
    const Value* loop = params.find("loop");
    const Value* speed = params.find("speed");
    const std::size_t kMembers = 1 + (loop != nullptr ? 1 : 0) + (speed != nullptr ? 1 : 0);
    const std::optional<base::Bits128> kClip = bits128Of(params.find("clip"));
    const std::optional<std::size_t> kLoop = loop != nullptr ? placeOf(kLoops, loop) : std::nullopt;
    const std::optional<Scalar> kSpeed = speed != nullptr ? scalarOf(*speed) : std::nullopt;
    if (params.names().size() != kMembers || !inputs.items().empty() || !kClip.has_value() ||
        (loop != nullptr && !kLoop.has_value()) || (speed != nullptr && !kSpeed.has_value())) {
        return graphInvalid("a clip node's params are its clip, and optionally a loop and a speed; it has no inputs");
    }
    ClipNode made{.clip = *kClip, .speed = kSpeed.value_or(Scalar{1.0}), .loop = std::nullopt};
    if (kLoop.has_value()) {
        made.loop = static_cast<Loop>(*kLoop);
    }
    return made;
}

result::Result<BlendNode> blendNodeOf(const Value& params, const Value& inputs) {
    const Value* weights = params.find("weights");
    RAWFRAME_TRY_ASSIGN(const auto kSync, phaseSyncOf(params));
    if (params.names().size() != (weights != nullptr ? 1U : 0U) + kSync.second ||
        (weights != nullptr && weights->kind() != Value::Kind::Object)) {
        return graphInvalid("a blend node's params are its weights and its phase sync");
    }
    BlendNode made;
    made.phaseSync = kSync.first;
    for (std::size_t at = 0; at < inputs.names().size(); ++at) {
        const std::optional<Connection> kFrom = connectionOf(inputs.items()[at]);
        if (!kFrom.has_value()) {
            return graphInvalid("a blend node's inputs are connections");
        }
        made.inputs.push_back(BlendInput{.name = inputs.names()[at], .from = *kFrom, .weight = 1.0});
    }
    for (std::size_t at = 0; weights != nullptr && at < weights->names().size(); ++at) {
        const auto kInput = std::ranges::find(made.inputs, weights->names()[at], &BlendInput::name);
        const std::optional<Scalar> kWeight = scalarOf(weights->items()[at]);
        if (kInput == made.inputs.end() || !kWeight.has_value()) {
            return graphInvalid("a blend node weighs its own inputs, by number or parameter");
        }
        kInput->weight = *kWeight;
    }
    return made;
}

result::Result<GraphNode> nodeOf(std::uint64_t id, const Value& record) {
    const Value* type = record.find("type");
    if (type == nullptr || type->kind() != Value::Kind::String || !typeIdInForm(*type->text())) {
        return graphInvalid("a node's type is namespace/name@version");
    }
    const std::string_view kType = *type->text();
    if (!std::ranges::contains(kKnownTypes, kType)) {
        // Kept whole and never read further.
        return GraphNode{.id = id,
                         .node = QuarantinedNode{.type = std::string{kType}, .record = document::writeCompact(record)}};
    }
    if (!hasMembers(record, {"type", "params", "inputs"}) || record.find("params")->kind() != Value::Kind::Object ||
        record.find("inputs")->kind() != Value::Kind::Object) {
        return graphInvalid("a node is its type, its params, and its inputs");
    }
    const Value& params = *record.find("params");
    const Value& inputs = *record.find("inputs");
    if (kType == kClipType) {
        RAWFRAME_TRY_ASSIGN(ClipNode made, clipNodeOf(params, inputs));
        return GraphNode{.id = id, .node = made};
    }
    if (kType == kBlendType) {
        RAWFRAME_TRY_ASSIGN(BlendNode made, blendNodeOf(params, inputs));
        return GraphNode{.id = id, .node = std::move(made)};
    }
    if (kType == kStateMachineType) {
        RAWFRAME_TRY_ASSIGN(StateMachineNode made, stateMachineOf(params, inputs));
        return GraphNode{.id = id, .node = std::move(made)};
    }
    if (kType == kLineSpaceType) {
        RAWFRAME_TRY_ASSIGN(BlendSpace1DNode made, blendSpace1DOf(params, inputs));
        return GraphNode{.id = id, .node = std::move(made)};
    }
    if (kType == kPlaneSpaceType) {
        RAWFRAME_TRY_ASSIGN(BlendSpace2DNode made, blendSpace2DOf(params, inputs));
        return GraphNode{.id = id, .node = std::move(made)};
    }
    if (kType == kMaskType) {
        const std::optional<base::Bits128> kMask =
            hasMembers(params, {"mask"}) ? bits128Of(params.find("mask")) : std::nullopt;
        const std::optional<Connection> kInside =
            hasMembers(inputs, {"inside", "outside"}) ? connectionOf(*inputs.find("inside")) : std::nullopt;
        const std::optional<Connection> kOutside =
            kInside.has_value() ? connectionOf(*inputs.find("outside")) : std::nullopt;
        if (!kMask.has_value() || !kOutside.has_value()) {
            return graphInvalid("a mask node's one param is its mask, and its inputs are inside and outside");
        }
        return GraphNode{.id = id, .node = MaskNode{.mask = *kMask, .inside = *kInside, .outside = *kOutside}};
    }
    const std::optional<Connection> kPose =
        hasMembers(inputs, {"pose"}) ? connectionOf(*inputs.find("pose")) : std::nullopt;
    if (!params.items().empty() || !kPose.has_value()) {
        return graphInvalid("an output node has no params and one input, its pose");
    }
    return GraphNode{.id = id, .node = OutputNode{.pose = *kPose}};
}

Value interfaceValue(const Graph& graph) {
    Value parameters = Value::object();
    for (const Parameter& parameter : graph.parameters) {
        parameters.add(parameter.name, parameterValue(parameter));
    }
    Value made = Value::object();
    made.add("parameters", std::move(parameters));
    return made;
}

std::string hexOf(const base::Sha256Digest& digest) {
    std::string made;
    for (const std::byte kByte : digest) {
        made.push_back("0123456789abcdef"[std::to_integer<std::size_t>(kByte) >> 4U]);
        made.push_back("0123456789abcdef"[std::to_integer<std::size_t>(kByte) & 0xFU]);
    }
    return made;
}

} // namespace

result::Status validate(const Graph& graph, const GraphLimits& limits) {
    if (graph.nodes.size() > limits.maximumNodes || graph.parameters.size() > limits.maximumParameters) {
        return graphOverLimit("a graph has more nodes or parameters than its limits");
    }
    RAWFRAME_TRY(parametersInForm(graph));
    std::size_t outputs = 0;
    for (std::size_t at = 0; at < graph.nodes.size(); ++at) {
        const GraphNode& node = graph.nodes[at];
        if (node.id == 0 || (at > 0 && !(graph.nodes[at - 1].id < node.id))) {
            return graphInvalid("a graph's nodes have an id each, once, in order");
        }
        if (const auto* kClip = std::get_if<ClipNode>(&node.node)) {
            if (kClip->clip == base::Bits128{} || !scalarInForm(graph, kClip->speed, true)) {
                return graphInvalid("a clip node names its clip, and its speed is a number or a float parameter");
            }
        } else if (const auto* kBlend = std::get_if<BlendNode>(&node.node)) {
            if (kBlend->inputs.empty() || kBlend->inputs.size() > limits.maximumInputs) {
                return kBlend->inputs.empty() ? graphInvalid("a blend node has an input")
                                              : graphOverLimit("a blend node has more inputs than its limit");
            }
            for (std::size_t input = 0; input < kBlend->inputs.size(); ++input) {
                const BlendInput& each = kBlend->inputs[input];
                if (!machineName(each.name) || (input > 0 && !(kBlend->inputs[input - 1].name < each.name)) ||
                    !scalarInForm(graph, each.weight, false)) {
                    return graphInvalid(
                        "a blend node's inputs are machine names in order, weighed by a number of nought "
                        "or more or by a float parameter");
                }
            }
            std::vector<std::string_view> names;
            for (const BlendInput& input : kBlend->inputs) {
                names.emplace_back(input.name);
            }
            RAWFRAME_TRY(phaseSyncInForm(kBlend->phaseSync, names));
        } else if (const auto* kMachine = std::get_if<StateMachineNode>(&node.node)) {
            RAWFRAME_TRY(stateMachineInForm(graph, *kMachine, limits));
        } else if (const auto* kMask = std::get_if<MaskNode>(&node.node)) {
            if (kMask->mask == base::Bits128{}) {
                return graphInvalid("a mask node names its mask");
            }
        } else if (const auto* kLine = std::get_if<BlendSpace1DNode>(&node.node)) {
            RAWFRAME_TRY(blendSpaceInForm(graph, *kLine, limits));
        } else if (const auto* kPlane = std::get_if<BlendSpace2DNode>(&node.node)) {
            RAWFRAME_TRY(blendSpaceInForm(graph, *kPlane, limits));
        } else if (const auto* kQuarantined = std::get_if<QuarantinedNode>(&node.node)) {
            const auto kRecord = document::parse(kQuarantined->record);
            const Value* type = kRecord.has_value() ? kRecord->find("type") : nullptr;
            if (type == nullptr || type->text() == nullptr || *type->text() != kQuarantined->type ||
                !typeIdInForm(kQuarantined->type) || std::ranges::contains(kKnownTypes, kQuarantined->type)) {
                return graphInvalid("a quarantined node is a record of a type this engine does not know");
            }
        } else {
            ++outputs;
        }
        for (const Connection* kFrom : connectionsOf(node)) {
            if (!connectionInForm(graph, *kFrom)) {
                return graphInvalid("a connection names a node of the graph and an output it has");
            }
        }
    }
    if (outputs != 1) {
        return graphInvalid("a graph has one output node");
    }
    RAWFRAME_TRY(acyclic(graph));
    for (std::size_t at = 0; at < graph.presentation.size(); ++at) {
        const auto& [kNode, kDrawing] = graph.presentation[at];
        if (nodeOf(graph, kNode) == nullptr || (at > 0 && !(graph.presentation[at - 1].first < kNode)) ||
            !document::parse(kDrawing).has_value()) {
            return graphInvalid("a graph's presentation draws its nodes, once each, in order");
        }
    }
    return {};
}

result::Result<std::string> writeGraph(const Graph& graph, const GraphLimits& limits) {
    RAWFRAME_TRY(validate(graph, limits));
    Value nodes = Value::object();
    for (const GraphNode& node : graph.nodes) {
        if (const auto* kQuarantined = std::get_if<QuarantinedNode>(&node.node)) {
            RAWFRAME_TRY_ASSIGN(Value record, document::parse(kQuarantined->record));
            nodes.add(hexOf(node.id), std::move(record));
        } else {
            nodes.add(hexOf(node.id), nodeValue(graph, node));
        }
    }
    Value made = Value::object();
    made.add("formatVersion", Value::integer(1));
    made.add("kind", Value::string("animation.graph"));
    made.add("interface", interfaceValue(graph));
    made.add("graph", std::move(nodes));
    if (!graph.presentation.empty()) {
        Value presentation = Value::object();
        for (const auto& [kNode, kDrawing] : graph.presentation) {
            RAWFRAME_TRY_ASSIGN(Value drawing, document::parse(kDrawing));
            presentation.add(hexOf(kNode), std::move(drawing));
        }
        made.add("presentation", std::move(presentation));
    }
    return document::write(made);
}

result::Result<Graph> readGraph(std::string_view text, const GraphLimits& limits) {
    auto parsed = document::parse(text);
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value* kind = parsed->find("kind");
    const bool kDrawn = parsed->find("presentation") != nullptr;
    const bool kShape = kDrawn ? hasMembers(*parsed, {"formatVersion", "kind", "interface", "graph", "presentation"})
                               : hasMembers(*parsed, {"formatVersion", "kind", "interface", "graph"});
    if (!kShape || parsed->find("formatVersion")->integer() != 1 || kind->text() == nullptr ||
        *kind->text() != "animation.graph" || !hasMembers(*parsed->find("interface"), {"parameters"}) ||
        parsed->find("interface")->find("parameters")->kind() != Value::Kind::Object ||
        parsed->find("graph")->kind() != Value::Kind::Object ||
        (kDrawn && parsed->find("presentation")->kind() != Value::Kind::Object)) {
        return graphInvalid("a graph is format version 1, kind animation.graph, an interface of parameters, its nodes, "
                            "and an optional presentation");
    }
    const Value& parameters = *parsed->find("interface")->find("parameters");
    const Value& nodes = *parsed->find("graph");
    if (parameters.names().size() > limits.maximumParameters || nodes.names().size() > limits.maximumNodes) {
        return graphOverLimit("a graph has more nodes or parameters than its limits");
    }
    Graph graph;
    for (std::size_t at = 0; at < parameters.names().size(); ++at) {
        RAWFRAME_TRY_ASSIGN(Parameter made, parameterOf(parameters.names()[at], parameters.items()[at]));
        graph.parameters.push_back(std::move(made));
    }
    for (std::size_t at = 0; at < nodes.names().size(); ++at) {
        const Value kName = Value::string(nodes.names()[at]);
        const std::optional<std::uint64_t> kNodeId = bits64Of(&kName);
        if (!kNodeId.has_value() || nodes.items()[at].kind() != Value::Kind::Object) {
            return graphInvalid("a graph's nodes are keyed by 16 hex digits");
        }
        RAWFRAME_TRY_ASSIGN(GraphNode made, nodeOf(*kNodeId, nodes.items()[at]));
        graph.nodes.push_back(std::move(made));
    }
    if (kDrawn) {
        const Value& presentation = *parsed->find("presentation");
        for (std::size_t at = 0; at < presentation.names().size(); ++at) {
            const Value kName = Value::string(presentation.names()[at]);
            const std::optional<std::uint64_t> kNode = bits64Of(&kName);
            if (!kNode.has_value()) {
                return graphInvalid("a graph's presentation is keyed by its nodes");
            }
            graph.presentation.emplace_back(*kNode, document::writeCompact(presentation.items()[at]));
        }
    }
    // What the writer makes of it is the text, byte for byte, or the text
    // was not in the one form.
    RAWFRAME_TRY_ASSIGN(const std::string kWritten, writeGraph(graph, limits));
    if (kWritten != text) {
        return graphInvalid("a graph is not in its canonical form");
    }
    return graph;
}

std::vector<base::Bits128> clipsOf(const Graph& graph) {
    std::vector<base::Bits128> clips;
    for (const GraphNode& node : graph.nodes) {
        if (const auto* kClip = std::get_if<ClipNode>(&node.node)) {
            clips.push_back(kClip->clip);
        }
    }
    std::ranges::sort(clips);
    const auto kRepeated = std::ranges::unique(clips);
    clips.erase(kRepeated.begin(), kRepeated.end());
    return clips;
}

std::vector<base::Bits128> masksOf(const Graph& graph) {
    std::vector<base::Bits128> masks;
    for (const GraphNode& node : graph.nodes) {
        if (const auto* kMask = std::get_if<MaskNode>(&node.node)) {
            masks.push_back(kMask->mask);
        }
    }
    std::ranges::sort(masks);
    const auto kRepeated = std::ranges::unique(masks);
    masks.erase(kRepeated.begin(), kRepeated.end());
    return masks;
}

result::Result<base::Sha256Digest> semanticHash(const Graph& graph) {
    RAWFRAME_TRY(validate(graph));
    if (std::ranges::any_of(graph.nodes, [](const GraphNode& node) {
            return std::holds_alternative<QuarantinedNode>(node.node);
        })) {
        return graphInvalid("a graph with a quarantined node has no semantic hash");
    }
    // Bottom up: a node's digest covers its type, params, and inputs, each
    // input by the digest of the node it comes from rather than that
    // node's id.
    std::map<std::uint64_t, base::Sha256Digest> digests;
    const auto kDigestOf = [&graph, &digests](std::uint64_t root) {
        std::vector<std::pair<std::uint64_t, bool>> pending{{root, false}};
        while (!pending.empty()) {
            const auto [kId, kExpanded] = pending.back();
            pending.pop_back();
            if (digests.contains(kId)) {
                continue;
            }
            const GraphNode& node = *nodeOf(graph, kId);
            if (!kExpanded) {
                pending.emplace_back(kId, true);
                for (const Connection* kFrom : connectionsOf(node)) {
                    pending.emplace_back(kFrom->node, false);
                }
                continue;
            }
            Value record = nodeValue(graph, node);
            Value inputs = Value::object();
            const Value& written = *record.find("inputs");
            for (std::size_t at = 0; at < written.names().size(); ++at) {
                const Connection kFrom = *connectionOf(written.items()[at]);
                Value input = Value::object();
                input.add("node_digest", Value::string(hexOf(digests.at(kFrom.node))));
                input.add("output", Value::string(kFrom.output));
                inputs.add(written.names()[at], std::move(input));
            }
            Value hashed = Value::object();
            hashed.add("type", *record.find("type"));
            hashed.add("params", *record.find("params"));
            hashed.add("inputs", std::move(inputs));
            digests.emplace(kId, base::sha256(document::writeCompact(hashed)));
        }
        return digests.at(root);
    };
    const auto kOutput = std::ranges::find_if(graph.nodes, [](const GraphNode& node) {
        return std::holds_alternative<OutputNode>(node.node);
    });
    Value outputs = Value::object();
    outputs.add("pose", Value::string(hexOf(kDigestOf(kOutput->id))));
    Value hashed = Value::object();
    hashed.add("formatVersion", Value::integer(1));
    hashed.add("kind", Value::string("animation.graph"));
    hashed.add("interface", interfaceValue(graph));
    hashed.add("outputs", std::move(outputs));
    return base::sha256(document::writeCompact(hashed));
}

} // namespace rawframe::animation
