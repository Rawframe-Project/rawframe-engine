#pragma once

// Canvas materials (ADR-0031, SPEC-0026's `canvas` domain, D355): graph
// documents of kind `canvas`, one of them a `rawframe/canvas@1` node, the
// domain's output, whose inputs are what a 2D draw shows: `color` (color4,
// left out at (1, 1, 1, 1)), times the draw's own color (its vertex
// color), and `emission` (color3, left out at (0, 0, 0)), the light it
// gives off, added.
//
//   {
//     "formatVersion": 1,
//     "kind": "canvas",
//     "interface": {},
//     "graph": {
//       "1f00000000000001": {
//         "type": "rawframe/sample_texture_2d@1",
//         "params": {"texture": "9f822820a44af4fc"},
//         "inputs": {}
//       },
//       "1f00000000000002": {
//         "type": "rawframe/combine4@1",
//         "params": {},
//         "inputs": {
//           "in1": {"node": "1f00000000000001", "output": "color"},
//           "in2": {"node": "1f00000000000001", "output": "alpha"}
//         }
//       },
//       "2f00000000000001": {
//         "type": "rawframe/canvas@1",
//         "params": {},
//         "inputs": {"color": {"node": "1f00000000000002", "output": "out"}}
//       }
//     },
//     "states": {"blend": "additive"}
//   }
//
// (shown on fewer lines than the canonical form). Its states, each left
// out at its default: `shading`, `lit` (the default) or `unlit`; and
// `blend`, SPEC-0026's canvas set, `normal` (the default: over what is
// behind by its alpha), `additive` (its color times its alpha added), or
// `multiply` (what is behind times its color). With no 2D lights yet, a
// lit material shows as an unlit one does, which is what it is under no
// light; its `normal_map` waits for them, refused until then.
//
// The domain's nodes: `uv` gives the draw's texture coordinates as `uv`
// (vec2); `sample_texture_2d` samples a game's texture at its `uv`, the
// draw's coordinates when left out; `multiply`, `add`, and `combine4` as a
// post process has them (D348). What folds compiles, with no shader made
// for a material: each output a constant plus a constant times one
// texture's color (its alpha in a color's fourth), channel by channel.

#include "rawframe/base/bits128.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/material.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::material {

/// SPEC-0026's canvas blend set.
enum class CanvasBlend : std::uint8_t {
    Normal,
    Additive,
    Multiply
};

/// A compiled canvas material: lit or unlit, how it blends, its color as
/// `color` plus `colorTexture` times the texture's color (its alpha in the
/// fourth), and its emission as `emission` plus `emissionTexture` times the
/// texture's color. The texture is none when its identity is nought, and
/// then both texture terms are nought.
struct CanvasMaterial {
    Shading shading = Shading::Lit;
    CanvasBlend blend = CanvasBlend::Normal;
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 4> colorTexture{0, 0, 0, 0};
    std::array<float, 3> emission{0, 0, 0};
    std::array<float, 3> emissionTexture{0, 0, 0};
    SampledTexture sampled;
    friend bool operator==(const CanvasMaterial&, const CanvasMaterial&) = default;
};

/// The cooked canvas material's resource type and representation
/// (ADR-0024).
inline constexpr base::Bits128 kCanvasMaterialType = base::parseBits128Hex("c4a1d0e83f5b42679e2d7b16a8f03c51").value;
inline constexpr std::string_view kCanvasMaterialRepresentation = "rawframe.canvasmaterial";

/// The domain's output.
inline constexpr std::string_view kCanvasOutputType = "rawframe/canvas@1";

/// Refuses (`Invalid`) a document out of the domain's contract: kind
/// `canvas`, an empty interface, one output node, its `color` a color4
/// and its `emission` a color3 (literals of finite numbers, the color's
/// alpha from nought to one and the emission never below nought, each
/// left out at its default), its `normal_map` a color3; its states in
/// their sets, left out at their defaults; the domain's nodes as the
/// header has them, and each connection to one from an output it has, of
/// the input's type.
[[nodiscard]] result::Status validateCanvas(const graph::Document& material, const graph::Limits& limits = {});
[[nodiscard]] result::Result<std::string> writeCanvas(const graph::Document& material,
                                                      const graph::Limits& limits = {});
/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<graph::Document> readCanvas(std::string_view text, const graph::Limits& limits = {});

/// Folds the document as the header has it. Refuses (`Unsupported`) what
/// does not fold: a node of a type it does not know, a second texture, a
/// product past the form, the texture's alpha in a color, a normal map.
[[nodiscard]] result::Result<CanvasMaterial> compileCanvas(const graph::Document& material);

/// A compiled canvas material's bytes: `RFCM`, format 1, its shading and
/// blend (a byte each) and two of nought; its color and color texture,
/// four numbers each, and its emission and emission texture, four numbers
/// each with the fourth nought; then its texture's identity (eight bytes),
/// filter and address (a byte each), two of nought, and its scale and
/// offset (two numbers each).
[[nodiscard]] std::vector<std::byte> encodeCanvas(const CanvasMaterial& made);
/// Refuses (`Invalid`) bytes `encodeCanvas` would not make.
[[nodiscard]] result::Result<CanvasMaterial> decodeCanvas(std::span<const std::byte> bytes);

} // namespace rawframe::material
