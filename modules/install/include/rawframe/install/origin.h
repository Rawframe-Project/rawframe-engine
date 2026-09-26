#pragma once

// Where an update's Builds come from (SPEC-0038): any infrastructure that
// serves a Build's manifest and signature by its root, and a blob by its
// digest. Nothing an origin serves is trusted: a manifest is read only once
// a pinned key set verifies its signature, and a blob only once it hashes to
// what that manifest says. A mirror is laid out as a library is
// (`content::Library`); a Build as packed is its own directory.

#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace rawframe::install {

class Origin {
public:
    virtual ~Origin() = default;

    /// The manifest of the Build of root `root`, at most `ceiling` bytes.
    /// Blocks.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> manifest(const base::Sha256Digest& root,
                                                                          std::uint64_t ceiling) = 0;
    /// That manifest's signature envelope, at most `ceiling` bytes. Blocks.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> signature(const base::Sha256Digest& root,
                                                                           std::uint64_t ceiling) = 0;
    /// The blob of digest `blob`, at most `ceiling` bytes. Blocks.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> blob(const content::ContentDigest& blob,
                                                                      std::uint64_t ceiling) = 0;
};

/// A mirror at `root`, laid out as a library. Refused (`FetchFailed`) when
/// it is not a directory.
[[nodiscard]] result::Result<std::unique_ptr<Origin>> mirrorAt(const std::filesystem::path& root);

/// The one Build packed at `root` (its `build.manifest`, `build.manifest.sig`,
/// and `sha256/`), served whatever root is asked for: the manifest is then
/// refused if it is not that Build's. Refused (`FetchFailed`) when `root` is
/// not a directory.
[[nodiscard]] result::Result<std::unique_ptr<Origin>> packedBuildAt(const std::filesystem::path& root);

} // namespace rawframe::install
