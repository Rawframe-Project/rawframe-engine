#pragma once

// Skeletons (SPEC-0035): an ordered bone hierarchy, each bone with a stable
// target identity and a bind pose, as a document people and tools review
// as text. One canonical form (SPEC-0028's authored-document profile; its
// arrays of numbers shown here on one line, where the form has one a line):
//
//   {
//     "formatVersion": 1,
//     "kind": "animation.skeleton",
//     "bones": [
//       {
//         "target": "3c1f0a9e5b7d2468ace013579bdf2468",
//         "name": "hips",
//         "translation": [0, 1, 0],
//         "rotation": [0, 0, 0, 1],
//         "scale": [1, 1, 1]
//       },
//       {
//         "target": "7e2b4d6f8091a3c5e7f9b1d3f5a7c9e1",
//         "name": "spine",
//         "parent": 0,
//         ...
//       }
//     ]
//   }
//
// The first bone is the one root and every other names a parent before it,
// so the order is the hierarchy's and nothing sorts it at runtime. A bind
// pose is in ADR-0046's units: meters, and a unit quaternion (x, y, z, w).
// A target is 32 lowercase hex digits, unique in the skeleton; its name is
// for people and diagnostics, never looked up at runtime. The skeleton's
// own identity is the resource identity of its document.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::animation {

/// A bone's place in its skeleton's order (SPEC-0035's skeleton space).
struct BoneIndex {
    std::uint32_t value = 0;

    friend constexpr auto operator<=>(const BoneIndex&, const BoneIndex&) noexcept = default;
};

/// A local transform: translation, rotation, then scale.
struct Transform {
    std::array<double, 3> translation{};
    std::array<double, 4> rotation{0.0, 0.0, 0.0, 1.0};
    std::array<double, 3> scale{1.0, 1.0, 1.0};

    friend bool operator==(const Transform&, const Transform&) = default;
};

struct Bone {
    base::Bits128 target;
    std::string name;
    /// None for the root only.
    std::optional<BoneIndex> parent;
    Transform bind;

    friend bool operator==(const Bone&, const Bone&) = default;
};

struct Skeleton {
    std::vector<Bone> bones;

    /// The bone of a target, if the skeleton has it.
    [[nodiscard]] std::optional<BoneIndex> find(base::Bits128 target) const noexcept;

    friend bool operator==(const Skeleton&, const Skeleton&) = default;
};

/// SPEC-0035's named limit point for skeletons; past it is `OverLimit`.
struct SkeletonLimits {
    std::size_t maximumBones = 1024;
};

/// A target's identity, derived once, at import, from the authoring name
/// path from the root to the bone (SPEC-0035). A skeleton keeps it through
/// a later rename; nothing derives it again at runtime.
[[nodiscard]] base::Bits128 targetIdOf(std::span<const std::string_view> namePath);

/// Refuses (`SkeletonInvalid`) a skeleton out of its rules.
[[nodiscard]] result::Status validate(const Skeleton& skeleton, const SkeletonLimits& limits = {});

[[nodiscard]] result::Result<std::string> writeSkeleton(const Skeleton& skeleton, const SkeletonLimits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<Skeleton> readSkeleton(std::string_view text, const SkeletonLimits& limits = {});

} // namespace rawframe::animation
