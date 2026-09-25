#pragma once

// SPEC-0008's identities: a resource, what kind of resource it is, how its
// bytes are encoded, and which exact bytes. None is interchangeable with
// another, or with a path.

#include "rawframe/base/bits128.h"
#include "rawframe/base/sha256.h"

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::content {

/// One logical resource, stable across renames, moves, and revisions of its
/// bytes. Zero is not a resource.
struct ResourceId {
    base::Bits128 value;
    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != base::Bits128{};
    }
    friend constexpr auto operator<=>(const ResourceId&, const ResourceId&) noexcept = default;
};

/// What a resource means, not how it is encoded. Zero is not a type.
struct ResourceTypeId {
    base::Bits128 value;
    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != base::Bits128{};
    }
    friend constexpr auto operator<=>(const ResourceTypeId&, const ResourceTypeId&) noexcept = default;
};

/// How a resource's bytes are read: a lowercase dotted identifier such as
/// `rawframe.audio.opus`.
class RepresentationId {
public:
    /// Lowercase letters and digits in two or more dot-separated words, each
    /// beginning with a letter, at most 64 characters; none otherwise.
    [[nodiscard]] static std::optional<RepresentationId> parse(std::string_view text);

    [[nodiscard]] std::string_view text() const noexcept {
        return text_;
    }
    friend auto operator<=>(const RepresentationId&, const RepresentationId&) = default;

private:
    explicit RepresentationId(std::string text) noexcept : text_(std::move(text)) {
    }
    std::string text_;
};

enum class DigestAlgorithm : std::uint8_t {
    Sha256 = 1,
};

/// Exactly which bytes: SHA-256, the one algorithm accepted so far.
struct ContentDigest {
    DigestAlgorithm algorithm = DigestAlgorithm::Sha256;
    base::Sha256Digest bytes{};
    friend constexpr bool operator==(const ContentDigest&, const ContentDigest&) noexcept = default;

    [[nodiscard]] static ContentDigest of(std::span<const std::byte> content) noexcept;
    /// `sha256:` and 64 lowercase hexadecimal digits; none otherwise.
    [[nodiscard]] static std::optional<ContentDigest> parse(std::string_view text) noexcept;
    [[nodiscard]] std::string text() const;
};

/// Compares without an early exit, for digests an attacker may have chosen.
[[nodiscard]] bool sameDigest(const ContentDigest& left, const ContentDigest& right) noexcept;

/// A resource with the type its user expects: resolving it checks the type
/// before any byte is read.
struct ResourceRef {
    ResourceId id;
    ResourceTypeId type;
};

/// A resource at exactly one revision: resolving it refuses any other.
struct PinnedResourceRef {
    ResourceRef resource;
    ContentDigest digest;
};

} // namespace rawframe::content
