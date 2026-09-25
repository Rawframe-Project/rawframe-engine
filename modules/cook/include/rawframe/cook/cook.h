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
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::cook {

/// The suffix of a source's sidecar: `<source file>.rfmeta`.
inline constexpr std::string_view kSidecarSuffix = ".rfmeta";

/// A sidecar as written: the resource its source becomes, the importer that
/// makes it, and the settings it gives, if any.
struct Sidecar {
    content::ResourceId id;
    std::string importer;
    std::optional<document::Value> settings;
};

/// Reads a sidecar: canonical JSON `{schema: 1, resourceId, importer,
/// settings?}` with a resource identity other than nought. Refused
/// (`BadSidecar`, or the document's own error) otherwise. An importer that
/// resolves a name to a resource reads the name's sidecar with this.
[[nodiscard]] result::Result<Sidecar> readSidecar(std::string_view text);

/// What an importer makes of one source.
struct Artifact {
    content::ResourceTypeId type;
    content::RepresentationId representation;
    std::vector<std::byte> bytes;
};

/// What one cook reads besides its source: other files under the sources,
/// named relative to the source's directory, and the files a directory
/// holds. Every read is recorded with a digest of what it saw; a cached
/// artifact is reused only while every read would see the same again
/// (ADR-0024: the key is every input, and these are inputs the importer
/// found). Nothing outside the sources is read, and a read is the same bytes
/// every time it is asked, so the two cooks of one source see one input.
class Reads {
public:
    /// One read as recorded: a file's path, or a directory's with the
    /// suffix it was listed for, relative to the sources, and the digest of
    /// the bytes or the listing.
    struct Read {
        std::string path;
        /// Empty for a file; for a listing, the suffix listed.
        std::string suffix;
        bool listing = false;
        base::Sha256Digest digest{};
    };

    /// `sources` is canonical; `directory` is the source's, relative to it.
    Reads(std::filesystem::path sources, std::filesystem::path directory);

    /// The bytes of the file at `path`, relative to the source's directory.
    /// Refused (`BadRead`) when it is absolute, lies outside the sources, or
    /// cannot be read.
    [[nodiscard]] result::Result<std::span<const std::byte>> file(std::string_view path);
    /// Every regular file under the directory at `path`, at any depth, whose
    /// name ends in `suffix`: paths relative to that directory, with `/`,
    /// sorted. Refused as `file` is.
    [[nodiscard]] result::Result<std::vector<std::string>> files(std::string_view path, std::string_view suffix);

    /// Every read so far, in path order.
    [[nodiscard]] std::vector<Read> reads() const;

    /// The digest of a listing: each name and a line feed, in order.
    [[nodiscard]] static base::Sha256Digest digestOfListing(std::span<const std::string> names);

private:
    [[nodiscard]] result::Result<std::filesystem::path> resolve(std::string_view path) const;

    std::filesystem::path sources_;
    std::filesystem::path directory_;
    std::map<std::string, std::vector<std::byte>> files_;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> listings_;
};

/// One registered importer: who it is, what settings it takes, and how it
/// cooks. Selection is by the sidecar's `importer`, never by extension.
struct Importer {
    std::string_view identity;
    /// The settings in the one form that enters the cook key, defaults
    /// written out; refused when they are not this importer's.
    result::Result<std::string> (*normalize)(const document::Value* settings) = nullptr;
    /// Cooks the source's bytes under normalized settings, reading anything
    /// else it needs through `reads`. Must give the same bytes for the same
    /// inputs: every cook is done twice and compared.
    result::Result<Artifact> (*cook)(std::span<const std::byte> source,
                                     std::string_view settings,
                                     Reads& reads) = nullptr;
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
/// artifact from the cache after verifying it and every read that made it, writes each artifact to
/// `objects/<digest>` under the output, and, only if nothing failed,
/// publishes `content.manifest` and `cook.receipt` there. Errors are the
/// request's own (an unreadable sources directory, an output inside it);
/// what failed while cooking is in the report.
[[nodiscard]] result::Result<CookReport> cookSources(const CookRequest& request);

/// The digest of a file, for a toolchain's identity.
[[nodiscard]] result::Result<base::Sha256Digest> digestOfFile(const std::filesystem::path& path);

} // namespace rawframe::cook
