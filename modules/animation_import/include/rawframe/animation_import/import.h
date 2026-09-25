#pragma once

// Importing animation (ADR-0039, SPEC-0035): a glTF 2.0 skin and its
// animations, as `.gltf` with embedded or sibling buffers or as `.glb`, made
// into one skeleton document and a clip document for each animation. The
// documents are the authority from then on: an author reviews them, the
// cook reads them (D125), and importing again is a new authoring event.
// Import tooling only: the parser here is trusted with an author's own
// files and never links into a client or a server.
//
// What is supported:
// - The first skin. Its joints become the bones, parents before children,
//   siblings in the glTF's child order. A joint's parent is a joint, but
//   for exactly one root; each bone's bind pose is its node's rest
//   transform, and its target is derived from its name path (SPEC-0035),
//   an unnamed joint named `joint<index>`.
// - What places the root (its parents that are not joints, and the scene)
//   is folded into the root's bind pose and into every key of the root's
//   tracks, so clips play in the skeleton's frame. That placement may not
//   scale unevenly.
// - Each animation's translation, rotation, and scale channels on joints,
//   with their LINEAR, STEP, or CUBICSPLINE keys. A cubic rotation is
//   turned by slerp between its values, since rotations are not cubic in a
//   clip. Channels on other nodes and morph weights are skipped, and
//   counted.
// - A clip lasts until its last key, holding its ends (`clamp`), or with
//   `loop` repeats, the keys at its end dropped, since a glTF loop repeats
//   its first key there. An animation of one instant lasts a thirtieth of a
//   second.
// - Required extensions: KHR_mesh_quantization only. Units and axes are
//   glTF's, which are Rawframe's (ADR-0046).

#include "rawframe/animation/clip.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::animation_import {

/// The bytes of a file a glTF names, at its path relative to the glTF,
/// alive until the import returns.
using ReadFile = std::function<result::Result<std::span<const std::byte>>(std::string_view path)>;

struct ImportSettings {
    /// The skeleton's resource identity, which every clip names.
    base::Bits128 skeleton;
    /// Clips repeat rather than hold their ends.
    bool loop = false;
    /// The source, and each buffer it reads, at most.
    std::size_t maximumBytes = std::size_t{256} << 20U;
    animation::SkeletonLimits skeletonLimits;
    animation::ClipLimits clipLimits;
};

/// An animation, by the glTF's name for it (`animation<index>` if it has
/// none).
struct ImportedClip {
    std::string name;
    animation::Clip clip;
};

struct ImportedAnimation {
    animation::Skeleton skeleton;
    /// In the glTF's order.
    std::vector<ImportedClip> clips;
    /// Channels on nodes that are not joints, and morph weights.
    std::size_t channelsSkipped = 0;
    /// Cubic rotation channels turned by slerp instead.
    std::size_t rotationsFlattened = 0;
};

/// Refuses (`BadSource`) what is not valid glTF 2.0, a buffer `read`
/// refuses or that is shorter than declared, a glTF with no skin, and a
/// rig the documents cannot hold; (`UnsupportedExtension`) a required
/// extension not supported; (`OverLimit`) a source past its limit; and
/// whatever the skeleton's or a clip's rules refuse.
[[nodiscard]] result::Result<ImportedAnimation>
importGltf(std::span<const std::byte> source, const ReadFile& read, const ImportSettings& settings);

} // namespace rawframe::animation_import
