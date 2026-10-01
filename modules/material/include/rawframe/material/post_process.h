#pragma once

// Post-process materials (ADR-0031, ADR-0051, SPEC-0026's `post_process`
// domain, D348): graph documents of kind `post_process`, one of them a
// `rawframe/post_process@1` node, the domain's output, whose one input,
// `color` (color4), is what the chain's picture becomes at its insertion
// point: its color where its alpha is one, blended over the picture coming
// in by its alpha. Left out, it is (0, 0, 0, 0), which changes nothing.
//
//   {
//     "formatVersion": 1,
//     "kind": "post_process",
//     "interface": {},
//     "graph": {
//       "1f00000000000001": {"type": "rawframe/scene_color@1", "params": {}, "inputs": {}},
//       "1f00000000000002": {
//         "type": "rawframe/multiply@1",
//         "params": {},
//         "inputs": {
//           "a": {"node": "1f00000000000001", "output": "color"},
//           "b": [1.0, 0.9, 0.8, 1.0]
//         }
//       },
//       "2f00000000000001": {
//         "type": "rawframe/post_process@1",
//         "params": {},
//         "inputs": {"color": {"node": "1f00000000000002", "output": "out"}}
//       }
//     },
//     "states": {"insertion": "before_tonemap"}
//   }
//
// (shown on fewer lines than the canonical form). The one state,
// `insertion`, is where in ADR-0051's chain it runs, the closed set of
// five: `after_temporal` (scene-linear, after the temporal slot),
// `before_tonemap` (scene-linear, after the grade), `after_tonemap` (the
// default: display-referred, before FXAA), `scene_output` (the view's
// picture, before the canvas), `final_output` (the composed picture).
//
// The domain's nodes: `scene_color` gives the picture coming in as
// `color` (color4, its alpha one); `scene_depth` its depth as `depth`
// (float); `screen_uv` where on the picture a texel is as `uv` (vec2, from
// the top left). `sample_texture_2d` samples a game's texture as a surface
// does, at its `uv` input, or the picture's coordinates when left out;
// `multiply` and `add` take a number, two, three, or four as literals
// (float, vec2, color3, color4); `combine4` makes a color4 of a color3
// `in1` and a float `in2`, each connected or a literal. A node of another
// type is kept whole, as SPEC-0028 keeps what it does not know.
//
// Generation 1 compiles what folds, with no shader made for a material:
// every value is a sum of a constant, the picture's color, one texture's
// color, and the two multiplied, each by a constant, channel by channel
// (the picture's alpha being one, the alpha holds a constant and the
// texture's alpha). So one texture, sampled at the picture's coordinates
// scaled and moved by literals; the picture and the texture each at most
// once in a product; and the texture's alpha only in the alpha. Depth, a
// second texture, and anything else wait for the rest of the library.

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

/// ADR-0051's closed set of insertion points, in the chain's order.
enum class Insertion : std::uint8_t {
    AfterTemporal,
    BeforeTonemap,
    AfterTonemap,
    SceneOutput,
    FinalOutput
};

/// A compiled post process: where it runs, and its color as the sum of
/// `constant`, `scene` times the picture's color, `texture` times the
/// texture's color (its alpha's in the fourth), and `both` times the two
/// multiplied, channel by channel; the picture's alpha is one, so it has
/// only `constant`'s and `texture`'s alpha. The texture is none when its
/// identity is nought, and then `texture` and `both` are nought; its scale
/// and offset map the picture's coordinates.
struct PostProcess {
    Insertion insertion = Insertion::AfterTonemap;
    std::array<float, 4> constant{0, 0, 0, 0};
    std::array<float, 3> scene{0, 0, 0};
    std::array<float, 4> texture{0, 0, 0, 0};
    std::array<float, 3> both{0, 0, 0};
    SampledTexture sampled;
    friend bool operator==(const PostProcess&, const PostProcess&) = default;
};

/// The cooked post process's resource type and representation (ADR-0024).
inline constexpr base::Bits128 kPostProcessType = base::parseBits128Hex("5b0e2a7c91d64f38a1e7c3d95f02b864").value;
inline constexpr std::string_view kPostProcessRepresentation = "rawframe.postprocess";

/// The domain's output, and the node library's types only it reads.
inline constexpr std::string_view kPostProcessOutputType = "rawframe/post_process@1";
inline constexpr std::string_view kSceneColorType = "rawframe/scene_color@1";
inline constexpr std::string_view kSceneDepthType = "rawframe/scene_depth@1";
inline constexpr std::string_view kScreenUvType = "rawframe/screen_uv@1";
inline constexpr std::string_view kCombine4Type = "rawframe/combine4@1";

/// Refuses (`Invalid`) a document out of the domain's contract: kind
/// `post_process`, an empty interface, one output node, its `color` a
/// color4 (a literal of finite numbers, its alpha from nought to one, left
/// out at nought), the insertion in its set, left out at its default; the
/// domain's nodes as the header has them, and each connection to one from
/// an output it has, of the input's type.
[[nodiscard]] result::Status validatePostProcess(const graph::Document& process, const graph::Limits& limits = {});
[[nodiscard]] result::Result<std::string> writePostProcess(const graph::Document& process,
                                                           const graph::Limits& limits = {});
/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<graph::Document> readPostProcess(std::string_view text, const graph::Limits& limits = {});

/// Folds the document as the header has it. Refuses (`Unsupported`) what
/// does not fold: a node of a type it does not know, the depth, a second
/// texture, a product past the form, the texture's alpha in a color.
[[nodiscard]] result::Result<PostProcess> compilePostProcess(const graph::Document& process);

/// A compiled post process's bytes: `RFPP`, format 1, its insertion (a
/// byte) and three of nought; its constant, scene, texture, and both, four
/// numbers each (scene's and both's fourth nought); then its texture's
/// identity (eight bytes), filter and address (a byte each), two of
/// nought, and its scale and offset (two numbers each).
[[nodiscard]] std::vector<std::byte> encodePostProcess(const PostProcess& made);
/// Refuses (`Invalid`) bytes `encodePostProcess` would not make.
[[nodiscard]] result::Result<PostProcess> decodePostProcess(std::span<const std::byte> bytes);

/// What a device reads of a post process: its constant, scene, texture,
/// and both (scene's and both's fourth nought), then the texture's scale
/// and offset. 80 bytes.
inline constexpr std::size_t kPostProcessBlobFloats = 20;
[[nodiscard]] std::array<float, kPostProcessBlobFloats> blobOf(const PostProcess& made) noexcept;

} // namespace rawframe::material
