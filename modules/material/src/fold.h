#pragma once

// The folded families' graphs (D348, D355): the post-process and canvas
// domains read their nodes alike and fold their values into one form, a
// constant plus what multiplies the picture's color, one texture's, and
// the two multiplied. A domain names its output node, the node giving
// the coordinates a texture is sampled at, and whether it reads the
// picture; its output's inputs are its own to check.

#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/material.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::material {

/// A folded family's domain: what its documents are called in a refusal,
/// its output node's type, the node giving a texture's coordinates when
/// its uv is left out, and whether it reads the picture (`scene_color`,
/// `scene_depth`).
struct FoldDomain {
    std::string_view called;
    std::string_view output;
    std::string_view coordinates;
    bool picture = false;
};

/// What an output of a node a domain knows carries.
enum class Carried : std::uint8_t {
    Float,
    Vec2,
    Color3,
    Color4
};

/// A literal: one to four finite numbers, a float, a vec2, a color3, or a
/// color4; a float's number in every channel.
struct Literal {
    std::array<double, 4> values{};
    Carried carried = Carried::Float;
};

/// An input: connected, or a literal, and what it carries when known.
struct Operand {
    std::optional<graph::Connection> from;
    Literal literal;
    std::optional<Carried> carried;
};

/// The domain's rules over a document's nodes, each output's type found
/// once however many inputs it feeds.
class Checker {
public:
    Checker(const graph::Document& document, const FoldDomain& domain) noexcept;

    /// What the output a connection names carries, if its node is of a
    /// type the domain knows; refused when the node has no such output.
    result::Result<std::optional<Carried>> carriedBy(const graph::Connection& from);

    result::Result<Operand> operandOf(const document::Value& value);

    /// A node's two inputs `first` and `second`, both given.
    result::Result<std::pair<Operand, Operand>>
    pairIn(const graph::Node& node, std::string_view first, std::string_view second);

    /// A `sample_texture_2d` node's texture and sampler state, and its uv,
    /// if connected, from a vec2.
    result::Result<std::pair<SampledTexture, std::optional<graph::Connection>>> sampleIn(const graph::Node& node);

    /// A node of the domain other than its output, checked; one of
    /// another type is kept whole.
    result::Status check(const graph::Node& node);

private:
    result::Result<std::optional<Carried>> mathOut(const graph::Node& node);
    result::Status combineIn(const graph::Node& node);
    result::Result<std::optional<Carried>> find(const graph::Connection& from);

    const graph::Document& document_;
    const FoldDomain& domain_;
    std::map<std::pair<graph::NodeId, std::string>, std::optional<Carried>> seen_;
};

/// A value folded: per channel, a constant, and what multiplies the
/// picture's color, the texture's, and the two multiplied. The picture's
/// alpha is one, so a fourth channel holds only a constant and the
/// texture's alpha; a float's every channel is its number, but one of the
/// texture's alpha has it in the fourth alone.
struct Form {
    Carried carried = Carried::Float;
    std::array<double, 4> constant{};
    std::array<double, 4> scene{};
    std::array<double, 4> texture{};
    std::array<double, 4> both{};
    bool alphaOnly = false;
};

/// Folds a document's values; the texture they sample is one for them all.
class Folder {
public:
    Folder(const graph::Document& document, const FoldDomain& domain) noexcept;

    result::Result<Form> formOf(const document::Value& value);
    result::Result<Form> formOf(const graph::Connection& from);

    /// The texture sampled, its coordinates scaled and moved.
    [[nodiscard]] const SampledTexture& sampled() const noexcept;

private:
    /// The coordinates scaled and moved (SPEC-0026's `tile_and_offset`,
    /// folded).
    struct Affine {
        std::array<double, 2> scale{1, 1};
        std::array<double, 2> offset{0, 0};
    };

    result::Result<Form> fold(const graph::Connection& from);
    result::Result<Carried> outOf(const Form& a, const Form& b) const;
    result::Result<Form> sum(const Form& a, const Form& b) const;
    result::Result<Form> product(const Form& a, const Form& b) const;
    result::Result<Affine> affineOf(const graph::Connection& from);

    const graph::Document& document_;
    const FoldDomain& domain_;
    Checker checker_;
    std::map<std::pair<graph::NodeId, std::string>, Form> seen_;
    std::optional<graph::NodeId> sampler_;
    SampledTexture sampled_;
};

/// The node of the domain's output type, when the document holds exactly
/// one.
[[nodiscard]] const graph::Node* outputOf(const graph::Document& document, const FoldDomain& domain);

/// Writes `word` and `value` little-endian, as the cooked forms hold them.
void putWord(std::vector<std::byte>& bytes, std::uint32_t word);
void putFloat(std::vector<std::byte>& bytes, float value);

} // namespace rawframe::material
