#include "rawframe/authoring/authored_material.h"

#include "rawframe/authoring/errors.h"
#include "rawframe/material/instance.h"
#include "rawframe/material/material.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <variant>

namespace rawframe::authoring {

namespace {

using document::Value;

std::unexpected<result::Error> refuse(result::ErrorClass errorClass, AuthoringError error, std::string_view why) {
    return result::fail(errorClass, kAuthoringDomain, code(error), why);
}

constexpr std::array<std::string_view, 4> kInstanceFields = {"formatVersion", "kind", "base", "overrides"};
constexpr std::array<std::string_view, 6> kGraphFields = {
    "formatVersion", "kind", "interface", "graph", "states", "presentation"};
constexpr std::array<std::string_view, 2> kInterfaceFields = {"inputs", "switches"};
constexpr std::array<std::string_view, 3> kNodeFields = {"type", "params", "inputs"};
constexpr std::array<std::string_view, 4> kStatesFields = {"shading", "blend", "alpha_cutoff", "double_sided"};

/// The instance's objects' members in order: the root's, and its overrides
/// by name (D721).
std::span<const std::string_view> instanceOrderOf(std::span<const JsonStep> at) noexcept {
    if (at.empty()) {
        return kInstanceFields;
    }
    return {};
}

/// The graph's objects' members in order, as its canonical writer puts them
/// (SPEC-0028, D728), by where they are (D729): the root's, the interface's,
/// the states', and a node's in `graph`; the graph's nodes, a node's params
/// and inputs, the interface's inputs and switches, and the presentation by
/// name, which for nodes is their ids' order.
std::span<const std::string_view> graphOrderOf(std::span<const JsonStep> at) noexcept {
    if (at.empty()) {
        return kGraphFields;
    }
    if (at.size() == 1 && at[0].member == "interface") {
        return kInterfaceFields;
    }
    if (at.size() == 1 && at[0].member == "states") {
        return kStatesFields;
    }
    if (at.size() == 2 && at[0].member == "graph") {
        return kNodeFields;
    }
    return {};
}

std::unexpected<result::Error> unread(result::Error error, std::string_view what) {
    std::string why = std::string{what} + " does not read: " + std::string{error.description()};
    return std::unexpected<result::Error>{std::move(error).mappedTo(
        result::ErrorClass::InvalidArgument, kAuthoringDomain, code(AuthoringError::ValidationFailed), why)};
}

result::Status readsInstance(std::string_view text) {
    auto read = material::readInstance(text);
    if (!read.has_value()) {
        return unread(std::move(read).error(), "the material instance");
    }
    return {};
}

result::Status readsGraph(std::string_view text) {
    auto read = material::readMaterial(text);
    if (!read.has_value()) {
        return unread(std::move(read).error(), "the material graph");
    }
    return {};
}

/// A Kest name's shape, as an interface input's and a switch's are.
bool named(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name[0] >= 'a' && name[0] <= 'z' &&
           std::ranges::all_of(name, [](char letter) {
               return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
           });
}

std::string hexOf(std::uint64_t id) {
    std::array<char, 16> digits{};
    for (std::size_t at = 0; at < digits.size(); ++at) {
        digits[at] = "0123456789abcdef"[(id >> (60 - 4 * at)) & 0xFU];
    }
    return std::string{digits.data(), digits.size()};
}

JsonStep step(std::string member) {
    return JsonStep{.member = std::move(member), .key = {}, .id = {}};
}

JsonDelta setting(const Value& staged, JsonPath path, std::optional<Value> to) {
    std::optional<Value> before = slotAt(staged, path);
    return JsonDelta{.path = std::move(path), .place = 0, .before = std::move(before), .after = std::move(to)};
}

/// The deltas `operation` makes of `staged`, an instance's or, `graph`, a
/// graph's; refused where they cannot be made.
result::Result<std::vector<JsonDelta>> deltasOf(const Value& staged, bool graph, const Operation& operation) {
    const auto kNotOurs = [graph] {
        return refuse(result::ErrorClass::InvalidArgument,
                      AuthoringError::ValidationFailed,
                      graph ? "the operation does not change a material graph"
                            : "the operation does not change a material instance");
    };
    if (const auto* set = std::get_if<SetMaterialOverride>(&operation)) {
        if (graph) {
            return kNotOurs();
        }
        if (!named(set->input)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "an override names an input or a switch as a Kest name is");
        }
        return std::vector{setting(staged, {step("overrides"), step(set->input)}, set->value)};
    }
    if (!graph) {
        return kNotOurs();
    }
    const auto* declared = std::get_if<SetMaterialInterfaceInput>(&operation);
    const auto* switched = std::get_if<SetMaterialSwitch>(&operation);
    if (declared != nullptr || switched != nullptr) {
        const std::string& kName = declared != nullptr ? declared->input : switched->name;
        if (!named(kName)) {
            return refuse(result::ErrorClass::InvalidArgument,
                          AuthoringError::ValidationFailed,
                          "an interface input or switch is named as a Kest name is");
        }
        return std::vector{setting(staged,
                                   {step("interface"), step(declared != nullptr ? "inputs" : "switches"), step(kName)},
                                   declared != nullptr ? declared->value : switched->value)};
    }
    const auto kHas = [&staged](std::uint64_t node) {
        return slotAt(staged, JsonPath{step("graph"), step(hexOf(node))}).has_value();
    };
    const auto kUnknown = [] {
        return refuse(result::ErrorClass::NotFound, AuthoringError::TargetNotFound, "the graph has no such node");
    };
    if (const auto* input = std::get_if<SetMaterialNodeInput>(&operation)) {
        if (!kHas(input->node)) {
            return kUnknown();
        }
        if (input->input.empty()) {
            return refuse(result::ErrorClass::InvalidArgument, AuthoringError::ValidationFailed, "an input is named");
        }
        return std::vector{setting(
            staged, {step("graph"), step(hexOf(input->node)), step("inputs"), step(input->input)}, input->value)};
    }
    if (const auto* param = std::get_if<SetMaterialNodeParam>(&operation)) {
        if (!kHas(param->node)) {
            return kUnknown();
        }
        if (param->param.empty()) {
            return refuse(result::ErrorClass::InvalidArgument, AuthoringError::ValidationFailed, "a param is named");
        }
        return std::vector{setting(
            staged, {step("graph"), step(hexOf(param->node)), step("params"), step(param->param)}, param->value)};
    }
    if (const auto* create = std::get_if<CreateMaterialNode>(&operation)) {
        if (create->node == 0 || kHas(create->node)) {
            return refuse(result::ErrorClass::AlreadyExists,
                          AuthoringError::Conflict,
                          "a new node's identity is fresh and not nought");
        }
        Value record = Value::object();
        record.add("type", Value::string(create->type));
        record.add("params", Value::object());
        record.add("inputs", Value::object());
        return std::vector{setting(staged, {step("graph"), step(hexOf(create->node))}, std::move(record))};
    }
    if (const auto* remove = std::get_if<RemoveMaterialNode>(&operation)) {
        if (!kHas(remove->node)) {
            return kUnknown();
        }
        // Its drawing goes with it, keyed by it (SPEC-0028).
        std::vector made{setting(staged, {step("graph"), step(hexOf(remove->node))}, std::nullopt)};
        if (slotAt(staged, JsonPath{step("presentation"), step(hexOf(remove->node))}).has_value()) {
            made.push_back(setting(staged, {step("presentation"), step(hexOf(remove->node))}, std::nullopt));
        }
        return made;
    }
    return kNotOurs();
}

} // namespace

result::Result<std::unique_ptr<AuthoredMaterial>>
AuthoredMaterial::open(base::Bits128 document, std::string_view text, const HistoryLimits& limits) {
    const bool kGraph = !material::namesBase(text);
    const DocumentReader kReads = kGraph ? readsGraph : readsInstance;
    RAWFRAME_TRY(kReads(text));
    RAWFRAME_TRY_ASSIGN(JsonDocument opened,
                        JsonDocument::open(text, kGraph ? graphOrderOf : instanceOrderOf, kReads, limits));
    return std::unique_ptr<AuthoredMaterial>{new AuthoredMaterial(document, std::move(opened), kGraph)};
}

std::string AuthoredMaterial::base() const {
    const Value* kBase = document_.root().find("base");
    return kBase != nullptr && kBase->text() != nullptr ? *kBase->text() : std::string{};
}

result::Result<Committed> AuthoredMaterial::change(std::uint64_t generation, std::span<const Operation> operations) {
    RAWFRAME_TRY_ASSIGN(JsonDocument::Staged staged, document_.stage(generation));
    if (operations.size() > kMaximumBatchOperations) {
        return refuse(result::ErrorClass::ResourceExhausted,
                      AuthoringError::LimitExceeded,
                      "a batch has more operations than its limit");
    }
    for (std::size_t at = 0; at < operations.size(); ++at) {
        auto deltas = deltasOf(staged.root, graph_, operations[at]);
        if (!deltas.has_value()) {
            return std::unexpected<result::Error>{std::move(deltas).error().withContext("index", std::to_string(at))};
        }
        for (JsonDelta& delta : *deltas) {
            auto applied = document_.apply(staged, std::move(delta));
            if (!applied.has_value()) {
                return std::unexpected<result::Error>{
                    std::move(applied).error().withContext("index", std::to_string(at))};
            }
        }
    }
    return document_.keep(std::move(staged));
}

std::string AuthoredMaterial::summaryAt(std::size_t entry) const {
    const std::vector<JsonDelta>& journal = document_.journalAt(entry);
    if (journal.empty()) {
        return "no change";
    }
    const JsonDelta& kFirst = journal.front();
    const bool kSet = kFirst.after.has_value();
    std::string made;
    if (kFirst.path.size() == 3 && kFirst.path[0].member == "interface") {
        made = (kSet ? "declare " : "drop ") + std::string{kFirst.path[1].member == "inputs" ? "input " : "switch "} +
               kFirst.path[2].member;
    } else if (kFirst.path.size() == 2 && kFirst.path[0].member == "overrides") {
        made = (kSet ? "set " : "unset ") + kFirst.path[1].member;
    } else if (kFirst.path.size() == 2) {
        const Value* kType = kSet ? kFirst.after->find("type") : kFirst.before->find("type");
        made = (kSet ? "create node " : "remove node ") +
               (kType != nullptr && kType->text() != nullptr ? *kType->text() : kFirst.path[1].member);
    } else {
        made = (kSet ? "set " : "unset ") + kFirst.path[1].member.substr(0, 8) + "." + kFirst.path[3].member;
    }
    // A node's drawing removed with it is the node's removal, not another.
    const std::size_t kMore =
        journal.size() - 1 - static_cast<std::size_t>(std::ranges::count_if(journal, [](const JsonDelta& delta) {
            return delta.path.front().member == "presentation";
        }));
    if (kMore > 0) {
        made += " and " + std::to_string(kMore) + " more";
    }
    return made;
}

result::Result<Value> answer(const AuthoredMaterial& material, const Query& query) {
    if (std::holds_alternative<ListMaterialNodeTypes>(query) && material.graph()) {
        const auto kWords = [](const std::vector<std::string>& names) {
            Value made = Value::array();
            for (const std::string& kName : names) {
                made.push(Value::string(kName));
            }
            return made;
        };
        Value types = Value::array();
        for (const material::NodeSignature& kSignature : material::nodeSignatures()) {
            Value type = Value::object();
            type.add("type", Value::string(kSignature.type));
            type.add("params", kWords(kSignature.params));
            type.add("inputs", kWords(kSignature.inputs));
            type.add("outputs", kWords(kSignature.outputs));
            types.push(std::move(type));
        }
        Value made = Value::object();
        made.add("types", std::move(types));
        return made;
    }
    const bool kInstance = std::holds_alternative<ReadMaterialInstance>(query) && !material.graph();
    const bool kGraph = std::holds_alternative<ReadMaterialGraph>(query) && material.graph();
    if (!kInstance && !kGraph) {
        return std::unexpected<result::Error>{refuse(result::ErrorClass::InvalidArgument,
                                                     AuthoringError::ValidationFailed,
                                                     material.graph() ? "the query does not read a material graph"
                                                                      : "the query does not read a material instance")
                                                  .error()
                                                  .withContext("operation", declarationOf(query).name)};
    }
    Value made = Value::object();
    made.add(kInstance ? "instance" : "graph", material.documentValue());
    return made;
}

} // namespace rawframe::authoring
