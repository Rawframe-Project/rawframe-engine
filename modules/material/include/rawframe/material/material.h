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
// left out for the mesh's normal (D313). The coat's feature group
// (`KHR_materials_clearcoat`, D716): `coat_weight` and `coat_roughness`,
// each from nought to one, nought by default, and `coat_normal`, connected
// or left out for the mesh's normal. The anisotropy's
// (`KHR_materials_anisotropy`, D717): `specular_roughness_anisotropy`,
// from nought to one, and `specular_anisotropy_rotation`, radians from the
// mesh's tangent toward its bitangent, each nought by default. A material
// whose coat or anisotropy is given is a program material.
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
/// A material with its own program (D484): the representation of a
/// material whose graph the blob cannot fold, cooked with the scene's
/// containers linked with what `generateSlang` wrote.
inline constexpr std::string_view kProgramRepresentation = "rawframe.material.program";

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
/// The vertex stage's and the streams' nodes (D691): the time on a named
/// timeline, the point's place and normal in the World (D694), and the
/// math that moves it; the rest of SPEC-0026's math is surface_graph's
/// table (D694).
inline constexpr std::string_view kTimeType = "rawframe/time@1";
inline constexpr std::string_view kWorldPositionType = "rawframe/world_position@1";
inline constexpr std::string_view kWorldNormalType = "rawframe/world_normal@1";
inline constexpr std::string_view kSubtractType = "rawframe/subtract@1";
inline constexpr std::string_view kSinType = "rawframe/sin@1";
inline constexpr std::string_view kCosType = "rawframe/cos@1";
inline constexpr std::string_view kNormalizeType = "rawframe/normalize@1";
inline constexpr std::string_view kCombine3Type = "rawframe/combine3@1";
/// The rest of the channel nodes and the vertex's color stream (D695).
inline constexpr std::string_view kCombine2Type = "rawframe/combine2@1";
inline constexpr std::string_view kSeparate2Type = "rawframe/separate2@1";
inline constexpr std::string_view kSwizzleType = "rawframe/swizzle@1";
inline constexpr std::string_view kVertexColorType = "rawframe/vertex_color@1";
/// The way to the eye, a unit vector, and the surface's tangent in the
/// World's axes, the fragment stage's alone (D696).
inline constexpr std::string_view kViewDirectionType = "rawframe/view_direction@1";
inline constexpr std::string_view kWorldTangentType = "rawframe/world_tangent@1";
/// A comparison of two floats, by its declared operator (D697).
inline constexpr std::string_view kCompareType = "rawframe/compare@1";
/// A typed constant (D704).
inline constexpr std::string_view kConstantType = "rawframe/constant@1";
/// Whether the face drawn is the front, the fragment stage's alone (D705).
inline constexpr std::string_view kFrontFacingType = "rawframe/front_facing@1";
/// An interface input's value, by the name its `name` param gives (D708),
/// and a texture input's texture, which a sampled texture samples (D710).
inline constexpr std::string_view kParameterType = "rawframe/parameter@1";
inline constexpr std::string_view kParameterTexture2dType = "rawframe/parameter_texture_2d@1";
/// What feeds one of a function's outputs, by the name its `name` param
/// gives (D711).
inline constexpr std::string_view kOutputType = "rawframe/output@1";
/// The four-wide channel nodes, combine4 the post-process and canvas
/// families' too (D348, D706).
inline constexpr std::string_view kCombine4Type = "rawframe/combine4@1";
inline constexpr std::string_view kSeparate4Type = "rawframe/separate4@1";
/// A point, a vector, or a normal taken from one space to another: the
/// model's, the World's, the eye's, the surface's tangent frame, or the
/// clip's (D714).
inline constexpr std::string_view kTransformType = "rawframe/transform@1";
/// SPEC-0026's inline-Slang node (D718): one function's body, its typed
/// inputs and outputs, and the stages, domains, and capabilities it
/// declares; a trusted package's alone.
inline constexpr std::string_view kInlineSlangType = "rawframe/inline_slang@1";
/// One of two inputs by a static switch the interface declares, chosen at
/// the cook: each switch an axis of the material's variants (D719).
inline constexpr std::string_view kStaticSwitchType = "rawframe/static_switch@1";

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

/// The trust class of the package a material comes from (ADR-0023,
/// ADR-0027): a first-party or explicitly trusted package's, or another's.
enum class Trust : std::uint8_t {
    Trusted,
    Untrusted
};

/// Refuses (`Untrusted`) a material its package's trust class may not
/// hold: an untrusted package's with an inline-Slang node (SPEC-0026,
/// D718). A game's own materials are trusted; whoever reads another
/// package's checks it here before cooking or opening it.
[[nodiscard]] result::Status admitted(const graph::Document& material, Trust trust);

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

/// The numbers a material's interface inputs hold, a vector of four each
/// (SPEC-0028's interface, D708): eight inputs at most, in name order,
/// each in its own vector, its channels past its type's nought.
inline constexpr std::size_t kParameterFloats = 32;

/// A function document (D711), read and checked as the engine's own
/// functions are: canonical, kind `function`, its interface's inputs (a
/// literal's default or a stream's) and one to four typed outputs, and
/// nodes of the atomic core but the surface, the quality switch, and the
/// textures, never a call, one output node each output, typed as it says.
/// Refuses (`Invalid`) any other.
[[nodiscard]] result::Result<graph::Document> readFunction(std::string_view text);

/// A surface material written as Slang (ADR-0026, D481 to D483): a module
/// importing the scene's `material` module and exporting `Material`, a
/// struct meeting its `IMaterial`, which the scene is linked with in place
/// of the blob's. Each node output is computed once, in graph order; a
/// texture is bound at the slots in the order it is first sampled, base,
/// packed, emission, and normal, `textures` naming them.
struct GeneratedSlang {
    std::string source;
    std::vector<SampledTexture> textures;
    /// Whether its vertices move: its position offset is connected (D691).
    bool moves = false;
    /// Whether it transmits what is behind it: its transmission weight is
    /// given (D720).
    bool transmits = false;
    /// The material's declared states, which its program does not say.
    Shading shading = Shading::Lit;
    Blend blend = Blend::Opaque;
    float alphaCutoff = 0.5F;
    bool doubleSided = false;
    /// Its interface inputs' defaults (D708).
    std::array<float, kParameterFloats> parameters{};
    /// Which interface input gives each texture slot's texture, one more
    /// than the input's slot, nought for a fixed texture (D710).
    std::array<std::uint8_t, 4> textureInputs{};
};

/// The surface written as Slang at a quality, for a graph the blob cannot
/// fold. Refuses (`Unsupported`) a node of a type this family does not
/// know, a mesh's coordinates past its first, more than four textures, and
/// a texture, a normal map (D691), or a tangent (D696) in the vertex stage;
/// (`Invalid`) what `validateSurface` refuses.
[[nodiscard]] result::Result<GeneratedSlang> generateSlang(const graph::Document& surface,
                                                           Quality quality = Quality::High);

/// The backends a program material carries a container for, in its order:
/// Vulkan and WebGPU's, Metal's, Direct3D 12's (D416).
inline constexpr std::size_t kProgramContainers = 3;

/// A program material, cooked (D484): its declared states (its Surface and
/// textures are its program's), the textures bound at the slots in order,
/// and the scene's containers linked with its program, one a backend; and
/// the shadow's, which its masked casters are cut by (D488).
struct ProgramMaterial {
    Shading shading = Shading::Lit;
    Blend blend = Blend::Opaque;
    float alphaCutoff = 0.5F;
    bool doubleSided = false;
    /// Whether its vertices move (D691): its own program then places them
    /// in every pass, the depth prepass's and the shadows' too.
    bool moves = false;
    /// Whether it transmits what is behind it (D720): drawn after the
    /// opaque models, reading a copy of what they made.
    bool transmits = false;
    /// Its interface inputs' values (D708), which an instance overrides.
    std::array<float, kParameterFloats> parameters{};
    std::vector<SampledTexture> textures;
    /// Which interface input gives each texture's, as GeneratedSlang's
    /// (D710): an instance's override of that input replaces it.
    std::array<std::uint8_t, 4> textureInputs{};
    std::array<std::vector<std::byte>, kProgramContainers> containers;
    std::array<std::vector<std::byte>, kProgramContainers> shadows;

    friend bool operator==(const ProgramMaterial&, const ProgramMaterial&) = default;
};

/// A program material's bytes: `RFMP`, format 5 (D488, D708, D710, D720);
/// its states (shading, blend, double sided, a byte each, and its flags,
/// one where its vertices move, two where it transmits) and alpha
/// cutoff; its parameters' numbers; the count of its textures, then each
/// one's identity (low word first), filter, address, the interface input
/// giving it (one more than its slot, nought for none), and a byte of
/// nought; then each scene container's length and bytes, then each shadow
/// container's. Little-endian. Formats 1 to 4, which carried no shadow's,
/// no parameters, no texture inputs, or no flag of transmission, are read
/// no more: a cook's key changes with its toolchain, so what was cooked so
/// is cooked again.
[[nodiscard]] std::vector<std::byte> encodeProgram(const ProgramMaterial& made);
/// Refuses (`Invalid`) bytes `encodeProgram` would not write: states out of
/// their sets, more than four textures or one naming none, a container
/// empty or not a Maul RHI container (`MRSC`), or bytes left over.
[[nodiscard]] result::Result<ProgramMaterial> decodeProgram(std::span<const std::byte> bytes);

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
