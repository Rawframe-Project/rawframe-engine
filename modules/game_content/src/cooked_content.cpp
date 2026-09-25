#include "rawframe/game_content/cooked_content.h"

#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/product.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace rawframe::game_content {

namespace {

constexpr std::string_view kManifestName = "content.manifest";

std::string readText(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

result::Result<std::vector<content::ManifestEntry>> entriesOf(std::string_view text,
                                                              const std::filesystem::path& root) {
    auto entries = content::readManifest(text);
    if (!entries.has_value()) {
        return std::unexpected<result::Error>{
            std::move(entries).error().withContext("path", (root / kManifestName).string())};
    }
    return entries;
}

} // namespace

result::Result<std::unique_ptr<CookedContent>> CookedContent::open(execution::Executor& blockingIo,
                                                                   execution::OwnerId owner,
                                                                   execution::CancellationScope& parent,
                                                                   const execution::MonotonicSource& clock,
                                                                   std::optional<std::filesystem::path> root) {
    std::unique_ptr<CookedContent> made{new CookedContent};
    std::vector<content::ContentSource> sources;
    if (root.has_value()) {
        RAWFRAME_TRY_ASSIGN(content::ContentSource source, content::ContentSource::directory(*root));
        sources.push_back(std::move(source));
        made->manifestText_ = readText(*root / kManifestName);
        RAWFRAME_TRY_ASSIGN(std::vector<content::ManifestEntry> entries, entriesOf(made->manifestText_, *root));
        made->manifests_.push_back(std::move(entries));
    }
    RAWFRAME_TRY_ASSIGN(made->store_,
                        content::ContentStore::create(blockingIo, owner, parent, clock, std::move(sources)));
    made->root_ = std::move(root);
    made->held_ = made->root_.has_value();
    if (made->held_) {
        RAWFRAME_TRY(made->publish(made->manifests_));
    }
    return made;
}

result::Result<std::unique_ptr<CookedContent>> CookedContent::openComposition(execution::Executor& blockingIo,
                                                                              execution::OwnerId owner,
                                                                              execution::CancellationScope& parent,
                                                                              const execution::MonotonicSource& clock,
                                                                              std::string_view record,
                                                                              const std::filesystem::path& library) {
    RAWFRAME_TRY_ASSIGN(const content::CompositionRecord kRecord, content::readComposition(record));
    if (!kRecord.mods.empty()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                           content::kContentDomain,
                                                           code(content::ContentError::ManifestInvalid),
                                                           "a Composition with mods waits for mod policy")
                                                  .error()};
    }
    std::vector<const content::BuildReference*> builds = {&kRecord.game};
    for (const content::BuildReference& each : kRecord.packages) {
        builds.push_back(&each);
    }
    std::unique_ptr<CookedContent> made{new CookedContent};
    std::vector<content::ContentSource> sources;
    for (const content::BuildReference* reference : builds) {
        const std::string kRoot = content::ContentDigest{.bytes = reference->build}.text().substr(7);
        const std::string kPublisher{content::publisherOf(reference->subject)};
        const std::string kKeysText = readText(library / "keys" / (kPublisher + ".keys"));
        auto keys = signature::readPublisherKeySet(kKeysText);
        if (!keys.has_value()) {
            return std::unexpected<result::Error>{std::move(keys).error().withContext("publisher", kPublisher)};
        }
        auto opened = content::ContentSource::build(library / "builds" / kRoot, reference->build, *keys);
        if (!opened.has_value()) {
            return std::unexpected<result::Error>{std::move(opened).error().withContext("build", kRoot)};
        }
        if (opened->subject != reference->subject || opened->version != reference->version) {
            return std::unexpected<result::Error>{result::fail(result::ErrorClass::InvalidArgument,
                                                               content::kContentDomain,
                                                               code(content::ContentError::ManifestInvalid),
                                                               "a Build is not the subject and version named")
                                                      .error()
                                                      .withContext("build", kRoot)};
        }
        sources.push_back(std::move(opened->source));
        made->manifests_.push_back(std::move(opened->entries));
    }
    RAWFRAME_TRY_ASSIGN(made->store_,
                        content::ContentStore::create(blockingIo, owner, parent, clock, std::move(sources)));
    made->held_ = true;
    made->compositionId_ = content::compositionIdOf(record);
    RAWFRAME_TRY(made->publish(made->manifests_));
    return made;
}

content::ContentStore& CookedContent::store() noexcept {
    return *store_;
}

result::Status CookedContent::admit(std::span<const content::AdmittedRepresentation> representations) {
    if (!held_) {
        return std::unexpected<result::Error>{
            result::fail(result::ErrorClass::FailedPrecondition,
                         content::kContentDomain,
                         code(content::ContentError::SourceUnavailable),
                         "the process has no cooked content (content.root or content.composition)")
                .error()};
    }
    for (const content::AdmittedRepresentation& each : representations) {
        const bool kKnown = std::ranges::any_of(admitted_, [&each](const content::AdmittedRepresentation& other) {
            return other.type == each.type && other.representation == each.representation;
        });
        if (!kKnown) {
            admitted_.push_back(each);
        }
    }
    return publish(manifests_);
}

result::Result<bool> CookedContent::refresh() {
    if (!root_.has_value()) {
        return false;
    }
    std::string text = readText(*root_ / kManifestName);
    if (text == manifestText_) {
        return false;
    }
    // Read once per change: a manifest refused now is not read again until
    // it changes again.
    manifestText_ = std::move(text);
    RAWFRAME_TRY_ASSIGN(std::vector<content::ManifestEntry> entries, entriesOf(manifestText_, *root_));
    std::vector<std::vector<content::ManifestEntry>> manifests;
    manifests.push_back(std::move(entries));
    RAWFRAME_TRY(publish(manifests));
    manifests_ = std::move(manifests);
    return true;
}

std::uint64_t CookedContent::generation() const noexcept {
    return generation_;
}

const std::optional<base::Sha256Digest>& CookedContent::compositionId() const noexcept {
    return compositionId_;
}

result::Status CookedContent::publish(const std::vector<std::vector<content::ManifestEntry>>& manifests) {
    // A resource of a family this process does not admit is left out: a
    // process holds what it can read, and the cook's output is shared by
    // every kind of process a game has.
    std::vector<content::BoundManifest> bound;
    for (std::size_t source = 0; source < manifests.size(); ++source) {
        bound.push_back(content::BoundManifest{.entries = {}, .source = source});
        std::ranges::copy_if(
            manifests[source], std::back_inserter(bound.back().entries), [this](const content::ManifestEntry& entry) {
                return std::ranges::any_of(admitted_, [&entry](const content::AdmittedRepresentation& each) {
                    return each.type == entry.type && each.representation == entry.representation;
                });
            });
    }
    RAWFRAME_TRY_ASSIGN(std::shared_ptr<const content::ContentCatalog> catalog,
                        content::ContentCatalog::build(bound, admitted_, manifests.size(), generation_ + 1));
    ++generation_;
    store_->publish(std::move(catalog));
    return {};
}

} // namespace rawframe::game_content
