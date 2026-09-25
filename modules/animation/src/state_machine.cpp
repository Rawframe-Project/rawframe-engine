// The `rawframe/state_machine@1` node as the graph document holds it.

#include "graph_parts.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace rawframe::animation {

namespace {

using document::Value;

constexpr std::array<std::string_view, 6> kComparisons{
    "equal", "not_equal", "less", "less_or_equal", "greater", "greater_or_equal"};
constexpr std::array<std::string_view, 2> kCurves{"linear", "cubic_in_out"};
constexpr std::array<std::string_view, 3> kInterruptions{"none", "by_higher_priority", "by_any"};

/// Transitions in order: those from any state first, then by source, then
/// by priority, highest first.
auto orderOf(const Transition& transition) {
    return std::tuple{
        transition.from.has_value(), transition.from.value_or(std::string{}), -std::int64_t{transition.priority}};
}

Value conditionValue(const Graph& graph, const Condition& condition) {
    Value made = Value::object();
    if (const auto* kParameter = std::get_if<ParameterCondition>(&condition)) {
        const Parameter* declared = parameterOf(graph, kParameter->parameter);
        made.add("parameter", hexValue(kParameter->parameter));
        made.add("comparison",
                 Value::string(std::string{kComparisons[static_cast<std::size_t>(kParameter->comparison)]}));
        made.add("value",
                 declared != nullptr && declared->type == ParameterType::Bool ? Value::boolean(kParameter->value != 0.0)
                                                                              : Value::real(kParameter->value));
    } else if (const auto* kPhase = std::get_if<PhaseCondition>(&condition)) {
        made.add("phase", Value::real(kPhase->phase));
    } else if (std::holds_alternative<FinishedCondition>(condition)) {
        made.add("finished", Value::boolean(true));
    } else {
        made.add("event", hexValue(std::get<EventCondition>(condition).event));
    }
    return made;
}

std::optional<Condition> conditionOf(const Value& value) {
    if (hasMembers(value, {"parameter", "comparison", "value"})) {
        const std::optional<std::uint64_t> kParameter = bits64Of(value.find("parameter"));
        const std::optional<std::size_t> kComparison = placeOf(kComparisons, value.find("comparison"));
        const Value& compared = *value.find("value");
        const std::optional<double> kValue =
            compared.truth().has_value() ? std::optional<double>{*compared.truth() ? 1.0 : 0.0} : numberOf(&compared);
        if (!kParameter.has_value() || !kComparison.has_value() || !kValue.has_value()) {
            return std::nullopt;
        }
        return ParameterCondition{
            .parameter = *kParameter, .comparison = static_cast<Comparison>(*kComparison), .value = *kValue};
    }
    if (hasMembers(value, {"phase"})) {
        const std::optional<double> kPhase = numberOf(value.find("phase"));
        return kPhase.has_value() ? std::optional<Condition>{PhaseCondition{*kPhase}} : std::nullopt;
    }
    if (hasMembers(value, {"finished"})) {
        return value.find("finished")->truth() == true ? std::optional<Condition>{FinishedCondition{}} : std::nullopt;
    }
    if (hasMembers(value, {"event"})) {
        const std::optional<std::uint64_t> kEvent = bits64Of(value.find("event"));
        return kEvent.has_value() ? std::optional<Condition>{EventCondition{*kEvent}} : std::nullopt;
    }
    return std::nullopt;
}

Value transitionValue(const Graph& graph, const Transition& transition) {
    Value made = Value::object();
    if (transition.from.has_value()) {
        made.add("from", Value::string(*transition.from));
    }
    made.add("to", Value::string(transition.to));
    if (transition.priority != 0) {
        made.add("priority", Value::integer(transition.priority));
    }
    if (transition.duration != 0.0) {
        made.add("duration", Value::real(transition.duration));
    }
    if (transition.curve != BlendCurve::Linear) {
        made.add("curve", Value::string(std::string{kCurves[static_cast<std::size_t>(transition.curve)]}));
    }
    if (transition.interruption != Interruption::None) {
        made.add("interruption",
                 Value::string(std::string{kInterruptions[static_cast<std::size_t>(transition.interruption)]}));
    }
    if (transition.cooldown != 0.0) {
        made.add("cooldown", Value::real(transition.cooldown));
    }
    if (transition.resetPhase) {
        made.add("resetPhase", Value::boolean(true));
    }
    Value conditions = Value::array();
    for (const Condition& condition : transition.conditions) {
        conditions.push(conditionValue(graph, condition));
    }
    made.add("conditions", std::move(conditions));
    return made;
}

result::Result<Transition> transitionOf(const Value& value) {
    if (value.kind() != Value::Kind::Object) {
        return graphInvalid("a transition is an object");
    }
    Transition made;
    std::size_t members = 2;
    const auto kText = [&value, &members](std::string_view name, std::string& into) {
        const Value* member = value.find(name);
        if (member == nullptr) {
            return true;
        }
        ++members;
        if (member->kind() != Value::Kind::String) {
            return false;
        }
        into = *member->text();
        return true;
    };
    const auto kNumber = [&value, &members](std::string_view name, double& into) {
        const Value* member = value.find(name);
        if (member == nullptr) {
            return true;
        }
        ++members;
        const std::optional<double> kRead = numberOf(member);
        into = kRead.value_or(0.0);
        return kRead.has_value();
    };
    std::string from;
    const bool kFrom = value.find("from") != nullptr;
    const Value* to = value.find("to");
    const Value* priority = value.find("priority");
    const Value* curve = value.find("curve");
    const Value* interruption = value.find("interruption");
    const Value* reset = value.find("resetPhase");
    const Value* conditions = value.find("conditions");
    const std::optional<std::int64_t> kPriority = priority != nullptr ? priority->integer() : std::optional{0L};
    const std::optional<std::size_t> kCurve = curve != nullptr ? placeOf(kCurves, curve) : std::optional{0UL};
    const std::optional<std::size_t> kInterruption =
        interruption != nullptr ? placeOf(kInterruptions, interruption) : std::optional{0UL};
    const bool kRead = kText("from", from) && to != nullptr && to->kind() == Value::Kind::String &&
                       kNumber("duration", made.duration) && kNumber("cooldown", made.cooldown) &&
                       kPriority.has_value() && *kPriority >= std::numeric_limits<std::int32_t>::min() &&
                       *kPriority <= std::numeric_limits<std::int32_t>::max() && kCurve.has_value() &&
                       kInterruption.has_value() && (reset == nullptr || reset->truth() == true) &&
                       conditions != nullptr && conditions->kind() == Value::Kind::Array;
    members += (priority != nullptr ? 1 : 0) + (curve != nullptr ? 1 : 0) + (interruption != nullptr ? 1 : 0) +
               (reset != nullptr ? 1 : 0);
    if (!kRead || value.names().size() != members) {
        return graphInvalid("a transition is an optional source, its destination, its conditions, and optionally a "
                            "priority, a duration, a curve, an interruption, a cooldown, and a phase reset");
    }
    if (kFrom) {
        made.from = from;
    }
    made.to = *to->text();
    made.priority = static_cast<std::int32_t>(*kPriority);
    made.curve = static_cast<BlendCurve>(*kCurve);
    made.interruption = static_cast<Interruption>(*kInterruption);
    made.resetPhase = reset != nullptr;
    for (const Value& each : conditions->items()) {
        const std::optional<Condition> kCondition = conditionOf(each);
        if (!kCondition.has_value()) {
            return graphInvalid("a condition compares a parameter, reaches a phase, finishes, or hears an event");
        }
        made.conditions.push_back(*kCondition);
    }
    return made;
}

bool conditionInForm(const Graph& graph, const Condition& condition) {
    if (const auto* kParameter = std::get_if<ParameterCondition>(&condition)) {
        const Parameter* declared = parameterOf(graph, kParameter->parameter);
        if (declared == nullptr || !std::isfinite(kParameter->value)) {
            return false;
        }
        switch (declared->type) {
        case ParameterType::Bool:
            return (kParameter->value == 0.0 || kParameter->value == 1.0) &&
                   (kParameter->comparison == Comparison::Equal || kParameter->comparison == Comparison::NotEqual);
        case ParameterType::Int:
            return kParameter->value == std::trunc(kParameter->value) && std::abs(kParameter->value) < kIntLimit;
        case ParameterType::Float:
            return true;
        case ParameterType::Vec2:
            return false;
        }
        return false;
    }
    if (const auto* kPhase = std::get_if<PhaseCondition>(&condition)) {
        return kPhase->phase >= 0.0 && kPhase->phase <= 1.0;
    }
    if (const auto* kEvent = std::get_if<EventCondition>(&condition)) {
        return kEvent->event != 0;
    }
    return true;
}

} // namespace

Value stateMachineParams(const Graph& graph, const StateMachineNode& node) {
    Value transitions = Value::array();
    for (const Transition& transition : node.transitions) {
        transitions.push(transitionValue(graph, transition));
    }
    Value made = Value::object();
    made.add("entry", Value::string(node.entry));
    made.add("transitions", std::move(transitions));
    return made;
}

Value stateMachineInputs(const StateMachineNode& node) {
    Value made = Value::object();
    for (const State& state : node.states) {
        made.add(state.name, connectionValue(state.from));
    }
    return made;
}

result::Result<StateMachineNode> stateMachineOf(const Value& params, const Value& inputs) {
    if (!hasMembers(params, {"entry", "transitions"}) || params.find("entry")->kind() != Value::Kind::String ||
        params.find("transitions")->kind() != Value::Kind::Array) {
        return graphInvalid("a state machine's params are its entry and its transitions");
    }
    StateMachineNode made{.states = {}, .entry = *params.find("entry")->text(), .transitions = {}};
    for (std::size_t at = 0; at < inputs.names().size(); ++at) {
        const std::optional<Connection> kFrom = connectionOf(inputs.items()[at]);
        if (!kFrom.has_value()) {
            return graphInvalid("a state machine's inputs are its states' connections");
        }
        made.states.push_back(State{.name = inputs.names()[at], .from = *kFrom});
    }
    for (const Value& each : params.find("transitions")->items()) {
        RAWFRAME_TRY_ASSIGN(Transition transition, transitionOf(each));
        made.transitions.push_back(std::move(transition));
    }
    return made;
}

result::Status stateMachineInForm(const Graph& graph, const StateMachineNode& node, const GraphLimits& limits) {
    if (node.states.size() > limits.maximumInputs || node.transitions.size() > limits.maximumTransitions) {
        return graphOverLimit("a state machine has more states or transitions than its limits");
    }
    const auto kState = [&node](std::string_view name) {
        return std::ranges::contains(node.states, name, &State::name);
    };
    for (std::size_t at = 0; at < node.states.size(); ++at) {
        if (!machineName(node.states[at].name) || (at > 0 && !(node.states[at - 1].name < node.states[at].name))) {
            return graphInvalid("a state machine's states are machine names, once each, in order");
        }
    }
    if (!kState(node.entry)) {
        return graphInvalid("a state machine enters one of its states");
    }
    for (std::size_t at = 0; at < node.transitions.size(); ++at) {
        const Transition& transition = node.transitions[at];
        if (transition.conditions.size() > limits.maximumConditions) {
            return graphOverLimit("a transition has more conditions than its limit");
        }
        if ((transition.from.has_value() && !kState(*transition.from)) || !kState(transition.to) ||
            transition.from == transition.to) {
            return graphInvalid("a transition goes from one of its machine's states, or any, to another");
        }
        if (!std::isfinite(transition.duration) || transition.duration < 0.0 || !std::isfinite(transition.cooldown) ||
            transition.cooldown < 0.0) {
            return graphInvalid("a transition's duration and cooldown are seconds, nought or more");
        }
        if (!std::ranges::all_of(transition.conditions, [&graph](const Condition& each) {
                return conditionInForm(graph, each);
            })) {
            return graphInvalid("a condition compares a declared bool, int, or float parameter with a value of its "
                                "type, reaches a phase in [0, 1], or hears an event");
        }
        if (at > 0 && !(orderOf(node.transitions[at - 1]) < orderOf(transition))) {
            return graphInvalid("a state machine's transitions are in order of source, then priority, highest "
                                "first, a priority once a source");
        }
    }
    // One always wins: a state's own transitions and those from any state
    // never share a priority.
    for (const State& state : node.states) {
        std::vector<std::int32_t> priorities;
        for (const Transition& transition : node.transitions) {
            if (!transition.from.has_value() || transition.from == state.name) {
                priorities.push_back(transition.priority);
            }
        }
        std::ranges::sort(priorities);
        if (std::ranges::adjacent_find(priorities) != priorities.end()) {
            return graphInvalid("a state's transitions and those from any state have a priority each");
        }
    }
    return {};
}

} // namespace rawframe::animation
