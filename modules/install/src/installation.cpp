#include "rawframe/install/installation.h"

#include "files.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/product.h"
#include "rawframe/document/json.h"
#include "rawframe/install/errors.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace rawframe::install {

namespace fs = std::filesystem;

namespace {

/// SPEC-0021's ceiling on a CompositionRecord.
constexpr std::uint64_t kMaximumComposition = std::uint64_t{1} << 20U;
constexpr std::uint64_t kMaximumInstalled = std::uint64_t{64} << 10U;

std::unexpected<result::Error> failed(result::ErrorClass errorClass, InstallError error, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kInstallDomain, code(error), why).error()};
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

std::string hexOf(const base::Sha256Digest& digest) {
    return content::ContentDigest{.bytes = digest}.text().substr(7);
}

/// The installed pointer's canonical record.
result::Result<std::string> writeInstalled(const Installed& installed) {
    using document::Value;
    Value retained = Value::array();
    for (const base::Sha256Digest& each : installed.retained) {
        retained.push(Value::string(content::ContentDigest{.bytes = each}.text()));
    }
    Value record = Value::object();
    record.add("schema", Value::integer(1));
    if (installed.active.has_value()) {
        record.add("active", Value::string(content::ContentDigest{.bytes = *installed.active}.text()));
    }
    record.add("retained", std::move(retained));
    return document::writeCanonicalRecord(record);
}

result::Result<Installed> readInstalled(std::string_view text) {
    const auto kInvalid = [] {
        return failed(result::ErrorClass::DataLoss,
                      InstallError::LibraryInvalid,
                      "the installed pointer is not {schema: 1, active?, retained}");
    };
    const auto kParsed = document::parseCanonicalRecord(text, {.maximumBytes = kMaximumInstalled});
    if (!kParsed.has_value()) {
        return kInvalid();
    }
    const document::Value* schema = kParsed->find("schema");
    const document::Value* active = kParsed->find("active");
    const document::Value* retained = kParsed->find("retained");
    if (schema == nullptr || schema->integer() != 1 || retained == nullptr ||
        retained->kind() != document::Value::Kind::Array || kParsed->names().size() != (active != nullptr ? 3U : 2U)) {
        return kInvalid();
    }
    Installed read;
    if (active != nullptr) {
        const auto kActive = active->text() != nullptr ? content::ContentDigest::parse(*active->text()) : std::nullopt;
        if (!kActive.has_value()) {
            return kInvalid();
        }
        read.active = kActive->bytes;
    }
    for (const document::Value& each : retained->items()) {
        const auto kRetained = each.text() != nullptr ? content::ContentDigest::parse(*each.text()) : std::nullopt;
        if (!kRetained.has_value()) {
            return kInvalid();
        }
        read.retained.push_back(kRetained->bytes);
    }
    return read;
}

/// A Build whose manifest was read, and the bytes to keep of it when it
/// came from an origin.
struct Target {
    content::BuildManifest manifest;
    std::vector<std::byte> manifestBytes;
    std::vector<std::byte> signatureBytes;
    bool fetched = false;
};

result::Status matches(const content::BuildManifest& manifest, const content::BuildReference& reference) {
    if (manifest.subject != reference.subject || manifest.version != reference.version) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                           content::kContentDomain,
                                                           code(content::ContentError::ManifestInvalid),
                                                           "a Build is not the subject and version named")
                                                  .error()
                                                  .withContext("build", hexOf(reference.build))};
    }
    return {};
}

} // namespace

result::Result<Installation> Installation::open(const fs::path& root, const InstallLimits& limits) {
    std::error_code error;
    fs::create_directories(root / "sha256", error);
    fs::create_directories(root / "builds", error);
    fs::create_directories(root / "compositions", error);
    // Whatever an earlier writer staged and never published.
    fs::remove_all(root / content::kStagingName, error);
    if (error) {
        return failed(result::ErrorClass::Unavailable, InstallError::WriteFailed, "the library cannot be made");
    }
    RAWFRAME_TRY_ASSIGN(content::Library library, content::Library::directory(root));
    Installation made{root, std::move(library), limits};
    made.limits_.retained = std::max<std::size_t>(made.limits_.retained, 1);
    if (fs::exists(root / content::kInstalledName, error)) {
        RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kText,
                            readFile(root / content::kInstalledName, kMaximumInstalled, "the installed pointer"));
        RAWFRAME_TRY_ASSIGN(made.installed_, readInstalled(textOf(kText)));
    }
    return made;
}

Inventory Installation::inventory() const {
    Inventory held;
    std::error_code error;
    for (const fs::directory_entry& prefix : fs::directory_iterator(root_ / "sha256", error)) {
        for (const fs::directory_entry& blob : fs::directory_iterator(prefix.path(), error)) {
            const auto kDigest = content::ContentDigest::parse("sha256:" + prefix.path().filename().string() +
                                                               blob.path().filename().string());
            if (kDigest.has_value() && blob.is_regular_file(error) &&
                content::blobPathOf(*kDigest) == fs::relative(blob.path(), root_, error).generic_string()) {
                held.insert(kDigest->bytes);
            }
        }
    }
    return held;
}

result::Status Installation::fetch(const UpdatePlan& plan, Origin& origin, UpdateReport& report) {
    if (plan.bytes() > limits_.maximumFetchBytes) {
        return failed(result::ErrorClass::ResourceExhausted,
                      InstallError::OverLimit,
                      "the update would fetch more than its limit");
    }
    for (const PlanEntry& entry : plan.entries) {
        auto fetched = origin.blob(entry.blob, entry.blobSize);
        if (!fetched.has_value()) {
            return std::unexpected<result::Error>{std::move(fetched).error().withContext("blob", entry.blob.text())};
        }
        // Verified before it is published: a blob in the store is always
        // what its digest says.
        if (fetched->size() != entry.blobSize ||
            !content::sameDigest(content::ContentDigest::of(*fetched), entry.blob)) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::DataLoss,
                             kInstallDomain,
                             code(InstallError::FetchedWrong),
                             "the origin served a blob that is not what the manifest says")
                    .error()
                    .withContext("blob", entry.blob.text())};
        }
        RAWFRAME_TRY(publishFile(root_ / content::kStagingName / (entry.blob.text().substr(7) + ".part"),
                                 root_ / content::blobPathOf(entry.blob),
                                 *fetched));
        ++report.fetched;
        report.fetchedBytes += fetched->size();
    }
    return {};
}

result::Status Installation::verify(const content::BuildManifest& manifest, Origin& origin, UpdateReport& report) {
    RAWFRAME_TRY_ASSIGN(const std::vector<content::ContentDigest> kDamaged, library_.damaged(manifest));
    if (kDamaged.empty()) {
        return {};
    }
    // A held blob that does not verify is never trusted: removed and
    // fetched again through the same plan.
    std::error_code error;
    for (const content::ContentDigest& blob : kDamaged) {
        fs::remove(root_ / content::blobPathOf(blob), error);
    }
    const UpdatePlan kPlan = planUpdate(std::span{&manifest, 1}, inventory());
    const std::size_t kBefore = report.fetched;
    RAWFRAME_TRY(fetch(kPlan, origin, report));
    report.healed += report.fetched - kBefore;
    RAWFRAME_TRY_ASSIGN(const std::vector<content::ContentDigest> kStill, library_.damaged(manifest));
    if (!kStill.empty()) {
        return failed(result::ErrorClass::DataLoss,
                      InstallError::FetchedWrong,
                      "the origin's blobs do not make the Build its manifest says");
    }
    return {};
}

result::Result<UpdateReport> Installation::add(const content::BuildReference& build, Origin& origin) {
    return install(std::span{&build, 1}, origin);
}

result::Result<UpdateReport> Installation::install(std::span<const content::BuildReference> builds, Origin& origin) {
    std::vector<Target> targets;
    for (const content::BuildReference& reference : builds) {
        const std::string_view kPublisher = content::publisherOf(reference.subject);
        RAWFRAME_TRY_ASSIGN(const signature::PublisherKeySet kKeys, library_.keys(kPublisher));
        Target target;
        std::error_code error;
        if (fs::exists(root_ / content::buildDirectoryOf(reference.build), error)) {
            RAWFRAME_TRY_ASSIGN(target.manifest, library_.manifest(reference.build, kKeys));
        } else {
            RAWFRAME_TRY_ASSIGN(target.manifestBytes, origin.manifest(reference.build, content::kMaximumBuildManifest));
            RAWFRAME_TRY_ASSIGN(target.signatureBytes,
                                origin.signature(reference.build, content::kMaximumBuildSignature));
            RAWFRAME_TRY_ASSIGN(
                target.manifest,
                content::readBuildManifest(target.manifestBytes, target.signatureBytes, reference.build, kKeys));
            target.fetched = true;
        }
        RAWFRAME_TRY(matches(target.manifest, reference));
        targets.push_back(std::move(target));
    }
    std::vector<content::BuildManifest> manifests;
    for (const Target& target : targets) {
        manifests.push_back(target.manifest);
    }
    UpdateReport report;
    RAWFRAME_TRY(fetch(planUpdate(manifests, inventory()), origin, report));
    for (const Target& target : targets) {
        RAWFRAME_TRY(verify(target.manifest, origin, report));
    }
    // Each fetched Build's directory published whole, after its blobs.
    for (const Target& target : targets) {
        if (!target.fetched) {
            continue;
        }
        const std::string kHex = hexOf(target.manifest.root);
        const fs::path kStaged = root_ / content::kStagingName / kHex;
        RAWFRAME_TRY(
            publishFile(kStaged / "manifest.part", kStaged / content::kBuildManifestName, target.manifestBytes));
        RAWFRAME_TRY(
            publishFile(kStaged / "signature.part", kStaged / content::kBuildSignatureName, target.signatureBytes));
        std::error_code error;
        const fs::path kPath = root_ / content::buildDirectoryOf(target.manifest.root);
        if (!fs::exists(kPath, error)) {
            fs::rename(kStaged, kPath, error);
            if (error) {
                return failed(
                    result::ErrorClass::Unavailable, InstallError::WriteFailed, "a Build cannot be put in place");
            }
            flushDirectory(kPath.parent_path());
        }
        fs::remove_all(kStaged, error);
    }
    return report;
}

result::Result<UpdateReport> Installation::update(std::string_view record, Origin& origin) {
    RAWFRAME_TRY_ASSIGN(const content::CompositionRecord kRecord, content::readComposition(record));
    std::vector<content::BuildReference> builds = {kRecord.game};
    builds.insert(builds.end(), kRecord.packages.begin(), kRecord.packages.end());
    builds.insert(builds.end(), kRecord.mods.begin(), kRecord.mods.end());
    RAWFRAME_TRY_ASSIGN(const UpdateReport kReport, install(builds, origin));
    const base::Sha256Digest kId = content::compositionIdOf(record);
    if (installed_.active == kId) {
        return kReport;
    }
    RAWFRAME_TRY(publishFile(root_ / content::kStagingName / (hexOf(kId) + ".part"),
                             root_ / content::compositionPathOf(kId),
                             bytesOf(record)));
    Installed next{.active = kId, .retained = {}};
    if (installed_.active.has_value()) {
        next.retained.push_back(*installed_.active);
    }
    for (const base::Sha256Digest& each : installed_.retained) {
        if (each != kId && next.retained.size() < limits_.retained) {
            next.retained.push_back(each);
        }
    }
    RAWFRAME_TRY(point(std::move(next)));
    return kReport;
}

result::Result<std::vector<content::BuildReference>>
Installation::buildsOf(const base::Sha256Digest& composition) const {
    RAWFRAME_TRY_ASSIGN(const std::vector<std::byte> kText,
                        readFile(root_ / content::compositionPathOf(composition),
                                 kMaximumComposition,
                                 "the library keeps no such Composition"));
    const auto kRecord = content::readComposition(textOf(kText));
    if (!kRecord.has_value() || content::compositionIdOf(textOf(kText)) != composition) {
        return failed(result::ErrorClass::DataLoss,
                      InstallError::LibraryInvalid,
                      "a kept Composition is not the one it is kept as");
    }
    std::vector<content::BuildReference> builds = {kRecord->game};
    builds.insert(builds.end(), kRecord->packages.begin(), kRecord->packages.end());
    builds.insert(builds.end(), kRecord->mods.begin(), kRecord->mods.end());
    return builds;
}

bool Installation::whole(const content::BuildReference& build) const {
    const auto kKeys = library_.keys(content::publisherOf(build.subject));
    if (!kKeys.has_value()) {
        return false;
    }
    const auto kManifest = library_.manifest(build.build, *kKeys);
    if (!kManifest.has_value()) {
        return false;
    }
    const auto kDamaged = library_.damaged(*kManifest);
    return kDamaged.has_value() && kDamaged->empty();
}

result::Status Installation::rollback() {
    if (installed_.retained.empty()) {
        return failed(result::ErrorClass::FailedPrecondition,
                      InstallError::NothingToRollBack,
                      "no Composition is retained to roll back to");
    }
    const base::Sha256Digest kTarget = installed_.retained.front();
    // Whole before it is pointed at: every Build held and verified.
    const auto kNotWhole = [] {
        return failed(result::ErrorClass::FailedPrecondition,
                      InstallError::NothingToRollBack,
                      "the retained Composition's Builds are not whole");
    };
    RAWFRAME_TRY_ASSIGN(const std::vector<content::BuildReference> kBuilds, buildsOf(kTarget));
    for (const content::BuildReference& build : kBuilds) {
        if (!whole(build)) {
            return kNotWhole();
        }
    }
    Installed next{.active = kTarget, .retained = {}};
    if (installed_.active.has_value()) {
        next.retained.push_back(*installed_.active);
    }
    next.retained.insert(next.retained.end(), installed_.retained.begin() + 1, installed_.retained.end());
    return point(std::move(next));
}

result::Result<UpdateReport> Installation::heal(Origin& origin) {
    if (!installed_.active.has_value()) {
        return UpdateReport{};
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<content::BuildReference> kBuilds, buildsOf(*installed_.active));
    return install(kBuilds, origin);
}

result::Result<std::size_t> Installation::collect() {
    std::set<std::string> builds;
    std::set<base::Sha256Digest> blobs;
    std::set<std::string> compositions;
    std::vector<base::Sha256Digest> kept = installed_.retained;
    if (installed_.active.has_value()) {
        kept.insert(kept.begin(), *installed_.active);
    }
    for (const base::Sha256Digest& composition : kept) {
        compositions.insert(hexOf(composition));
        RAWFRAME_TRY_ASSIGN(const std::vector<content::BuildReference> kBuilds, buildsOf(composition));
        for (const content::BuildReference& build : kBuilds) {
            builds.insert(hexOf(build.build));
            RAWFRAME_TRY_ASSIGN(const signature::PublisherKeySet kKeys,
                                library_.keys(content::publisherOf(build.subject)));
            RAWFRAME_TRY_ASSIGN(const content::BuildManifest kManifest, library_.manifest(build.build, kKeys));
            for (const std::vector<content::BuildChunk>& chunks : kManifest.chunks) {
                for (const content::BuildChunk& chunk : chunks) {
                    blobs.insert(chunk.blob.bytes);
                }
            }
        }
    }
    std::error_code error;
    for (const fs::directory_entry& each : fs::directory_iterator(root_ / "builds", error)) {
        if (!builds.contains(each.path().filename().string())) {
            fs::remove_all(each.path(), error);
        }
    }
    for (const fs::directory_entry& each : fs::directory_iterator(root_ / "compositions", error)) {
        if (!compositions.contains(each.path().filename().string())) {
            fs::remove(each.path(), error);
        }
    }
    std::size_t removed = 0;
    for (const base::Sha256Digest& held : inventory()) {
        if (!blobs.contains(held)) {
            removed += fs::remove(root_ / content::blobPathOf(content::ContentDigest{.bytes = held}), error) ? 1 : 0;
        }
    }
    if (error) {
        return failed(result::ErrorClass::Unavailable, InstallError::WriteFailed, "the library cannot be collected");
    }
    return removed;
}

result::Status Installation::point(Installed next) {
    RAWFRAME_TRY_ASSIGN(const std::string kRecord, writeInstalled(next));
    RAWFRAME_TRY(publishFile(
        root_ / content::kStagingName / "installed.part", root_ / content::kInstalledName, bytesOf(kRecord)));
    installed_ = std::move(next);
    return {};
}

} // namespace rawframe::install
