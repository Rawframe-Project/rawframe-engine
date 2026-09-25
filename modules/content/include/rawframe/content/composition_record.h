#pragma once

// SPEC-0021's CompositionRecord: the one exact playable closure a process
// runs (ADR-0023), a Game Build and the exact Packages and Mods beside it,
// each named by its Build root hash, under one profile. Its identity is the
// SHA-256 of its canonical bytes. It carries no signature: its trust is its
// hash and the publisher signatures of the Builds it names.

#include "rawframe/base/sha256.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::content {

/// One Build a Composition names: what it is, and its root hash.
struct BuildReference {
    std::string subject;
    std::string version;
    base::Sha256Digest build{};
};

struct CompositionRecord {
    BuildReference game;
    /// In subject order, each subject once.
    std::vector<BuildReference> mods;
    std::vector<BuildReference> packages;
    /// A profile token, 1 to 64 bytes.
    std::string profile;
    /// Unix seconds.
    std::int64_t createdAt = 0;
};

/// The record's canonical bytes, closed schema, at most 1 MiB: at most 256
/// mods and 4,096 packages, each list in subject order with no subject
/// twice, every subject, version, and root in its grammar. Refused
/// (`ManifestInvalid`, of the content domain) otherwise.
[[nodiscard]] result::Result<CompositionRecord> readComposition(std::string_view text);
[[nodiscard]] result::Result<std::string> writeComposition(const CompositionRecord& record);

/// The CompositionId: SHA-256 of the record's exact canonical bytes.
[[nodiscard]] base::Sha256Digest compositionIdOf(std::string_view text) noexcept;

} // namespace rawframe::content
