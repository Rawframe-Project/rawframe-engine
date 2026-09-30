#include "rawframe/graph/graph.h"

#include "rawframe/graph/errors.h"

#include <algorithm>
#include <array>
#include <map>

namespace rawframe::graph {

namespace {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kGraphDomain, code(GraphError::Invalid), why);
}

std::unexpected<result::Error> overLimit(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kGraphDomain, code(GraphError::OverLimit), why);
}

std::string hexOf(const base::Sha256Digest& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::byte kByte : digest) {
        const auto kValue = std::to_integer<unsigned>(kByte);
        text.push_back(kDigits[kValue >> 4U]);
        text.push_back(kDigits[kValue & 15U]);
    }
    return text;
}

/// A record in the grammar: an object led by its `type`, in form, with
/// `params` and `inputs` objects where it has them. What else a record
/// holds, and the order of its params and inputs, are its family's: a
/// family keeps a record of a type it does not know whole.
result::Status recordInForm(const Value& record, const Limits& limits) {
    const Value* params = record.find("params");
    const Value* inputs = record.find("inputs");
    if (record.kind() != Value::Kind::Object || record.names().empty() || record.names()[0] != "type" ||
        record.items()[0].kind() != Value::Kind::String || !typeIdInForm(*record.items()[0].text()) ||
        (params != nullptr && params->kind() != Value::Kind::Object) ||
        (inputs != nullptr && inputs->kind() != Value::Kind::Object)) {
        return invalid("a node is led by its type, its params and inputs objects");
    }
    if ((params != nullptr && params->names().size() > limits.maximumInputs) ||
        (inputs != nullptr && inputs->names().size() > limits.maximumInputs)) {
        return overLimit("a node has more params or inputs than its limits");
    }
    return {};
}

} // namespace

std::string nodeIdText(NodeId id) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text(16, '0');
    for (std::size_t at = 0; at < 16; ++at) {
        text[15 - at] = kDigits[(id >> (at * 4)) & 15U];
    }
    return text;
}

std::optional<NodeId> nodeIdOf(std::string_view text) noexcept {
    if (text.size() != 16) {
        return std::nullopt;
    }
    NodeId id = 0;
    for (const char kDigit : text) {
        const bool kNumeral = kDigit >= '0' && kDigit <= '9';
        if (!kNumeral && (kDigit < 'a' || kDigit > 'f')) {
            return std::nullopt;
        }
        id = (id << 4U) | static_cast<NodeId>(kNumeral ? kDigit - '0' : kDigit - 'a' + 10);
    }
    return id;
}

bool machineName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64 || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    return std::ranges::all_of(name, [](char each) {
        return (each >= 'a' && each <= 'z') || (each >= '0' && each <= '9') || each == '_';
    });
}

bool typeIdInForm(std::string_view type) noexcept {
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
    // The standard library's `rawframe`, or a package's `publisher/package`.
    return (segments == 2 || segments == 3) && (segments == 3 || kPath.starts_with("rawframe/")) && kVersioned;
}

Value connectionValue(const Connection& connection) {
    Value made = Value::object();
    made.add("node", Value::string(nodeIdText(connection.node)));
    made.add("output", Value::string(connection.output));
    return made;
}

std::optional<Connection> connectionOf(const Value& value) {
    if (value.kind() != Value::Kind::Object || value.names().size() != 2 || value.names()[0] != "node" ||
        value.names()[1] != "output" || value.items()[0].kind() != Value::Kind::String ||
        value.items()[1].kind() != Value::Kind::String || !machineName(*value.items()[1].text())) {
        return std::nullopt;
    }
    const std::optional<NodeId> kNode = nodeIdOf(*value.items()[0].text());
    if (!kNode.has_value()) {
        return std::nullopt;
    }
    return Connection{.node = *kNode, .output = *value.items()[1].text()};
}

std::vector<Connection> connectionsOf(const Value& record) {
    std::vector<Connection> made;
    if (const Value* kInputs = record.find("inputs"); kInputs != nullptr) {
        for (const Value& input : kInputs->items()) {
            if (std::optional<Connection> connection = connectionOf(input)) {
                made.push_back(std::move(*connection));
            }
        }
    }
    return made;
}

result::Status validate(const Document& graph, const Limits& limits) {
    if (graph.nodes.size() > limits.maximumNodes || graph.presentation.size() > limits.maximumPresentation) {
        return overLimit("a graph has more nodes or drawings than its limits");
    }
    if (graph.interface.kind() != Value::Kind::Object) {
        return invalid("a graph's interface is an object");
    }
    for (std::size_t at = 0; at < graph.nodes.size(); ++at) {
        if (at > 0 && graph.nodes[at - 1].id >= graph.nodes[at].id) {
            return invalid("a graph's nodes are in id order, each once");
        }
        RAWFRAME_TRY(recordInForm(graph.nodes[at].record, limits));
    }
    const auto kIndexOf = [&graph](NodeId id) -> std::optional<std::size_t> {
        const auto kFound = std::ranges::lower_bound(graph.nodes, id, {}, &Node::id);
        if (kFound == graph.nodes.end() || kFound->id != id) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(kFound - graph.nodes.begin());
    };
    // Every connection to a node there; then no cycle: 0 unseen, 1 on the
    // path, 2 done, and a node met again on the path closes one.
    std::vector<std::vector<std::size_t>> from(graph.nodes.size());
    for (std::size_t at = 0; at < graph.nodes.size(); ++at) {
        for (const Connection& connection : connectionsOf(graph.nodes[at].record)) {
            const std::optional<std::size_t> kTo = kIndexOf(connection.node);
            if (!kTo.has_value()) {
                return invalid("a graph's connections are to its nodes");
            }
            from[at].push_back(*kTo);
        }
    }
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
            if (next == from[at].size()) {
                marks[at] = 2;
                path.pop_back();
                continue;
            }
            const std::size_t kTo = from[at][next++];
            if (marks[kTo] == 1) {
                return invalid("a graph has no cycle");
            }
            if (marks[kTo] == 0) {
                marks[kTo] = 1;
                path.emplace_back(kTo, 0);
            }
        }
    }
    for (std::size_t at = 0; at < graph.presentation.size(); ++at) {
        if ((at > 0 && graph.presentation[at - 1].first >= graph.presentation[at].first) ||
            !kIndexOf(graph.presentation[at].first).has_value()) {
            return invalid("a graph's presentation is keyed by its nodes, in order");
        }
    }
    return {};
}

result::Result<std::string> writeDocument(const Document& graph, const Limits& limits) {
    RAWFRAME_TRY(validate(graph, limits));
    Value written = Value::object();
    written.add("formatVersion", Value::integer(1));
    written.add("kind", Value::string(graph.kind));
    written.add("interface", graph.interface);
    Value nodes = Value::object();
    for (const Node& node : graph.nodes) {
        nodes.add(nodeIdText(node.id), node.record);
    }
    written.add("graph", std::move(nodes));
    for (const auto& [kName, kSection] : graph.sections) {
        written.add(kName, kSection);
    }
    if (!graph.presentation.empty()) {
        Value drawn = Value::object();
        for (const auto& [kId, kDrawing] : graph.presentation) {
            drawn.add(nodeIdText(kId), kDrawing);
        }
        written.add("presentation", std::move(drawn));
    }
    std::string text = document::write(written);
    if (text.size() > limits.maximumBytes) {
        return overLimit("a graph is longer than its limits");
    }
    return text;
}

result::Result<Document> readDocument(std::string_view text,
                                      std::string_view kind,
                                      std::span<const std::string_view> sections,
                                      const Limits& limits) {
    if (text.size() > limits.maximumBytes) {
        return overLimit("a graph is longer than its limits");
    }
    auto parsed = document::parse(text, {.maximumBytes = limits.maximumBytes});
    if (!parsed.has_value()) {
        return std::unexpected<result::Error>{std::move(parsed).error()};
    }
    const Value& root = *parsed;
    // The members in their order: the four the grammar requires, the
    // family's sections it has, then an optional presentation.
    std::vector<std::string_view> expected = {"formatVersion", "kind", "interface", "graph"};
    for (const std::string_view kSection : sections) {
        if (root.find(kSection) != nullptr) {
            expected.push_back(kSection);
        }
    }
    const Value* drawn = root.find("presentation");
    if (drawn != nullptr) {
        expected.emplace_back("presentation");
    }
    if (root.kind() != Value::Kind::Object || !std::ranges::equal(root.names(), expected)) {
        return invalid("a graph is its format version, kind, interface, nodes, its family's sections, and an optional "
                       "presentation, in that order");
    }
    const Value* kKind = root.find("kind");
    const Value& nodes = *root.find("graph");
    if (root.find("formatVersion")->integer() != 1 || kKind->text() == nullptr ||
        kKind->kind() != Value::Kind::String || *kKind->text() != kind || nodes.kind() != Value::Kind::Object ||
        (drawn != nullptr && drawn->kind() != Value::Kind::Object)) {
        return invalid("a graph is format version 1 of its kind, its nodes an object");
    }
    if (nodes.names().size() > limits.maximumNodes ||
        (drawn != nullptr && drawn->names().size() > limits.maximumPresentation)) {
        return overLimit("a graph has more nodes or drawings than its limits");
    }
    Document graph{.kind = std::string{kind}, .interface = *root.find("interface")};
    for (std::size_t at = 0; at < nodes.names().size(); ++at) {
        const std::optional<NodeId> kId = nodeIdOf(nodes.names()[at]);
        if (!kId.has_value()) {
            return invalid("a graph's nodes are keyed by 16 lowercase hex digits");
        }
        graph.nodes.push_back({.id = *kId, .record = nodes.items()[at]});
    }
    for (const std::string_view kSection : sections) {
        if (const Value* kFound = root.find(kSection); kFound != nullptr) {
            graph.sections.emplace_back(std::string{kSection}, *kFound);
        }
    }
    for (std::size_t at = 0; drawn != nullptr && at < drawn->names().size(); ++at) {
        const std::optional<NodeId> kId = nodeIdOf(drawn->names()[at]);
        if (!kId.has_value()) {
            return invalid("a graph's presentation is keyed by its nodes");
        }
        graph.presentation.emplace_back(*kId, drawn->items()[at]);
    }
    RAWFRAME_TRY(validate(graph, limits));
    return graph;
}

result::Result<base::Sha256Digest> semanticHash(std::span<const Node> nodes, const HashHeader& header) {
    const auto kNodeOf = [&nodes](NodeId id) -> const Node* {
        const auto kFound = std::ranges::lower_bound(nodes, id, {}, &Node::id);
        return kFound == nodes.end() || kFound->id != id ? nullptr : &*kFound;
    };
    // Bottom up from each output, each node once.
    std::map<NodeId, base::Sha256Digest> digests;
    const auto kDigestOf = [&kNodeOf, &digests](NodeId root) -> result::Result<base::Sha256Digest> {
        std::vector<std::pair<NodeId, bool>> pending{{root, false}};
        while (!pending.empty()) {
            const auto [kId, kExpanded] = pending.back();
            pending.pop_back();
            if (digests.contains(kId)) {
                continue;
            }
            const Node* kNode = kNodeOf(kId);
            if (kNode == nullptr) {
                return invalid("a graph's outputs and connections are to its nodes");
            }
            if (!kExpanded) {
                if (pending.size() > (std::size_t{1} << 20U)) {
                    return invalid("a graph has no cycle");
                }
                pending.emplace_back(kId, true);
                for (const Connection& from : connectionsOf(kNode->record)) {
                    pending.emplace_back(from.node, false);
                }
                continue;
            }
            const Value* kFound = kNode->record.find("inputs");
            const Value kNone = Value::object();
            const Value& kInputs = kFound != nullptr ? *kFound : kNone;
            Value inputs = Value::object();
            for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
                const std::optional<Connection> kFrom = connectionOf(kInputs.items()[at]);
                if (!kFrom.has_value()) {
                    inputs.add(kInputs.names()[at], kInputs.items()[at]);
                    continue;
                }
                const auto kDigest = digests.find(kFrom->node);
                if (kDigest == digests.end()) {
                    return invalid("a graph has no cycle");
                }
                Value input = Value::object();
                input.add("node_digest", Value::string(hexOf(kDigest->second)));
                input.add("output", Value::string(kFrom->output));
                inputs.add(kInputs.names()[at], std::move(input));
            }
            Value hashed = Value::object();
            hashed.add("type", *kNode->record.find("type"));
            const Value* kParams = kNode->record.find("params");
            hashed.add("params", kParams != nullptr ? *kParams : Value::object());
            hashed.add("inputs", std::move(inputs));
            digests.emplace(kId, base::sha256(document::writeCompact(hashed)));
        }
        return digests.at(root);
    };
    Value outputs = Value::object();
    for (const auto& [kName, kNode] : header.outputs) {
        RAWFRAME_TRY_ASSIGN(const base::Sha256Digest kDigest, kDigestOf(kNode));
        outputs.add(kName, Value::string(hexOf(kDigest)));
    }
    Value hashed = Value::object();
    hashed.add("formatVersion", Value::integer(1));
    hashed.add("kind", Value::string(std::string{header.kind}));
    hashed.add("interface", header.interface);
    hashed.add("outputs", std::move(outputs));
    for (const auto& [kName, kSection] : header.sections) {
        hashed.add(kName, kSection);
    }
    return base::sha256(document::writeCompact(hashed));
}

} // namespace rawframe::graph
