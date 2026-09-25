// Phase sync (SPEC-0035, D136) as the graph document holds it on a blend
// or a blend space.

#include "graph_parts.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <string>

namespace rawframe::animation {

namespace {

using document::Value;

constexpr std::array<std::string_view, 2> kLeaders{"weight_leader", "declared_leader"};

} // namespace

void addPhaseSync(const std::optional<PhaseSync>& sync, Value& params) {
    if (!sync.has_value()) {
        return;
    }
    if (sync->leader == PhaseLeader::Declared) {
        params.add("leader", Value::string(sync->input));
    }
    params.add("phaseSync", Value::string(std::string{kLeaders[static_cast<std::size_t>(sync->leader)]}));
}

result::Result<std::pair<std::optional<PhaseSync>, std::size_t>> phaseSyncOf(const Value& params) {
    const Value* sync = params.find("phaseSync");
    const Value* leader = params.find("leader");
    if (sync == nullptr) {
        if (leader != nullptr) {
            return graphInvalid("a node names a phase leader only when it declares phase sync");
        }
        return std::pair{std::optional<PhaseSync>{}, std::size_t{0}};
    }
    const std::optional<std::size_t> kLeader = placeOf(kLeaders, sync);
    const bool kDeclared = kLeader == static_cast<std::size_t>(PhaseLeader::Declared);
    if (!kLeader.has_value() || kDeclared != (leader != nullptr) ||
        (leader != nullptr && leader->kind() != Value::Kind::String)) {
        return graphInvalid("a node's phase sync is weight_leader, or declared_leader with its leader");
    }
    PhaseSync made{.leader = static_cast<PhaseLeader>(*kLeader), .input = {}};
    if (kDeclared) {
        made.input = *leader->text();
    }
    return std::pair{std::optional{made}, std::size_t{kDeclared ? 2U : 1U}};
}

result::Status phaseSyncInForm(const std::optional<PhaseSync>& sync, std::span<const std::string_view> inputs) {
    if (!sync.has_value()) {
        return {};
    }
    if (sync->leader == PhaseLeader::Declared ? !std::ranges::contains(inputs, std::string_view{sync->input})
                                              : !sync->input.empty()) {
        return graphInvalid("a declared phase leader is one of its node's inputs, and a weighed one names none");
    }
    return {};
}

} // namespace rawframe::animation
