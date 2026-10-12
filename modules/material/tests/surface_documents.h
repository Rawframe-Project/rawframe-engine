#pragma once

// Surface material documents the tests build node by node: a surface node
// keyed kNode and the nodes after it, connections, a sampled texture, and
// what a refusal is.

#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/errors.h"
#include "rawframe/material/material.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::material::testing {

inline bool refusedWith(const auto& outcome, MaterialError error) {
    return !outcome.has_value() && outcome.error().domain() == kMaterialDomain && outcome.error().code() == code(error);
}

constexpr graph::NodeId kNode = 0x2f00000000000002ULL;

using document::Value;

constexpr graph::NodeId kSampler = kNode + 1;
constexpr graph::NodeId kUv = kNode + 2;

inline Value objectOf(std::vector<std::pair<std::string, Value>> members) {
    Value made = Value::object();
    for (auto& [name, value] : members) {
        made.add(std::move(name), std::move(value));
    }
    return made;
}

inline graph::Node nodeOf(graph::NodeId id, std::string_view type, Value params, Value inputs) {
    return {.id = id,
            .record = objectOf({{"type", Value::string(std::string{type})},
                                {"params", std::move(params)},
                                {"inputs", std::move(inputs)}})};
}

/// A surface node given `inputs`, then `nodes`.
inline graph::Document surfaceOf(Value inputs, std::vector<graph::Node> nodes) {
    graph::Document made{.kind = "surface"};
    made.nodes.push_back(nodeOf(kNode, kSurfaceType, Value::object(), std::move(inputs)));
    std::ranges::move(nodes, std::back_inserter(made.nodes));
    return made;
}

inline Value from(graph::NodeId node, std::string output) {
    return graph::connectionValue({.node = node, .output = std::move(output)});
}

/// A sampler of the texture a44ecb4a39ac5cc8 with `params` besides.
inline graph::Node samplerOf(std::vector<std::pair<std::string, Value>> params, Value inputs = Value::object()) {
    params.emplace_back("texture", Value::string("a44ecb4a39ac5cc8"));
    return nodeOf(kSampler, kSampleTexture2dType, objectOf(std::move(params)), std::move(inputs));
}

/// Its color feeding the base color, sampled at a uv node's `channel`.
inline graph::Document sampledAt(std::int64_t channel) {
    std::vector<std::pair<std::string, Value>> params;
    if (channel != 0) {
        params.emplace_back("channel", Value::integer(channel));
    }
    return surfaceOf(objectOf({{"base_color", from(kSampler, "color")}}),
                     {samplerOf({}, objectOf({{"uv", from(kUv, "uv")}})),
                      nodeOf(kUv, kUvType, objectOf(std::move(params)), Value::object())});
}

inline graph::Node mathOf(graph::NodeId id, std::string_view type, Value a, Value b) {
    return nodeOf(id, type, Value::object(), objectOf({{"a", std::move(a)}, {"b", std::move(b)}}));
}

inline Value pair(double x, double y) {
    return Value::array({Value::real(x), Value::real(y)});
}

} // namespace rawframe::material::testing
