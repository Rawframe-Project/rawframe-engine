#pragma once

// Masks (SPEC-0035): which bones of a skeleton a part of a graph owns, as
// a document people and tools review, in one canonical form:
//
//   {
//     "formatVersion": 1,
//     "kind": "animation.mask",
//     "skeleton": "52771075251e7361deaecf4939c72e56",
//     "chains": [
//       {
//         "root": "3c1f0a9e5b7d2468ace013579bdf2468",
//         "descendants": true,
//         "weight": 1
//       }
//     ]
//   }
//
// Each chain names a bone by its target, whether the bones below it are in
// the chain too, and how much the chain counts, from over nought to one.
// Chains are in order of their root, each root once. A bone takes the
// weight of the nearest chain at or above it that reaches it; a bone no
// chain reaches weighs nought.

#include "rawframe/animation/skeleton.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::animation {

struct MaskChain {
    base::Bits128 root;
    bool descendants = true;
    double weight = 1.0;

    friend bool operator==(const MaskChain&, const MaskChain&) = default;
};

struct Mask {
    /// The skeleton's resource identity.
    base::Bits128 skeleton;
    std::vector<MaskChain> chains;

    friend bool operator==(const Mask&, const Mask&) = default;
};

/// SPEC-0035's named limit point for masks; past it is `OverLimit`.
struct MaskLimits {
    std::size_t maximumChains = 256;
};

/// Refuses (`MaskInvalid`) a mask out of its rules.
[[nodiscard]] result::Status validate(const Mask& mask, const MaskLimits& limits = {});

[[nodiscard]] result::Result<std::string> writeMask(const Mask& mask, const MaskLimits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<Mask> readMask(std::string_view text, const MaskLimits& limits = {});

/// Each bone's weight, in the skeleton's order. Refuses (`BindingInvalid`)
/// a mask of another skeleton, or naming a bone the skeleton lacks.
[[nodiscard]] result::Result<std::vector<double>>
boneWeights(const Mask& mask, const Skeleton& skeleton, base::Bits128 skeletonId);

/// SPEC-0035's server bone subset: for each bone, in the skeleton's order,
/// 1 when the mask weighs it or a bone below it, since a bone is placed by
/// every bone above it, and 0 otherwise. Refused as `boneWeights` is.
[[nodiscard]] result::Result<std::vector<std::uint8_t>>
boneSubset(const Mask& mask, const Skeleton& skeleton, base::Bits128 skeletonId);

} // namespace rawframe::animation
