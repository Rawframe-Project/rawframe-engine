// Installing and updating Builds (SPEC-0038): a publisher packs two versions
// of a game into a mirror, and a player's library installs the first and
// updates to the second. The update fetches only what the store lacks, ends
// byte for byte where a fresh install of the second ends, survives being
// stopped at every blob, refuses what an origin serves wrong, heals what
// rots in the store, rolls back without fetching, and collects only what no
// kept Composition needs.

#include "rawframe/build/build.h"
#include "rawframe/build/publisher_key.h"
#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/library.h"
#include "rawframe/content/manifest.h"
#include "rawframe/document/json.h"
#include "rawframe/install/errors.h"
#include "rawframe/install/installation.h"
#include "rawframe/signature/errors.h"
#include "rawframe/signature/signature.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::install;

namespace fs = std::filesystem;

namespace {

std::string readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

/// Bytes no chunker finds structure in: SHA-256 of 0, 1, 2, and so on.
std::string noise(std::size_t size) {
    std::string text;
    for (std::uint64_t block = 0; text.size() < size; ++block) {
        const base::Sha256Digest kDigest = base::sha256(std::to_string(block));
        text.append(reinterpret_cast<const char*>(kDigest.data()), kDigest.size());
    }
    text.resize(size);
    return text;
}

/// A cook's output of `resources` by identity, with its manifest and a
/// receipt that proves it, as the cook leaves one.
void cook(const fs::path& directory, const std::map<std::uint64_t, std::string>& resources) {
    fs::remove_all(directory);
    std::vector<content::ManifestEntry> entries;
    document::Value artifacts = document::Value::array();
    for (const auto& [id, bytes] : resources) {
        const content::ContentDigest kDigest = content::ContentDigest::of(bytesOf(bytes));
        const std::string kLocator = "objects/" + kDigest.text().substr(7);
        writeText(directory / kLocator, bytes);
        entries.push_back(
            content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                   .type = content::ResourceTypeId{base::Bits128{.high = 9, .low = 9}},
                                   .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                                   .byteLength = bytes.size(),
                                   .digest = kDigest,
                                   .locator = kLocator});
        std::array<char, 32> hex{};
        base::formatBits128Hex(entries.back().id.value, hex);
        document::Value artifact = document::Value::object();
        artifact.add("resourceId", document::Value::string(std::string{hex.data(), hex.size()}));
        artifact.add("representation", document::Value::string("rawframe.audio.wave"));
        artifact.add("digest", document::Value::string(kDigest.text()));
        artifact.add("byteLength", document::Value::integer(static_cast<std::int64_t>(bytes.size())));
        artifacts.push(std::move(artifact));
    }
    const std::string kManifest = content::writeManifest(entries);
    writeText(directory / "content.manifest", kManifest);
    document::Value receipt = document::Value::object();
    receipt.add("kind", document::Value::string("cook.receipt"));
    receipt.add("formatVersion", document::Value::integer(1));
    receipt.add("manifest", document::Value::string(content::ContentDigest::of(bytesOf(kManifest)).text()));
    receipt.add("artifacts", std::move(artifacts));
    receipt.add("failures", document::Value::integer(0));
    writeText(directory / "cook.receipt", document::write(receipt));
}

/// A publisher's two versions of `rawframe/runners` in a mirror, their
/// Compositions, and the key set a player pins.
struct Published {
    fs::path base = test::scratchDirectory("install");
    fs::path mirror = base / "mirror";
    build::PublisherKey key = *build::generatePublisherKey("rawframe");
    std::string keys = *signature::writePublisherKeySet(*build::keySetOf(key, 1'790'000'000));
    std::string large = noise(std::size_t{3} * 1024 * 1024);
    content::BuildReference first;
    content::BuildReference second;
    std::string firstRecord;
    std::string secondRecord;

    Published() {
        fs::remove_all(base);
        // The second version changes the end of the large resource, drops
        // one resource, and adds another.
        std::string changed = large;
        changed.replace(changed.size() - 4096, 4096, noise(4096 + 7).substr(7));
        std::string phrase;
        while (phrase.size() < std::size_t{1024} * 1024) {
            phrase += "every shot sounds from where it was fired; ";
        }
        first = publish("0.1.0", {{1, "bang"}, {2, large}, {3, phrase}});
        second = publish("0.2.0", {{1, "bang"}, {2, changed}, {4, "whoosh"}});
        firstRecord = recordOf(first);
        secondRecord = recordOf(second);
    }
    ~Published() {
        fs::remove_all(base);
    }
    Published(const Published&) = delete;
    Published& operator=(const Published&) = delete;

    content::BuildReference publish(std::string_view version, const std::map<std::uint64_t, std::string>& resources) {
        cook(base / "cooked", resources);
        const auto kReport = build::packBuild(build::BuildRequest{.cooked = base / "cooked",
                                                                  .output = base / "packed",
                                                                  .identity = {.subject = "rawframe/runners",
                                                                               .version = std::string{version},
                                                                               .engine = "0.1.0",
                                                                               .platform = "linux",
                                                                               .architecture = "x86_64",
                                                                               .side = "client",
                                                                               .configuration = "build.development",
                                                                               .profile = "tool"},
                                                                  .signer = &key});
        RAWFRAME_EXPECT(kReport.has_value());
        const content::BuildReference kReference{.subject = "rawframe/runners",
                                                 .version = std::string{version},
                                                 .build = kReport.has_value() ? kReport->root : base::Sha256Digest{}};
        // Into the mirror as a player's library would add it.
        writeText(mirror / content::keysPathOf("rawframe"), keys);
        auto mirrored = Installation::open(mirror);
        auto packed = packedBuildAt(base / "packed");
        RAWFRAME_EXPECT(mirrored.has_value() && packed.has_value() && mirrored->add(kReference, **packed).has_value());
        fs::remove_all(base / "packed");
        return kReference;
    }

    [[nodiscard]] std::string recordOf(const content::BuildReference& game) const {
        return *content::writeComposition(content::CompositionRecord{
            .game = game, .mods = {}, .packages = {}, .profile = "community", .createdAt = 1'790'000'000});
    }

    /// A player's library with the publisher's key set pinned.
    [[nodiscard]] Installation library(std::string_view name, const InstallLimits& limits = {}) const {
        writeText(base / name / content::keysPathOf("rawframe"), keys);
        return *Installation::open(base / name, limits);
    }

    [[nodiscard]] std::unique_ptr<Origin> origin() const {
        return *mirrorAt(mirror);
    }

    [[nodiscard]] content::BuildManifest manifestOf(const content::BuildReference& build) const {
        const auto kLibrary = content::Library::directory(mirror);
        return *kLibrary->manifest(build.build, *kLibrary->keys("rawframe"));
    }
};

/// Serves what its origin serves, refusing every blob from the `fail`th on.
class StoppedOrigin final : public Origin {
public:
    StoppedOrigin(Origin& origin, std::size_t fail) noexcept : origin_(origin), fail_(fail) {
    }
    result::Result<std::vector<std::byte>> manifest(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return origin_.manifest(root, ceiling);
    }
    result::Result<std::vector<std::byte>> signature(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return origin_.signature(root, ceiling);
    }
    result::Result<std::vector<std::byte>> blob(const content::ContentDigest& blob, std::uint64_t ceiling) override {
        if (served_++ >= fail_) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::Unavailable, kInstallDomain, code(InstallError::FetchFailed), "stop")
                    .error()};
        }
        return origin_.blob(blob, ceiling);
    }
    result::Result<std::vector<std::byte>> record(std::string_view path, std::uint64_t ceiling) override {
        return origin_.record(path, ceiling);
    }

private:
    Origin& origin_;
    std::size_t fail_ = 0;
    std::size_t served_ = 0;
};

/// Serves one blob with a byte changed.
class TamperingOrigin final : public Origin {
public:
    explicit TamperingOrigin(Origin& origin) noexcept : origin_(origin) {
    }
    result::Result<std::vector<std::byte>> manifest(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return origin_.manifest(root, ceiling);
    }
    result::Result<std::vector<std::byte>> signature(const base::Sha256Digest& root, std::uint64_t ceiling) override {
        return origin_.signature(root, ceiling);
    }
    result::Result<std::vector<std::byte>> blob(const content::ContentDigest& blob, std::uint64_t ceiling) override {
        auto served = origin_.blob(blob, ceiling);
        if (served.has_value() && !served->empty()) {
            served->back() ^= std::byte{1};
        }
        return served;
    }
    result::Result<std::vector<std::byte>> record(std::string_view path, std::uint64_t ceiling) override {
        return origin_.record(path, ceiling);
    }

private:
    Origin& origin_;
};

Inventory blobSetOf(const content::BuildManifest& manifest) {
    Inventory blobs;
    for (const auto& chunks : manifest.chunks) {
        for (const content::BuildChunk& chunk : chunks) {
            blobs.insert(chunk.blob.bytes);
        }
    }
    return blobs;
}

std::size_t blobsOf(const content::BuildManifest& manifest) {
    return blobSetOf(manifest).size();
}

bool refusedAs(const auto& outcome, InstallError error) {
    return !outcome.has_value() && outcome.error().domain() == kInstallDomain && outcome.error().code() == code(error);
}

/// Every file under `root`, by its path within it, and its bytes.
std::map<std::string, std::string> filesUnder(const fs::path& root) {
    std::map<std::string, std::string> files;
    for (const fs::directory_entry& each : fs::recursive_directory_iterator(root)) {
        if (each.is_regular_file()) {
            files.emplace(fs::relative(each.path(), root).generic_string(), readText(each.path()));
        }
    }
    return files;
}

} // namespace

RAWFRAME_TEST(AnUpdateFetchesOnlyWhatTheStoreLacks) {
    const Published kPublished;
    Installation library = kPublished.library("player");
    const auto kOrigin = kPublished.origin();
    const content::BuildManifest kFirst = kPublished.manifestOf(kPublished.first);
    const content::BuildManifest kSecond = kPublished.manifestOf(kPublished.second);

    const auto kInstalled = library.update(kPublished.firstRecord, *kOrigin);
    RAWFRAME_EXPECT(kInstalled.has_value() && kInstalled->fetched == blobsOf(kFirst) && kInstalled->healed == 0);
    RAWFRAME_EXPECT(library.installed().active == content::compositionIdOf(kPublished.firstRecord));

    // The second version's plan against the first's store: its new resource
    // and the large resource's changed last chunk, nothing else.
    const UpdatePlan kPlan = planUpdate(std::span{&kSecond, 1}, library.inventory());
    RAWFRAME_EXPECT(kPlan.entries.size() == 2);
    RAWFRAME_EXPECT(!kPlan.entries.empty() && kPlan.entries.front().manifest == kSecond.descriptor);
    const auto kUpdated = library.update(kPublished.secondRecord, *kOrigin);
    RAWFRAME_EXPECT(kUpdated.has_value() && kUpdated->fetched == kPlan.entries.size() &&
                    kUpdated->fetchedBytes == kPlan.bytes());
    RAWFRAME_EXPECT(library.installed().active == content::compositionIdOf(kPublished.secondRecord) &&
                    library.installed().retained == std::vector{content::compositionIdOf(kPublished.firstRecord)});
    // Nothing left to fetch: the same update again fetches nothing.
    const auto kAgain = library.update(kPublished.secondRecord, *kOrigin);
    RAWFRAME_EXPECT(kAgain.has_value() && kAgain->fetched == 0);
    RAWFRAME_EXPECT(planUpdate(std::span{&kSecond, 1}, library.inventory()).entries.empty());
}

RAWFRAME_TEST(AnUpdatedLibraryIsAFreshOne) {
    const Published kPublished;
    const auto kOrigin = kPublished.origin();
    Installation updated = kPublished.library("updated");
    RAWFRAME_EXPECT(updated.update(kPublished.firstRecord, *kOrigin).has_value());
    RAWFRAME_EXPECT(updated.update(kPublished.secondRecord, *kOrigin).has_value());
    RAWFRAME_EXPECT(updated.collect().has_value());
    Installation fresh = kPublished.library("fresh");
    RAWFRAME_EXPECT(fresh.update(kPublished.secondRecord, *kOrigin).has_value());

    // Past the retained first version, which the fresh library never had,
    // the two hold the same files byte for byte.
    std::map<std::string, std::string> kept = filesUnder(kPublished.base / "updated");
    std::map<std::string, std::string> made = filesUnder(kPublished.base / "fresh");
    const content::BuildManifest kFirst = kPublished.manifestOf(kPublished.first);
    const content::BuildManifest kSecond = kPublished.manifestOf(kPublished.second);
    const Inventory kSecondBlobs = blobSetOf(kSecond);
    for (const base::Sha256Digest& blob : blobSetOf(kFirst)) {
        if (!kSecondBlobs.contains(blob)) {
            kept.erase(content::blobPathOf(content::ContentDigest{.bytes = blob}));
        }
    }
    kept.erase(content::buildDirectoryOf(kPublished.first.build) + "/build.manifest");
    kept.erase(content::buildDirectoryOf(kPublished.first.build) + "/build.manifest.sig");
    kept.erase(content::compositionPathOf(content::compositionIdOf(kPublished.firstRecord)));
    kept.erase("installed");
    made.erase("installed");
    RAWFRAME_EXPECT(kept == made);
}

RAWFRAME_TEST(APlanIsItsCanonicalRecord) {
    const Published kPublished;
    const content::BuildManifest kSecond = kPublished.manifestOf(kPublished.second);
    const UpdatePlan kAll = planUpdate(std::span{&kSecond, 1}, {});
    RAWFRAME_EXPECT(kAll.entries.size() == blobsOf(kSecond));
    const auto kWritten = writePlan(kAll);
    RAWFRAME_EXPECT(kWritten.has_value() &&
                    writePlan(planUpdate(std::span{&kSecond, 1}, {})).value_or("") == *kWritten);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->find("mirror") == std::string::npos &&
                    kWritten->find(kSecond.descriptor.text()) != std::string::npos);
    Inventory held;
    for (const PlanEntry& entry : kAll.entries) {
        held.insert(entry.blob.bytes);
    }
    RAWFRAME_EXPECT(writePlan(planUpdate(std::span{&kSecond, 1}, held)).value_or("") ==
                    std::string{"{\"entries\":[],\"schema\":1}"});
}

RAWFRAME_TEST(AStoppedUpdateChangesNothingAndResumes) {
    const Published kPublished;
    const auto kOrigin = kPublished.origin();
    const content::BuildManifest kSecond = kPublished.manifestOf(kPublished.second);
    const std::size_t kNeeded = blobsOf(kSecond);
    for (std::size_t stop = 0; stop < kNeeded; ++stop) {
        Installation library = kPublished.library("stopped" + std::to_string(stop));
        StoppedOrigin stopped{*kOrigin, stop};
        RAWFRAME_EXPECT(refusedAs(library.update(kPublished.secondRecord, stopped), InstallError::FetchFailed));
        // Nothing installed, no Build published, and only whole, verified
        // blobs left behind.
        RAWFRAME_EXPECT(!library.installed().active.has_value() && library.inventory().size() == stop);
        RAWFRAME_EXPECT(!fs::exists(kPublished.base / ("stopped" + std::to_string(stop)) /
                                    content::buildDirectoryOf(kPublished.second.build)));
        // Opened again, as after a crash, and retried: only the rest.
        Installation reopened = *Installation::open(kPublished.base / ("stopped" + std::to_string(stop)));
        const auto kResumed = reopened.update(kPublished.secondRecord, *kOrigin);
        RAWFRAME_EXPECT(kResumed.has_value() && kResumed->fetched == kNeeded - stop);
        RAWFRAME_EXPECT(reopened.installed().active == content::compositionIdOf(kPublished.secondRecord));
    }
}

RAWFRAME_TEST(WhatAnOriginServesWrongIsRefused) {
    const Published kPublished;
    const auto kOrigin = kPublished.origin();
    Installation library = kPublished.library("player");
    RAWFRAME_EXPECT(library.update(kPublished.firstRecord, *kOrigin).has_value());
    const Inventory kBefore = library.inventory();

    TamperingOrigin tampering{*kOrigin};
    RAWFRAME_EXPECT(refusedAs(library.update(kPublished.secondRecord, tampering), InstallError::FetchedWrong));
    RAWFRAME_EXPECT(library.installed().active == content::compositionIdOf(kPublished.firstRecord) &&
                    library.inventory() == kBefore);

    // A manifest another key signed, or of another Build than named.
    fs::copy_file(kPublished.mirror / content::buildDirectoryOf(kPublished.first.build) / "build.manifest.sig",
                  kPublished.base / "first.sig");
    fs::copy_file(kPublished.base / "first.sig",
                  kPublished.mirror / content::buildDirectoryOf(kPublished.second.build) / "build.manifest.sig",
                  fs::copy_options::overwrite_existing);
    const auto kForged = library.update(kPublished.secondRecord, *kOrigin);
    RAWFRAME_EXPECT(!kForged.has_value() && kForged.error().domain() == signature::kSignatureDomain);
    content::BuildReference named = kPublished.second;
    named.build = kPublished.first.build;
    named.version = "0.2.0";
    const auto kMisnamed = library.add(named, *kOrigin);
    RAWFRAME_EXPECT(!kMisnamed.has_value() && kMisnamed.error().code() == code(content::ContentError::ManifestInvalid));
    RAWFRAME_EXPECT(library.installed().active == content::compositionIdOf(kPublished.firstRecord));

    // No key set pinned for the publisher: nothing is read.
    Installation unpinned = *Installation::open(kPublished.base / "unpinned");
    RAWFRAME_EXPECT(!unpinned.update(kPublished.firstRecord, *kOrigin).has_value() && unpinned.inventory().empty());
}

RAWFRAME_TEST(WhatRotsInTheStoreIsHealed) {
    const Published kPublished;
    const auto kOrigin = kPublished.origin();
    Installation library = kPublished.library("player");
    RAWFRAME_EXPECT(library.update(kPublished.firstRecord, *kOrigin).has_value());
    const content::BuildManifest kFirst = kPublished.manifestOf(kPublished.first);
    const content::ContentDigest kLarge = kFirst.chunks[1].front().blob;
    const fs::path kPath = kPublished.base / "player" / content::blobPathOf(kLarge);

    // A reused blob that rotted is never trusted: the update fetches it
    // again as it verifies the Build.
    writeText(kPath, "rotted");
    const auto kUpdated = library.update(kPublished.secondRecord, *kOrigin);
    RAWFRAME_EXPECT(kUpdated.has_value() && kUpdated->healed == 1);
    // One gone missing is planned again, and fetched from the origin.
    fs::remove(kPath);
    const auto kHealed = library.heal(*kOrigin);
    RAWFRAME_EXPECT(kHealed.has_value() && kHealed->fetched == 1 && kHealed->healed == 0 && fs::exists(kPath));
    const auto kWhole = library.heal(*kOrigin);
    RAWFRAME_EXPECT(kWhole.has_value() && kWhole->fetched == 0 && kWhole->healed == 0);
}

RAWFRAME_TEST(RollbackMovesThePointerAndCollectKeepsWhatItNeeds) {
    const Published kPublished;
    const auto kOrigin = kPublished.origin();
    Installation library = kPublished.library("player");
    RAWFRAME_EXPECT(refusedAs(library.rollback(), InstallError::NothingToRollBack));
    RAWFRAME_EXPECT(library.update(kPublished.firstRecord, *kOrigin).has_value());
    RAWFRAME_EXPECT(library.update(kPublished.secondRecord, *kOrigin).has_value());
    const base::Sha256Digest kFirst = content::compositionIdOf(kPublished.firstRecord);
    const base::Sha256Digest kSecond = content::compositionIdOf(kPublished.secondRecord);

    // Back and forth, fetching nothing, and the pointer survives reopening.
    RAWFRAME_EXPECT(library.rollback().has_value() && library.installed().active == kFirst &&
                    library.installed().retained == std::vector{kSecond});
    RAWFRAME_EXPECT(Installation::open(kPublished.base / "player")->installed().active == kFirst);
    RAWFRAME_EXPECT(library.rollback().has_value() && library.installed().active == kSecond);

    // Both kept, so collect keeps every blob of both.
    const Inventory kHeld = library.inventory();
    RAWFRAME_EXPECT(library.collect() == std::size_t{0} && library.inventory() == kHeld);

    // A third Composition (the second with a profile of its own) retires
    // the first past the retention depth, and collect then removes what only
    // the first needed; rolling back once is still possible.
    const std::string kThird = *content::writeComposition(content::CompositionRecord{
        .game = kPublished.second, .mods = {}, .packages = {}, .profile = "ranked", .createdAt = 1'790'000'000});
    RAWFRAME_EXPECT(library.update(kThird, *kOrigin).has_value() &&
                    library.installed().retained == std::vector{kSecond});
    const auto kRemoved = library.collect();
    RAWFRAME_EXPECT(kRemoved.has_value() && *kRemoved > 0 &&
                    !fs::exists(kPublished.base / "player" / content::buildDirectoryOf(kPublished.first.build)) &&
                    !fs::exists(kPublished.base / "player" / content::compositionPathOf(kFirst)));
    RAWFRAME_EXPECT(library.rollback().has_value() && library.installed().active == kSecond);
    const auto kWhole = library.heal(*kOrigin);
    RAWFRAME_EXPECT(kWhole.has_value() && kWhole->fetched == 0);
}

// SPEC-0038's golden plans: the golden Build of SPEC-0021's corpus (D82)
// planned against no store and against one holding its first resource and
// the first chunk of its second, byte for byte. A change is a new plan
// generation, never a silent one; RAWFRAME_WRITE_GOLDEN=1 rewrites them.
RAWFRAME_TEST(PlansAreGolden) {
    const fs::path kBuild{RAWFRAME_BUILD_GOLDEN};
    const fs::path kPlans{RAWFRAME_PLAN_GOLDEN};
    const std::string kManifest = readText(kBuild / "build.manifest");
    const std::string kSigned = readText(kBuild / "build.manifest.sig");
    const auto kRoot = content::ContentDigest::parse(readText(kBuild / "root.txt").substr(0, 71));
    const auto kKeys = signature::readPublisherKeySet(readText(kBuild / "rawframe.keys"));
    RAWFRAME_EXPECT(kRoot.has_value() && kKeys.has_value());
    if (!kRoot.has_value() || !kKeys.has_value()) {
        return;
    }
    const auto kRead = content::readBuildManifest(bytesOf(kManifest), bytesOf(kSigned), kRoot->bytes, *kKeys);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->chunks.size() >= 2);
    if (!kRead.has_value() || kRead->chunks.size() < 2) {
        return;
    }
    const Inventory kHeld = {kRead->chunks[0].front().blob.bytes, kRead->chunks[1].front().blob.bytes};
    for (const auto& [name, inventory] : {std::pair{"full.plan", Inventory{}}, std::pair{"partial.plan", kHeld}}) {
        const auto kWritten = writePlan(planUpdate(std::span{&*kRead, 1}, inventory));
        RAWFRAME_EXPECT(kWritten.has_value());
        if (std::getenv("RAWFRAME_WRITE_GOLDEN") != nullptr && kWritten.has_value()) {
            writeText(kPlans / name, *kWritten);
        }
        RAWFRAME_EXPECT(kWritten.value_or("") == readText(kPlans / name));
    }
}
