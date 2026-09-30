#pragma once

// Surface materials (ADR-0031, SPEC-0026): graph documents of kind
// `surface` on SPEC-0028's grammar, one of them a `rawframe/surface@1`
// node, the domain's output, whose inputs are the Surface contract's core
// parameters. An input left unconnected holds a literal, and one at its
// default is left out:
//
//   {
//     "formatVersion": 1,
//     "kind": "surface",
//     "interface": {},
//     "graph": {
//       "2f00000000000002": {
//         "type": "rawframe/surface@1",
//         "params": {},
//         "inputs": {
//           "base_color": [0.8, 0.1, 0.1],
//           "specular_roughness": 0.5
//         }
//       }
//     },
//     "states": {
//       "blend": "masked",
//       "alpha_cutoff": 0.25
//     }
//   }
//
// (arrays shown on one line here). The core parameters and their defaults
// (OpenPBR's): `base_color` [0.8, 0.8, 0.8], `base_metalness` 0,
// `specular_weight` 1, `specular_color` [1, 1, 1], `specular_roughness`
// 0.3, `specular_ior` 1.5, `emission_color` [1, 1, 1], `emission_luminance`
// 0 (nits), `geometry_opacity` 1, `ambient_occlusion` 1. Colors are linear
// Rec.709, each channel from nought to one, as are the weights, the
// metalness, the roughness, the opacity, and the occlusion; the index of
// refraction is from one to three; luminance is nought or more.
// `geometry_normal`, a stream or a map, has no literal: it is connected or
// left out for the mesh's normal (D313).
//
// The optional `states` (SPEC-0026's declared material states, in this
// order, each left out at its default): `shading` `lit` or `unlit`;
// `blend` `opaque`, `masked`, or `translucent`; `alpha_cutoff` (0.5,
// masked only); `double_sided` (false).
//
// The standard node library's first two nodes (D308) feed it a texture:
//
//   "1f00000000000001": {
//     "type": "rawframe/sample_texture_2d@1",
//     "params": {
//       "filter": "nearest",
//       "texture": "a44ecb4a39ac5cc8"
//     },
//     "inputs": {
//       "uv": {"node": "1f00000000000003", "output": "uv"}
//     }
//   },
//   "1f00000000000003": {"type": "rawframe/uv@1", "params": {}, "inputs": {}}
//
// `sample_texture_2d` samples the game's texture that the game's `texture`
// line names by the 16 hex digits of `texture` (never nought), at the
// coordinates of its `uv` input, a mesh's first set when it is left out.
// Its other params are the declared sampler state, each left out at its
// default: `address` `repeat` or `clamp`, `filter` `linear` or `nearest`.
// Its outputs are `color` (color3) and `alpha` (float); the texture's own
// format says whether its texels are sRGB, so color is pipeline-owned, as
// SPEC-0026 has it. `uv` gives a mesh's texture coordinates, the set its
// `channel` names (0 to 7, left out at 0); its output is `uv` (vec2).
// `multiply` and `add` (D311) take inputs `a` and `b`, each connected or a
// literal: a number (float), two (vec2), or three (color3); their output
// `out` is of their inputs' type, or of the one that is not a float when
// the other is (broadcast). `separate3` (D312) takes a color3 `in` and
// gives its channels as `r`, `g`, and `b` (floats). `normal_map` (D313)
// takes a color3 `in`, a tangent-space normal as glTF encodes it, and its
// param `scale` (the glTF normal scale, left out at one); its `out` is a
// vec3. `quality_switch` (D318) takes `default` and any of `high`, `low`,
// and `medium`, each connected or a literal as `multiply`'s are, all of one
// type; its `out` is of that type, and at each quality it is the input for
// that quality, or `default` where it has none (SPEC-0026's quality axis).
// A node's params and inputs are in name order.
//
// A node of any other type is kept whole, as SPEC-0028 keeps what it does
// not know. Generation 1 compiles three textures at most (D312), each
// sampled at a mesh's first coordinates, or at them scaled and moved by
// `multiply` and `add` with literals (SPEC-0026's `tile_and_offset`,
// folded), and each feeding its inputs directly or multiplied by a literal
// (the glTF factor): the base texture, its color `base_color` and its alpha
// `geometry_opacity`; the packed texture, a channel each (`separate3`'s,
// or its alpha) for `base_metalness`, `specular_roughness`, and
// `ambient_occlusion`; the emission texture, its color `emission_color`;
// the normal texture, through `normal_map`, `geometry_normal`.
// Any other connection, or a node of a type it does not know, does not
// compile until the rest of the library exists.

#include "rawframe/base/sha256.h"
#include "rawframe/graph/graph.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::material {

/// SPEC-0026's Surface contract, version 1: the core parameters.
struct Surface {
    std::array<float, 3> baseColor{0.8F, 0.8F, 0.8F};
    float baseMetalness = 0;
    float specularWeight = 1;
    std::array<float, 3> specularColor{1, 1, 1};
    float specularRoughness = 0.3F;
    float specularIor = 1.5F;
    std::array<float, 3> emissionColor{1, 1, 1};
    /// Nits.
    float emissionLuminance = 0;
    float geometryOpacity = 1;
    float ambientOcclusion = 1;

    friend bool operator==(const Surface&, const Surface&) = default;
};

enum class Shading : std::uint8_t {
    Lit,
    Unlit
};

enum class Blend : std::uint8_t {
    Opaque,
    Masked,
    Translucent
};

enum class Filter : std::uint8_t {
    Linear,
    Nearest
};

enum class Address : std::uint8_t {
    Repeat,
    Clamp
};

/// A texture a material samples (D308): the game's texture by the
/// identity its `texture` line gives it (none when nought), its declared
/// sampler state, and where it is sampled: a mesh's first coordinates
/// times `scale`, plus `offset` (D311).
struct SampledTexture {
    std::uint64_t id = 0;
    Filter filter = Filter::Linear;
    Address address = Address::Repeat;
    std::array<float, 2> scale{1, 1};
    std::array<float, 2> offset{0, 0};

    friend bool operator==(const SampledTexture&, const SampledTexture&) = default;
};

/// The channel of a texture a number is read from; none for an input no
/// texture feeds.
enum class Channel : std::uint8_t {
    None,
    Red,
    Green,
    Blue,
    Alpha
};

/// The textures a material samples and what each feeds (D312, D313).
struct Textures {
    /// Its color the base color, its alpha the opacity.
    SampledTexture base;
    bool baseColor = false;
    bool opacity = false;
    /// A channel each for the metalness, the roughness, and the occlusion
    /// (glTF packs them blue, green, and red).
    SampledTexture packed;
    Channel metalness = Channel::None;
    Channel roughness = Channel::None;
    Channel occlusion = Channel::None;
    /// Its color the emission's color.
    SampledTexture emission;
    /// A tangent-space normal, its x and y times `normalScale`.
    SampledTexture normal;
    float normalScale = 1;

    friend bool operator==(const Textures&, const Textures&) = default;
};

/// SPEC-0026's closed quality axis: which of a `quality_switch`'s inputs a
/// material is compiled with. Which applies is the process's to say.
enum class Quality : std::uint8_t {
    Low,
    Medium,
    High
};

/// A compiled material: its declared states, its Surface, and the textures
/// it samples. An input a texture feeds holds what the texture is
/// multiplied by in `surface`, one when nothing (D311).
struct Material {
    Shading shading = Shading::Lit;
    Blend blend = Blend::Opaque;
    float alphaCutoff = 0.5F;
    bool doubleSided = false;
    Surface surface;
    Textures textures;

    friend bool operator==(const Material&, const Material&) = default;
};

/// The cooked material's resource type and representation (ADR-0024): a
/// compiled `Material`, little-endian, which a runtime decodes without the
/// document.
inline constexpr base::Bits128 kMaterialType = base::parseBits128Hex("06e95486decf73eaeea9ad8ca0cc02a8").value;
inline constexpr std::string_view kMaterialRepresentation = "rawframe.material";

/// The one type a surface material's output is, and the node library's
/// types this family knows.
inline constexpr std::string_view kSurfaceType = "rawframe/surface@1";
inline constexpr std::string_view kSampleTexture2dType = "rawframe/sample_texture_2d@1";
inline constexpr std::string_view kUvType = "rawframe/uv@1";
inline constexpr std::string_view kMultiplyType = "rawframe/multiply@1";
inline constexpr std::string_view kAddType = "rawframe/add@1";
inline constexpr std::string_view kSeparate3Type = "rawframe/separate3@1";
inline constexpr std::string_view kNormalMapType = "rawframe/normal_map@1";
inline constexpr std::string_view kQualitySwitchType = "rawframe/quality_switch@1";

/// What a surface compiles to at each quality, by `Quality`: the same
/// material at each where its graph does not differ (D318).
using Qualities = std::array<Material, 3>;

/// A surface material's document for `made`, its surface node keyed by
/// `node` and the nodes its textures need by the ids after it, in order,
/// each only where it is needed.
[[nodiscard]] graph::Document documentOf(const Material& made, graph::NodeId node);

/// Refuses (`Invalid`) a document out of the family's rules: kind
/// `surface`, an empty interface, one surface node, its inputs the core
/// parameters in order with literals in range and none at its default, no
/// params, and states in their sets, in order, none at its default; the
/// node library's nodes as the header has them, and each connection to one
/// from an output it has, of the input's type.
[[nodiscard]] result::Status validateSurface(const graph::Document& surface, const graph::Limits& limits = {});

[[nodiscard]] result::Result<std::string> writeMaterial(const graph::Document& surface,
                                                        const graph::Limits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<graph::Document> readMaterial(std::string_view text, const graph::Limits& limits = {});

/// Its states, Surface, and texture at `quality`, each quality switch its
/// input for it: every literal, and the default where one is left out
/// (then or by the switch). Refuses (`Unsupported`) what generation 1 does not
/// compile: a node of a type it does not know, or a connection besides the
/// header's; (`Invalid`) a factor out of its input's range.
[[nodiscard]] result::Result<Material> compile(const graph::Document& surface, Quality quality = Quality::High);
/// The surface compiled at each quality, as `compile` refuses it.
[[nodiscard]] result::Result<Qualities> compileQualities(const graph::Document& surface);

/// SPEC-0028's semantic hash: from the surface node down, and the states,
/// blind to ids and drawings. Refuses (`Unsupported`) a document holding a
/// node of a type this family does not know.
[[nodiscard]] result::Result<base::Sha256Digest> semanticHash(const graph::Document& surface);

/// A compiled material's bytes: `RFMT`, format 6, then its material at the
/// high quality: its states (shading,
/// blend, double sided, a byte each and one of nought), its alpha cutoff,
/// its Surface's sixteen numbers in the contract's order, then its base,
/// packed, emission, and normal textures, each its identity (eight bytes),
/// filter, and address (a byte each), two bytes of nought, and its scale
/// and offset (two numbers each); then what they feed, a byte each: the
/// base color, the opacity, the metalness's channel, the roughness's, the
/// occlusion's, and three of nought; then the normal's scale. Then a byte,
/// one for a low material that differs and two for a medium one, three of
/// nought, and each that differs, low first, as the high one is (D318).
[[nodiscard]] std::vector<std::byte> encode(const Qualities& made);
/// A material the same at every quality.
[[nodiscard]] std::vector<std::byte> encode(const Material& made);

/// Refuses (`Invalid`) bytes `encode` would not make, or a material a
/// document could not compile to: cooked content is checked as it is read.
[[nodiscard]] result::Result<Qualities> decode(std::span<const std::byte> bytes);

/// ADR-0031's typed data blob, what a device reads of a material: the base
/// color and metalness; the specular color times its weight, and the
/// roughness; the emission's color times its luminance, and the index of
/// refraction; the opacity, the occlusion, the alpha cutoff (nought unless
/// masked), and its flags: one when unlit, two when its base texture's
/// color multiplies the base color, four when its alpha multiplies the
/// opacity, eight when the emission texture's color multiplies the
/// emission, sixteen when the normal texture bends the normal; the base,
/// packed, emission, and normal textures' scale and offset (D311); and the
/// channels (one to four, red to alpha; nought for none) of the packed
/// texture multiplying the metalness, the roughness, and the occlusion,
/// then the normal's scale (D312, D313). 144 bytes.
inline constexpr std::size_t kBlobFloats = 36;
[[nodiscard]] std::array<float, kBlobFloats> blobOf(const Material& made) noexcept;

} // namespace rawframe::material
