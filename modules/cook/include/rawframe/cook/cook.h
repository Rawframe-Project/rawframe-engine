#pragma once

// Cooking (ADR-0024): authored sources, each identified by its `.rfmeta`
// sidecar, turned by registered importers into immutable content-addressed
// artifacts, a SPEC-0008 content manifest naming them, and a receipt that
// proves the cook. Import tooling only: nothing here enters a client or a
// server.

#include "rawframe/base/sha256.h"
#include "rawframe/content/identity.h"
#include "rawframe/content/sidecar.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::cook {

/// A subasset an importer finds in a source and cooks (ADR-0024, D314):
/// its key, which the source's sidecar maps to the resource it becomes.
struct Subasset {
    std::string key;
    content::ResourceTypeId type;
    content::RepresentationId representation;
    std::vector<std::byte> bytes;

    friend bool operator==(const Subasset&, const Subasset&) = default;
};

/// What an importer makes of one source: the source's own resource, its
/// subassets in key order, and what it reports of the cook, written into
/// the receipt beside the source: counts by name, as SPEC-0026's variant
/// report (D319), in the importer's order.
struct Artifact {
    content::ResourceTypeId type;
    content::RepresentationId representation;
    std::vector<std::byte> bytes;
    std::vector<Subasset> subassets;
    std::vector<std::pair<std::string, std::int64_t>> report;

    friend bool operator==(const Artifact&, const Artifact&) = default;
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

    /// `sources` is canonical; `directory` is the source's, relative to it;
    /// `subassets` is the source's sidecar's map.
    Reads(std::filesystem::path sources,
          std::filesystem::path directory,
          std::map<std::string, content::ResourceId, std::less<>> subassets = {});

    /// The bytes of the file at `path`, relative to the source's directory.
    /// Refused (`BadRead`) when it is absolute, lies outside the sources, or
    /// cannot be read.
    [[nodiscard]] result::Result<std::span<const std::byte>> file(std::string_view path);
    /// Every regular file under the directory at `path`, at any depth, whose
    /// name ends in `suffix`: paths relative to that directory, with `/`,
    /// sorted. Refused as `file` is.
    [[nodiscard]] result::Result<std::vector<std::string>> files(std::string_view path, std::string_view suffix);

    /// The resource the sidecar maps the subasset `key` to, so what an
    /// importer cooks can name another of the source's subassets. Refused
    /// (`UnmappedSubasset`, naming the key) when it maps it to none: an
    /// identity is given by whoever authors the sidecar, never by a cook.
    [[nodiscard]] result::Result<content::ResourceId> subasset(std::string_view key);

    /// Makes `subasset` give a key the sidecar does not map a new identity
    /// from `fresh`, rather than refuse it: what `mapSubassets` cooks with.
    void assignWith(std::function<content::ResourceId()> fresh);
    /// The keys given identities so, and the identities.
    [[nodiscard]] const std::map<std::string, content::ResourceId, std::less<>>& assigned() const noexcept {
        return assigned_;
    }

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
    std::map<std::string, content::ResourceId, std::less<>> subassets_;
    std::function<content::ResourceId()> fresh_;
    std::map<std::string, content::ResourceId, std::less<>> assigned_;
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
    /// SPEC-0026's `variants_per_package`: the most material variants the
    /// artifacts' reports may count together (D319).
    std::int64_t maximumVariants = 4096;
};

struct CookReport {
    std::size_t cooked = 0;
    std::size_t reused = 0;
    /// The material variants the artifacts' reports count (D319).
    std::int64_t variants = 0;
    /// Every failure, in source order; any at all publishes nothing.
    std::vector<result::Error> failures;
};

/// Scans `sources` for sidecars (`<source file>.rfmeta`) in path order,
/// cooks each source with its importer (twice, compared) or takes its
/// artifact from the cache after verifying it and every read that made it,
/// writes each artifact and subasset to `objects/<digest>` under the output
/// (each subasset under the resource its sidecar maps it to, and refused as
/// `UnmappedSubasset` when it maps it to none), and, only if nothing failed,
/// publishes `content.manifest` and `cook.receipt` there. Errors are the
/// request's own (an unreadable sources directory, an output inside it);
/// what failed while cooking is in the report.
[[nodiscard]] result::Result<CookReport> cookSources(const CookRequest& request);

/// What `mapSubassets` did: each sidecar it wrote, by its path under the
/// sources, with the keys it added; and what failed, in sidecar order.
struct MapReport {
    std::vector<std::pair<std::string, std::vector<std::string>>> written;
    std::vector<result::Error> failures;
};

/// Gives each subasset an importer finds in a source, and the source's
/// sidecar does not map, an identity from `fresh`, and writes each sidecar
/// that gained one (ADR-0024: identity is given by authoring tooling, never
/// by a cook). A key already mapped keeps its identity, and one the source
/// no longer has stays. A source that does not cook for another reason is a
/// failure, and its sidecar is left as it was. Errors are the request's
/// own (an unreadable sources directory).
[[nodiscard]] result::Result<MapReport> mapSubassets(const std::filesystem::path& sources,
                                                     std::span<const Importer> importers,
                                                     const std::function<content::ResourceId()>& fresh);

/// The digest of a file, for a toolchain's identity.
[[nodiscard]] result::Result<base::Sha256Digest> digestOfFile(const std::filesystem::path& path);

} // namespace rawframe::cook
