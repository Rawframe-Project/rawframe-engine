#pragma once

// A cooked surface material as the scene draws it: the one
// reading of a material's cooked bytes, which a scene loading its game's
// content and a tool drawing a material's thumbnail share.

#include "rawframe/base/bits128.h"
#include "rawframe/content/identity.h"
#include "rawframe/material/material.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rawframe::render_scene {

/// A program material read, with the revision it was read from: one
/// program however many instances draw with it, so they share its
/// pipelines, and one a reload finds unchanged kept, so it keeps them.
struct ReadProgram {
    content::ContentDigest revision;
    std::shared_ptr<const material::ProgramMaterial> program;
};
/// The program materials read so far, by their resources' identities.
using Programs = std::map<base::Bits128, ReadProgram>;

/// A material's texture as the scene binds it.
[[nodiscard]] SceneTexture sceneTextureOf(const material::SampledTexture& texture) noexcept;

/// A material's cooked bytes, the representation they are in, and the
/// revision (the cook's digest of them) they were read at.
struct CookedMaterial {
    std::string representation;
    content::ContentDigest revision;
    std::vector<std::byte> bytes;
};

/// Reads the cooked material of a resource identity: what an instance's
/// base is read through.
using CookedMaterials = std::function<result::Result<CookedMaterial>(base::Bits128 id)>;

/// The surface material `cooked`, of resource `id`, as the scene draws it
/// under `identity`: its blob at `quality`, or, cooked with its own
/// program, that program, its states in the blob and its textures at
/// their slots, the program kept in `programs`; or, an instance,
/// its base's read through `bases`, its parameters and texture
/// overrides the instance's. An instance's base is no instance.
[[nodiscard]] result::Result<SceneMaterial> sceneMaterialOf(std::uint64_t identity,
                                                            base::Bits128 id,
                                                            const CookedMaterial& cooked,
                                                            material::Quality quality,
                                                            Programs& programs,
                                                            const CookedMaterials& bases);

} // namespace rawframe::render_scene
