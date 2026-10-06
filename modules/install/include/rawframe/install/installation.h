#pragma once

// A library a machine installs into and updates (SPEC-0038). An update is a
// chunk difference: the target Builds' manifests are read and verified, the
// blobs the store lacks are fetched from an origin and verified, every
// target is verified whole (blobs already held reverified before they are
// reused), and only then does the installed pointer move, in one atomic
// write. Nothing installed is ever changed in place: blobs, Build
// directories, and kept Compositions are written under `staging/` and
// renamed into place, so an update stopped at any point leaves the installed
// state as it was, and a retried one fetches only what is still missing.
// Rollback moves the pointer back to a retained Composition; heal refetches
// whatever a Build's verification finds damaged; collect removes what no
// active or retained Composition needs. One writer at a time.

#include "rawframe/base/sha256.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/library.h"
#include "rawframe/install/origin.h"
#include "rawframe/install/plan.h"
#include "rawframe/release/release.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::install {

/// SPEC-0038's named limit points; values are the product's, these are
/// generation 1's.
struct InstallLimits {
    /// Compositions retained for rollback behind the active one (at least 1).
    std::size_t retained = 1;
    /// The most one update or heal fetches.
    std::uint64_t maximumFetchBytes = std::uint64_t{64} << 30U;
};

/// What the installed pointer names.
struct Installed {
    std::optional<base::Sha256Digest> active;
    /// Newest first.
    std::vector<base::Sha256Digest> retained;
};

struct UpdateReport {
    /// The Builds' blobs fetched, and their bytes.
    std::size_t fetched = 0;
    std::uint64_t fetchedBytes = 0;
    /// Of those, held blobs that did not verify and were fetched again.
    std::size_t healed = 0;
};

/// What following a channel did (D424): the Release it installed, the
/// pointer's sequence kept, and the update.
struct Followed {
    std::string version;
    base::Sha256Digest release{};
    std::int64_t sequence = 0;
    UpdateReport update;
};

class Installation {
public:
    /// The library at `root`, made if there is none, with what an earlier
    /// writer left staged removed. Refused (`WriteFailed`) when it cannot be
    /// made, and (`LibraryInvalid`) when its installed pointer is not one.
    [[nodiscard]] static result::Result<Installation> open(const std::filesystem::path& root,
                                                           const InstallLimits& limits = {});

    [[nodiscard]] const Installed& installed() const noexcept {
        return installed_;
    }

    /// Adds `build` from `origin`: its manifest verified against the key set
    /// pinned in the library for its subject's publisher, and the subject
    /// and version it names (`ManifestInvalid`); the blobs the store lacks
    /// fetched; and the whole Build verified before its directory is
    /// published. A Build already held is verified, and healed from `origin`
    /// if it must be. The installed pointer does not move.
    [[nodiscard]] result::Result<UpdateReport> add(const content::BuildReference& build, Origin& origin);

    /// Updates to the Composition whose canonical record is `record`: every
    /// Build it names added as `add` adds one, and each the subject and
    /// version it names (`ManifestInvalid`), then the Composition kept and
    /// made active, the one active before retained for rollback.
    [[nodiscard]] result::Result<UpdateReport> update(std::string_view record, Origin& origin);

    /// SPEC-0020's update check, then the update (D424): `subject`'s pointer
    /// for `channel` and the Release it names fetched from `origin` with
    /// their signatures, each verified against the key set pinned in the
    /// library for the subject's publisher (a key it does not list,
    /// `UnknownKey`, is for the caller to refresh and ask again); the
    /// pointer past the sequence the library last followed
    /// (`SequenceRegression`), a lower Release under a higher sequence
    /// being a rollback; the Release's Composition fetched by its digest and
    /// checked against the artifact's size and digest; then installed as
    /// `update` installs it. The sequence is kept once the update is done,
    /// so a refused or stopped follow can be retried.
    [[nodiscard]] result::Result<Followed> follow(std::string_view subject, release::Channel channel, Origin& origin);

    /// Makes the newest retained Composition active again and retains the
    /// one that was, fetching nothing; refused (`NothingToRollBack`) when none
    /// is retained or its Builds are not whole.
    [[nodiscard]] result::Status rollback();

    /// Verifies every Build of the active Composition and fetches again
    /// whatever does not verify.
    [[nodiscard]] result::Result<UpdateReport> heal(Origin& origin);

    /// Removes every Build, blob, and kept Composition that neither the
    /// active Composition nor a retained one needs, and returns how many
    /// blobs went.
    [[nodiscard]] result::Result<std::size_t> collect();

    /// The blobs the store holds, whether or not they verify.
    [[nodiscard]] Inventory inventory() const;

private:
    Installation(std::filesystem::path root, content::Library library, const InstallLimits& limits) noexcept
        : root_(std::move(root)), library_(std::move(library)), limits_(limits) {
    }

    /// Adds Builds as `add` adds one, their blobs in one plan.
    [[nodiscard]] result::Result<UpdateReport> install(std::span<const content::BuildReference> builds, Origin& origin);
    /// Fetches, verifies, and publishes a plan's blobs.
    [[nodiscard]] result::Status fetch(const UpdatePlan& plan, Origin& origin, UpdateReport& report);
    /// Verifies a Build whole, fetching again what does not verify.
    [[nodiscard]] result::Status verify(const content::BuildManifest& manifest, Origin& origin, UpdateReport& report);
    /// The Builds a kept Composition names.
    [[nodiscard]] result::Result<std::vector<content::BuildReference>>
    buildsOf(const base::Sha256Digest& composition) const;
    /// Whether a Build is held and verifies.
    [[nodiscard]] bool whole(const content::BuildReference& build) const;
    /// Writes the installed pointer.
    [[nodiscard]] result::Status point(Installed next);

    std::filesystem::path root_;
    content::Library library_;
    InstallLimits limits_;
    Installed installed_;
};

} // namespace rawframe::install
