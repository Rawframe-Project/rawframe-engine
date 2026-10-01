#include "common.h"

#include "rawframe/graph/errors.h"
#include "rawframe/material/errors.h"

#include <algorithm>

namespace rawframe::material {

using document::Value;

std::unexpected<result::Error> invalid(std::string_view why) {
    return result::fail(result::ErrorClass::InvalidArgument, kMaterialDomain, code(MaterialError::Invalid), why);
}

std::unexpected<result::Error> unsupported(std::string_view why) {
    return result::fail(result::ErrorClass::Unsupported, kMaterialDomain, code(MaterialError::Unsupported), why);
}

std::unexpected<result::Error> asMaterial(result::Error error) {
    if (error.domain() != graph::kGraphDomain) {
        return std::unexpected<result::Error>{std::move(error)};
    }
    return result::fail(result::ErrorClass::InvalidArgument,
                        kMaterialDomain,
                        code(error.code() == graph::code(graph::GraphError::OverLimit) ? MaterialError::OverLimit
                                                                                       : MaterialError::Invalid),
                        error.description());
}

const graph::Node* nodeOf(const graph::Document& document, graph::NodeId id) {
    const auto kFound = std::ranges::find(document.nodes, id, &graph::Node::id);
    return kFound == document.nodes.end() ? nullptr : &*kFound;
}

result::Result<std::pair<const Value*, const Value*>>
partsOf(const graph::Node& node, std::span<const std::string_view> params, std::span<const std::string_view> inputs) {
    const Value* kParams = node.record.find("params");
    const Value* kInputs = node.record.find("inputs");
    if (node.record.names().size() != 3 || kParams == nullptr || kInputs == nullptr ||
        node.record.names()[1] != "params" || kParams->kind() != Value::Kind::Object ||
        kInputs->kind() != Value::Kind::Object) {
        return invalid("a node of the library is its type, its params, and its inputs");
    }
    for (const auto& [kNames, kAllowed] : {std::pair{kParams, params}, std::pair{kInputs, inputs}}) {
        std::size_t next = 0;
        for (const std::string& name : kNames->names()) {
            const auto kPlace = std::ranges::find(kAllowed, name);
            if (kPlace == kAllowed.end() || static_cast<std::size_t>(kPlace - kAllowed.begin()) < next) {
                return invalid("a node of the library has its type's params and inputs, in name order");
            }
            next = static_cast<std::size_t>(kPlace - kAllowed.begin()) + 1;
        }
    }
    return std::pair{kParams, kInputs};
}

} // namespace rawframe::material
