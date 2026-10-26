#pragma once

// The standard subgraph layer: first-party functions, each a
// function document over the atomic core shipped with the engine, its
// interface's inputs read by parameter nodes, each of its outputs fed by an
// output node. A node of a function's type calls it, its inputs the
// function's, its outputs the function's; the generated material writes
// the function's nodes in its place.

#include "rawframe/graph/graph.h"
#include "rawframe/result/result.h"
#include "surface_graph.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::material::functions {

/// One of a function's outputs: its name, its type, and what feeds its
/// output node.
struct Output {
    std::string_view name;
    surface_graph::Carried carried = surface_graph::Carried::Float;
    surface_graph::Operand from;
};

/// A function document, read and checked.
struct Function {
    graph::Document document;
    std::vector<surface_graph::InterfaceInput> inputs;
    std::vector<Output> outputs;
};

/// Whether a node type is one of the engine's functions.
[[nodiscard]] bool isFunction(std::string_view type);

/// The engine's functions' types, in the library's order.
[[nodiscard]] std::vector<std::string_view> types();

/// The engine's function of `type`, read from the library it ships with.
[[nodiscard]] result::Result<Function> functionOf(std::string_view type);

/// Reads and checks a function document in its canonical form: kind
/// `function`; an interface of its inputs (each a literal's default, a
/// stream's, or a texture its calls give) and one to four typed
/// outputs; and nodes of the atomic core but the surface and the quality
/// switch, never another function's, with one output node each output,
/// typed as it says. Refuses (`Invalid`) any other.
[[nodiscard]] result::Result<Function> readFunction(std::string_view text);

/// The texture a call gives one of its function's texture inputs:
/// the game's texture by its identity, and the caller's texture input
/// giving it, if one does, its default that texture.
struct GivenTexture {
    std::uint64_t id = 0;
    std::optional<std::size_t> input;
};

/// A call: the function a node's type names, and what each of its inputs
/// is given, in the function's inputs' order, none where the call leaves
/// it to its default; a texture input's texture in `textures` instead.
struct Call {
    Function function;
    std::vector<std::optional<surface_graph::Operand>> given;
    std::vector<GivenTexture> textures;
};

/// A calling node's inputs, checked: each one of the function's, of its
/// type, each texture input given the game's texture by 16 lowercase hex
/// digits or the caller's texture input by its parameter_texture_2d node.
/// Refuses (`Invalid`) any other, and params, which a call has none of.
[[nodiscard]] result::Result<Call> callIn(const surface_graph::Graph& surface, const graph::Node& node);

} // namespace rawframe::material::functions
