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
// `geometry_normal`, a stream or a map, has no literal.
//
// The optional `states` (SPEC-0026's declared material states, in this
// order, each left out at its default): `shading` `lit` or `unlit`;
// `blend` `opaque`, `masked`, or `translucent`; `alpha_cutoff` (0.5,
// masked only); `double_sided` (false).
//
// A node of any other type is kept whole, as SPEC-0028 keeps what it does
// not know, and a material holding one, or a connected input, does not
// compile until the standard node library exists: generation 1 compiles
// the literal surface alone.

#include "rawframe/base/sha256.h"
#include "rawframe/graph/graph.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

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

/// A compiled material: its declared states and its Surface.
struct Material {
    Shading shading = Shading::Lit;
    Blend blend = Blend::Opaque;
    float alphaCutoff = 0.5F;
    bool doubleSided = false;
    Surface surface;

    friend bool operator==(const Material&, const Material&) = default;
};

/// The one type a surface material's output is.
inline constexpr std::string_view kSurfaceType = "rawframe/surface@1";

/// A surface material's document for `made`, its surface node keyed by
/// `node`.
[[nodiscard]] graph::Document documentOf(const Material& made, graph::NodeId node);

/// Refuses (`Invalid`) a document out of the family's rules: kind
/// `surface`, an empty interface, one surface node, its inputs the core
/// parameters in order with literals in range and none at its default, no
/// params, and states in their sets, in order, none at its default.
[[nodiscard]] result::Status validateSurface(const graph::Document& surface, const graph::Limits& limits = {});

[[nodiscard]] result::Result<std::string> writeMaterial(const graph::Document& surface,
                                                        const graph::Limits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<graph::Document> readMaterial(std::string_view text, const graph::Limits& limits = {});

/// Its states and Surface: every literal, and the default where one is left
/// out. Refuses (`Unsupported`) a document holding another node or a
/// connected input.
[[nodiscard]] result::Result<Material> compile(const graph::Document& surface);

/// SPEC-0028's semantic hash: the surface node's inputs, the states, blind
/// to ids and drawings.
[[nodiscard]] result::Result<base::Sha256Digest> semanticHash(const graph::Document& surface);

/// ADR-0031's typed data blob, what a device reads of a material: the base
/// color and metalness; the specular color times its weight, and the
/// roughness; the emission's color times its luminance, and the index of
/// refraction; the opacity, the occlusion, the alpha cutoff (nought unless
/// masked), and whether it is unlit (one) or lit (nought).
inline constexpr std::size_t kBlobFloats = 16;
[[nodiscard]] std::array<float, kBlobFloats> blobOf(const Material& made) noexcept;

} // namespace rawframe::material
