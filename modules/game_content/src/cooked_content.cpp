#include "rawframe/game_content/cooked_content.h"

#include "rawframe/content/errors.h"
#include "rawframe/content/manifest.h"

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
        RAWFRAME_TRY_ASSIGN(made->entries_, entriesOf(made->manifestText_, *root));
    }
    RAWFRAME_TRY_ASSIGN(made->store_,
                        content::ContentStore::create(blockingIo, owner, parent, clock, std::move(sources)));
    made->root_ = std::move(root);
    if (made->root_.has_value()) {
        RAWFRAME_TRY(made->publish(made->entries_));
    }
    return made;
}

content::ContentStore& CookedContent::store() noexcept {
    return *store_;
}

result::Status CookedContent::admit(std::span<const content::AdmittedRepresentation> representations) {
    if (!root_.has_value()) {
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::FailedPrecondition,
                                                           content::kContentDomain,
                                                           code(content::ContentError::SourceUnavailable),
                                                           "the process has no cooked content (content.root)")
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
    return publish(entries_);
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
    RAWFRAME_TRY(publish(entries));
    entries_ = std::move(entries);
    return true;
}

std::uint64_t CookedContent::generation() const noexcept {
    return generation_;
}

result::Status CookedContent::publish(const std::vector<content::ManifestEntry>& entries) {
    // A resource of a family this process does not admit is left out: a
    // process holds what it can read, and the cook's output is shared by
    // every kind of process a game has.
    content::BoundManifest bound{.entries = {}, .source = 0};
    std::ranges::copy_if(entries, std::back_inserter(bound.entries), [this](const content::ManifestEntry& entry) {
        return std::ranges::any_of(admitted_, [&entry](const content::AdmittedRepresentation& each) {
            return each.type == entry.type && each.representation == entry.representation;
        });
    });
    const std::span<const content::BoundManifest> kManifests{&bound, 1};
    RAWFRAME_TRY_ASSIGN(std::shared_ptr<const content::ContentCatalog> catalog,
                        content::ContentCatalog::build(kManifests, admitted_, 1, generation_ + 1));
    ++generation_;
    store_->publish(std::move(catalog));
    return {};
}

} // namespace rawframe::game_content
