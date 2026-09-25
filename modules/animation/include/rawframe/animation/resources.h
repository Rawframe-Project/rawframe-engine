#pragma once

// The animation documents as content (ADR-0013, the `.rfanim` family): a
// resource type for each kind, each with one representation, the
// document's text in its one form, which the runtime reads as it would the
// source.

#include "rawframe/base/bits128.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::animation {

inline constexpr base::Bits128 kSkeletonType = base::parseBits128Hex("9e29456fc61322244a4e2d92279a1c78").value;
inline constexpr std::string_view kSkeletonRepresentation = "rawframe.animation.skeleton";
inline constexpr base::Bits128 kClipType = base::parseBits128Hex("b9e95e43b18befeb184cc5ee605af34a").value;
inline constexpr std::string_view kClipRepresentation = "rawframe.animation.clip";
inline constexpr base::Bits128 kGraphType = base::parseBits128Hex("f36bb4007f7e69033303c765ee51637f").value;
inline constexpr std::string_view kGraphRepresentation = "rawframe.animation.graph";
inline constexpr base::Bits128 kMaskType = base::parseBits128Hex("5d0a7e3c91b24f68a0c3e7d1942b6f85").value;
inline constexpr std::string_view kMaskRepresentation = "rawframe.animation.mask";

enum class DocumentKind : std::uint8_t {
    Skeleton,
    Clip,
    Graph,
    Mask,
};

/// Which animation document a text says it is, by its `kind`; none for
/// text that is not JSON or names another kind. Says nothing of whether
/// the rest reads.
[[nodiscard]] std::optional<DocumentKind> documentKind(std::string_view text);

} // namespace rawframe::animation
