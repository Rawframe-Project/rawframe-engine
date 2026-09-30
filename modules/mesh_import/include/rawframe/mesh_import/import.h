#pragma once

// Importing meshes (ADR-0058): glTF 2.0, as `.gltf` with embedded or sibling
// buffers or as `.glb`, cooked into the runtime's mesh. Import tooling only:
// the parser here is trusted with an author's own files and never links
// into a client or a server.
//
// What is supported, required or not:
// - The default scene (or the first), every node's meshes placed by its
//   world transform, one part per primitive in scene order. A transform
//   that mirrors turns its triangles so they still face out.
// - Triangles, triangle strips, and triangle fans, indexed or not; points
//   and lines are refused.
// - POSITION, and NORMAL and TEXCOORD_0 when every primitive has them;
//   either missing anywhere is dropped everywhere. Sparse accessors.
// - Each primitive's material (D314), glTF's metallic-roughness: the base
//   color's factor and texture, the alpha mode and cutoff, the metallic and
//   roughness factors and texture, occlusion from that texture's red (or
//   from its own texture when there is none) at strength one, the emissive
//   factor and texture, with KHR_materials_emissive_strength, the normal
//   texture and its scale, double sidedness, KHR_materials_unlit, and
//   KHR_texture_transform's offset and scale. Textures are sampled at the
//   first coordinates, nearest or linear, repeated or clamped on both
//   axes alike. A material asking for any other of these is refused
//   rather than drawn differently; the other extensions a material uses
//   are optional and ignored.
// - Required extensions: KHR_mesh_quantization, KHR_materials_unlit,
//   KHR_materials_emissive_strength, and KHR_texture_transform. Any other
//   required extension is refused by name.
// - Skins and morph targets are ignored: the rest pose is cooked.
// - Units and axes are glTF's, which are Rawframe's (ADR-0046): nothing is
//   converted.

#include "rawframe/material/material.h"
#include "rawframe/mesh/mesh.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::mesh_import {

/// The bytes of a file a glTF names, at its path relative to the glTF,
/// alive until the import returns.
using ReadFile = std::function<result::Result<std::span<const std::byte>>(std::string_view path)>;

/// The identity the game knows a subasset by, by its key (D314): given by
/// whoever authors the source's sidecar, never nought.
using Identify = std::function<result::Result<std::uint64_t>(std::string_view key)>;

/// A material the glTF's primitives are drawn with: its subasset key,
/// `material/` and the material's name, and what it compiles to, naming
/// its textures by their identities.
struct ImportedMaterial {
    std::string key;
    material::Material made;

    friend bool operator==(const ImportedMaterial&, const ImportedMaterial&) = default;
};

/// An image those materials sample: its subasset key, `texture/` and the
/// image's name (its URI when it has none), the image as the glTF holds
/// it, and whether its texels are colors (sRGB) or data (linear).
struct ImportedTexture {
    std::string key;
    std::vector<std::byte> image;
    bool color = true;

    friend bool operator==(const ImportedTexture&, const ImportedTexture&) = default;
};

/// What a glTF makes: its mesh, each part naming its material's identity,
/// and the materials and textures that draws with, each in key order.
struct Imported {
    mesh::Mesh mesh;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedTexture> textures;
};

struct ImportLimits {
    mesh::MeshLimits mesh;
    /// The source, and each buffer it reads, at most.
    std::size_t maximumBytes = std::size_t{256} << 20U;
};

/// The required extensions supported, by name.
[[nodiscard]] std::span<const std::string_view> supportedExtensions() noexcept;

/// Refuses (`BadSource`) what is not valid glTF 2.0, a buffer or image
/// `read` refuses or that is shorter than declared, and geometry that is
/// not triangles; (`UnsupportedExtension`) a required extension not
/// supported; (`UnsupportedMaterial`) a material as the header has it, one
/// with no name or a name two share, an image with neither, and textures on
/// a mesh without coordinates; and (`OverLimit`) a source or mesh past the
/// limits. `identify`'s refusals are its own.
[[nodiscard]] result::Result<Imported> importGltf(std::span<const std::byte> source,
                                                  const ReadFile& read,
                                                  const Identify& identify,
                                                  const ImportLimits& limits = {});

} // namespace rawframe::mesh_import
