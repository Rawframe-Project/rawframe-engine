#pragma once

// ADR-0023's product identities as text, where a Build or a Composition
// names what it is: a `publisher/name` subject and a Semantic Versioning
// 2.0.0 version. Publishing and display identities only: resources are
// still named by their 128-bit identities (ADR-0013).

#include <string_view>

namespace rawframe::content {

/// Whether `text` is a `publisher/name` subject, each segment
/// `[a-z0-9]([a-z0-9-]*[a-z0-9])?`.
[[nodiscard]] bool validSubject(std::string_view text) noexcept;

/// The subject's publisher segment: all of it before the slash.
[[nodiscard]] std::string_view publisherOf(std::string_view subject) noexcept;

/// Whether `text` is a Semantic Versioning 2.0.0 version of at most 64
/// bytes.
[[nodiscard]] bool validVersion(std::string_view text) noexcept;

} // namespace rawframe::content
