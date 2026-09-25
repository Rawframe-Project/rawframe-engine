#pragma once

// Cooking (ADR-0024): authored sources, each identified by its `.rfmeta`
// sidecar, turned by registered importers into immutable content-addressed
// artifacts, a SPEC-0008 content manifest naming them, and a receipt that
// proves the cook. Import tooling only: nothing here enters a client or a
// server.

#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::cook {

/// What an importer makes of one source.
struct Artifact {
    content::ResourceTypeId type;
    content::RepresentationId representation;
    std::vector<std::byte> bytes;
};

/// One registered importer: who it is, what settings it takes, and how it
/// cooks. Selection is by the sidecar's `importer`, never by extension.
struct Importer {
    std::string_view identity;
    /// The settings in the one form that enters the cook key, defaults
    /// written out; refused when they are not this importer's.
    result::Result<std::string> (*normalize)(const document::Value* settings) = nullptr;
    /// Cooks the source's bytes under normalized settings. Must give the same
    /// bytes for the same inputs: every cook is done twice and compared.
    result::Result<Artifact> (*cook)(std::span<const std::byte> source, std::string_view settings) = nullptr;
};

struct CookRequest {
    /// The authored sources, and the sidecars beside them.
    std::filesystem::path sources;
    /// Where the artifacts, manifest, and receipt go; never inside the
    /// sources.
    std::filesystem::path output;
    /// A derived-data cache, reused across cooks; none cooks everything.
    std::optional<std::filesystem::path> cache;
    std::span<const Importer> importers;
    /// The digest of the tool doing the cook: in every key, so a changed
    /// tool never reuses what another made.
    base::Sha256Digest toolchain{};
    /// The target profile the artifacts are for.
    std::string target = "any";
};

struct CookReport {
    std::size_t cooked = 0;
    std::size_t reused = 0;
    /// Every failure, in source order; any at all publishes nothing.
    std::vector<result::Error> failures;
};

/// Scans `sources` for sidecars (`<source file>.rfmeta`) in path order,
/// cooks each source with its importer (twice, compared) or takes its
/// artifact from the cache after verifying it, writes each artifact to
/// `objects/<digest>` under the output, and, only if nothing failed,
/// publishes `content.manifest` and `cook.receipt` there. Errors are the
/// request's own (an unreadable sources directory, an output inside it);
/// what failed while cooking is in the report.
[[nodiscard]] result::Result<CookReport> cookSources(const CookRequest& request);

/// The digest of a file, for a toolchain's identity.
[[nodiscard]] result::Result<base::Sha256Digest> digestOfFile(const std::filesystem::path& path);

} // namespace rawframe::cook
