// A graph's modifier stages (SPEC-0035, D138) as its document holds them.

#include "graph_parts.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rawframe::animation {

namespace {

using document::Value;

constexpr std::string_view kTwoBoneIkType = "rawframe/two_bone_ik@1";
constexpr std::string_view kLookAtType = "rawframe/look_at@1";
constexpr std::array<std::string_view, 2> kRelevances{"presentation", "simulation"};
constexpr std::array<double, 3> kForward{0.0, 0.0, 1.0};

Value tripleValue(const Triple& triple) {
    Value made = Value::array();
    for (const Scalar& each : triple) {
        made.push(scalarValue(each));
    }
    return made;
}

std::optional<Triple> tripleOf(const Value* value) {
    if (value == nullptr || value->kind() != Value::Kind::Array || value->items().size() != 3) {
        return std::nullopt;
    }
    Triple made{0.0, 0.0, 0.0};
    for (std::size_t at = 0; at < 3; ++at) {
        const std::optional<Scalar> kEach = scalarOf(value->items()[at]);
        if (!kEach.has_value()) {
            return std::nullopt;
        }
        made[at] = *kEach;
    }
    return made;
}

Value modifierValue(const Modifier& modifier) {
    Value params = Value::object();
    std::string_view type;
    if (const auto* kIk = std::get_if<TwoBoneIk>(&modifier.stage)) {
        type = kTwoBoneIkType;
        params.add("goal", tripleValue(kIk->goal));
        if (kIk->pole.has_value()) {
            params.add("pole", tripleValue(*kIk->pole));
        }
    } else {
        const auto& look = std::get<LookAt>(modifier.stage);
        type = kLookAtType;
        if (look.axis != kForward) {
            params.add("axis", arrayOf({look.axis[0], look.axis[1], look.axis[2], 0.0}, 3));
        }
        params.add("bone", Value::string(hexOf(look.bone)));
        params.add("goal", tripleValue(look.goal));
    }
    if (modifier.relevance != Relevance::Presentation) {
        params.add("relevance", Value::string(std::string{kRelevances[static_cast<std::size_t>(modifier.relevance)]}));
    }
    if (const auto* kIk = std::get_if<TwoBoneIk>(&modifier.stage)) {
        params.add("tip", Value::string(hexOf(kIk->tip)));
    }
    if (modifier.weight != Scalar{1.0}) {
        params.add("weight", scalarValue(modifier.weight));
    }
    Value made = Value::object();
    made.add("type", Value::string(std::string{type}));
    made.add("params", std::move(params));
    return made;
}

result::Result<Modifier> modifierOf(const Value& value) {
    const Value* type = value.find("type");
    const Value* params = value.find("params");
    if (!hasMembers(value, {"type", "params"}) || type->kind() != Value::Kind::String ||
        params->kind() != Value::Kind::Object) {
        return graphInvalid("a modifier stage is its type and its params");
    }
    Modifier made{.stage = TwoBoneIk{}, .weight = 1.0, .relevance = Relevance::Presentation};
    std::size_t members = 0;
    if (const Value* weight = params->find("weight")) {
        const std::optional<Scalar> kWeight = scalarOf(*weight);
        if (!kWeight.has_value()) {
            return graphInvalid("a stage's weight is a number or a float parameter");
        }
        made.weight = *kWeight;
        ++members;
    }
    if (const Value* relevance = params->find("relevance")) {
        const std::optional<std::size_t> kRelevance = placeOf(kRelevances, relevance);
        if (!kRelevance.has_value()) {
            return graphInvalid("a stage's relevance is presentation or simulation");
        }
        made.relevance = static_cast<Relevance>(*kRelevance);
        ++members;
    }
    const std::optional<Triple> kGoal = tripleOf(params->find("goal"));
    if (*type->text() == kTwoBoneIkType) {
        TwoBoneIk ik;
        const std::optional<base::Bits128> kTip = bits128Of(params->find("tip"));
        const Value* pole = params->find("pole");
        if (!kGoal.has_value() || !kTip.has_value() || (pole != nullptr && !tripleOf(pole).has_value())) {
            return graphInvalid("a two-bone IK stage is its tip, a goal of three numbers, and optionally a pole");
        }
        ik.tip = *kTip;
        ik.goal = *kGoal;
        if (pole != nullptr) {
            ik.pole = tripleOf(pole);
        }
        members += 2 + (pole != nullptr ? 1U : 0U);
        made.stage = std::move(ik);
    } else if (*type->text() == kLookAtType) {
        LookAt look;
        const std::optional<base::Bits128> kBone = bits128Of(params->find("bone"));
        const Value* axis = params->find("axis");
        const auto kAxis = axis != nullptr ? numbersOf(axis, 3) : std::optional<std::array<double, 4>>{};
        if (!kGoal.has_value() || !kBone.has_value() || (axis != nullptr && !kAxis.has_value())) {
            return graphInvalid("a look-at stage is its bone, a goal of three numbers, and optionally an axis");
        }
        look.bone = *kBone;
        look.goal = *kGoal;
        if (kAxis.has_value()) {
            look.axis = {(*kAxis)[0], (*kAxis)[1], (*kAxis)[2]};
        }
        members += 2 + (axis != nullptr ? 1U : 0U);
        made.stage = look;
    } else {
        return graphInvalid("a modifier stage is of a type this engine knows");
    }
    if (params->names().size() != members) {
        return graphInvalid("a modifier stage's params are only those of its type");
    }
    return made;
}

} // namespace

Value modifiersValue(std::span<const Modifier> modifiers) {
    Value made = Value::array();
    for (const Modifier& modifier : modifiers) {
        made.push(modifierValue(modifier));
    }
    return made;
}

result::Result<std::vector<Modifier>> modifiersOf(const Value& value) {
    if (value.kind() != Value::Kind::Array) {
        return graphInvalid("a graph's modifiers are a list of stages");
    }
    std::vector<Modifier> made;
    for (const Value& each : value.items()) {
        RAWFRAME_TRY_ASSIGN(Modifier modifier, modifierOf(each));
        made.push_back(std::move(modifier));
    }
    return made;
}

result::Status modifiersInForm(const Graph& graph, const GraphLimits& limits) {
    if (graph.modifiers.size() > limits.maximumModifiers) {
        return graphOverLimit("a graph has more modifier stages than its limit");
    }
    const auto kTripleInForm = [&graph](const Triple& triple) {
        return std::ranges::all_of(triple, [&graph](const Scalar& each) {
            return scalarInForm(graph, each, true);
        });
    };
    for (const Modifier& modifier : graph.modifiers) {
        if (!scalarInForm(graph, modifier.weight, false)) {
            return graphInvalid("a stage's weight is a number of nought or more or a float parameter");
        }
        if (const auto* kIk = std::get_if<TwoBoneIk>(&modifier.stage)) {
            if (kIk->tip == base::Bits128{} || !kTripleInForm(kIk->goal) ||
                (kIk->pole.has_value() && !kTripleInForm(*kIk->pole))) {
                return graphInvalid("a two-bone IK stage names its tip, and its goal and pole are numbers or float "
                                    "parameters");
            }
            continue;
        }
        const auto& look = std::get<LookAt>(modifier.stage);
        const double kLength =
            std::sqrt((look.axis[0] * look.axis[0]) + (look.axis[1] * look.axis[1]) + (look.axis[2] * look.axis[2]));
        if (look.bone == base::Bits128{} || !kTripleInForm(look.goal) || !finite(look.axis) || !(kLength > 0.0)) {
            return graphInvalid("a look-at stage names its bone, its goal is numbers or float parameters, and its "
                                "axis has a length");
        }
    }
    return {};
}

} // namespace rawframe::animation
