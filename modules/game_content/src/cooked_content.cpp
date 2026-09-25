#include "rawframe/game_content/cooked_content.h"

#include "rawframe/content/composition_record.h"
#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"
#include "rawframe/content/product.h"

#include <algorithm>
#include <iterator>

#if RAWFRAME_FILE_SYSTEM
#include <fstream>
#endif

namespace rawframe::game_content {

namespace {

#if RAWFRAME_FILE_SYSTEM
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
#endif

} // namespace

result::Result<std::unique_ptr<CookedContent>> CookedContent::none(execution::Executor& blockingIo,
                                                                   execution::OwnerId owner,
                                                                   execution::CancellationScope& parent,
                                                                   const execution::MonotonicSource& clock) {
    std::unique_ptr<CookedContent> made{new CookedContent};
    RAWFRAME_TRY_ASSIGN(made->store_, content::ContentStore::create(blockingIo, owner, parent, clock, {}));
    return made;
}

#if RAWFRAME_FILE_SYSTEM
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
    return compose(
        blockingIo,
        owner,
        parent,
        clock,
        record,
        [&library](std::string_view publisher) -> result::Result<std::string> {
            return readText(library / "keys" / (std::string{publisher} + ".keys"));
        },
        [&library](std::string_view root, const base::Sha256Digest& digest, const signature::PublisherKeySet& keys) {
            return content::ContentSource::build(library / "builds" / std::string{root}, digest, keys);
        });
}
#endif

result::Result<std::unique_ptr<CookedContent>> CookedContent::openComposition(execution::Executor& blockingIo,
                                                                              execution::OwnerId owner,
                                                                              execution::CancellationScope& parent,
                                                                              const execution::MonotonicSource& clock,
                                                                              std::string_view record,
                                                                              HeldLibrary library) {
    return compose(
        blockingIo,
        owner,
        parent,
        clock,
        record,
        [&library](std::string_view publisher) -> result::Result<std::string> {
            const std::string kPath = "keys/" + std::string{publisher} + ".keys";
            const auto kFound = std::ranges::find(library, kPath, &HeldLibrary::value_type::first);
            if (kFound == library.end()) {
                return std::string{};
            }
            return std::string{reinterpret_cast<const char*>(kFound->second.data()), kFound->second.size()};
        },
        [&library](std::string_view root, const base::Sha256Digest& digest, const signature::PublisherKeySet& keys) {
            // The Build's own files, by their paths within it.
            const std::string kPrefix = "builds/" + std::string{root} + "/";
            HeldLibrary files;
            for (const auto& [path, bytes] : library) {
                if (path.starts_with(kPrefix)) {
                    files.emplace_back(path.substr(kPrefix.size()), bytes);
                }
            }
            return content::ContentSource::build(std::move(files), digest, keys);
        });
}

result::Result<std::unique_ptr<CookedContent>> CookedContent::compose(execution::Executor& blockingIo,
                                                                      execution::OwnerId owner,
                                                                      execution::CancellationScope& parent,
                                                                      const execution::MonotonicSource& clock,
                                                                      std::string_view record,
                                                                      const KeysOf& keysOf,
                                                                      const BuildOf& buildOf) {
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
        RAWFRAME_TRY_ASSIGN(const std::string kKeysText, keysOf(kPublisher));
        auto keys = signature::readPublisherKeySet(kKeysText);
        if (!keys.has_value()) {
            return std::unexpected<result::Error>{std::move(keys).error().withContext("publisher", kPublisher)};
        }
        auto opened = buildOf(kRoot, reference->build, *keys);
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
#if !RAWFRAME_FILE_SYSTEM
    return false;
#else
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
#endif
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
