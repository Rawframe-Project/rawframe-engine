// The store against SPEC-0008's read algorithm: bytes returned only when
// exactly the declared length and digest, typed and pinned refusals before
// any input or output, memory and directory sources alike, a directory
// source no link can lead out of, reads that finish in the generation they
// began in, and cancellation and deadlines that stop publication.

#include "rawframe/content/errors.h"
#include "rawframe/content/store.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace rawframe;
using namespace rawframe::content;

namespace {

constexpr ResourceTypeId kSoundType{base::Bits128{.high = 0x50, .low = 1}};
constexpr ResourceTypeId kTextureType{base::Bits128{.high = 0x50, .low = 2}};
constexpr execution::OwnerId kOwner{11};

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

ResourceId idOf(std::uint64_t value) {
    return ResourceId{base::Bits128{.high = 0, .low = value}};
}

ManifestEntry entry(std::uint64_t id, std::string locator, std::string_view bytes) {
    const std::vector<std::byte> kBytes = bytesOf(bytes);
    return ManifestEntry{.id = idOf(id),
                         .type = kSoundType,
                         .representation = *RepresentationId::parse("rawframe.audio.opus"),
                         .byteLength = kBytes.size(),
                         .digest = ContentDigest::of(kBytes),
                         .locator = std::move(locator)};
}

std::shared_ptr<const ContentCatalog> catalogOf(std::vector<ManifestEntry> entries, std::uint64_t generation) {
    const std::vector<AdmittedRepresentation> kAdmitted = {
        {.type = kSoundType, .representation = *RepresentationId::parse("rawframe.audio.opus")}};
    const std::vector<BoundManifest> kManifests = {BoundManifest{.entries = std::move(entries), .source = 0}};
    return *ContentCatalog::build(kManifests, kAdmitted, 1, generation);
}

/// An executor, a scope, and a store over one source, as a Runtime would
/// own them.
struct Fixture {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 2}};
    std::unique_ptr<ContentStore> store;

    explicit Fixture(ContentSource source) {
        RAWFRAME_EXPECT(io.admitOwner(kOwner, {.maximumPendingTasks = 64}).has_value());
        std::vector<ContentSource> sources;
        sources.push_back(std::move(source));
        store = std::move(*ContentStore::create(io, kOwner, root, clock, std::move(sources)));
    }
    ~Fixture() {
        store.reset();
        io.stop();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
};

/// The read's outcome, waited for; the error's code, or nought for bytes.
std::pair<std::uint32_t, std::string> outcomeOf(result::Result<execution::AsyncHandle<VerifiedContent>> started) {
    if (!started.has_value()) {
        return {started.error().code().value, "refused"};
    }
    auto outcome = started->wait();
    if (outcome.isCancelled()) {
        return {code(ContentError::Cancelled).value, "cancelled"};
    }
    if (outcome.isError()) {
        return {outcome.error().code().value, "failed"};
    }
    const std::span<const std::byte> kBytes = (*outcome).bytes();
    return {0, std::string{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()}};
}

std::pair<std::uint32_t, std::string> read(std::string_view bytes) {
    return {0, std::string{bytes}};
}

std::pair<std::uint32_t, std::string> failure(ContentError error, std::string_view how) {
    return {code(error).value, std::string{how}};
}

} // namespace

RAWFRAME_TEST(AReadReturnsExactlyTheDeclaredBytes) {
    auto source = ContentSource::memory({{"shot.rfopus", bytesOf("bang")},
                                         {"wrong.rfopus", bytesOf("bong")},
                                         {"short.rfopus", bytesOf("ban")},
                                         {"long.rfopus", bytesOf("banging")}});
    Fixture fixture{std::move(*source)};
    ContentStore& store = *fixture.store;
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType})) ==
                    failure(ContentError::SourceUnavailable, "refused"));
    store.publish(catalogOf({entry(1, "shot.rfopus", "bang"),
                             entry(2, "wrong.rfopus", "bang"),
                             entry(3, "short.rfopus", "bang"),
                             entry(4, "long.rfopus", "bang"),
                             entry(5, "missing.rfopus", "bang")},
                            1));
    const ResourceRef kShot{.id = idOf(1), .type = kSoundType};
    auto started = store.read(kShot);
    RAWFRAME_EXPECT(started.has_value());
    if (started.has_value()) {
        auto outcome = started->wait();
        RAWFRAME_EXPECT(outcome.hasValue() && (*outcome).bytes().size() == 4 && (*outcome).generation() == 1 &&
                        (*outcome).descriptor().id == idOf(1) &&
                        (*outcome).fingerprint() == store.catalog()->fingerprint());
    }
    // Refused before any read.
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kTextureType})) ==
                    failure(ContentError::ResourceTypeMismatch, "refused"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(9), .type = kSoundType})) ==
                    failure(ContentError::ResourceNotFound, "refused"));
    RAWFRAME_EXPECT(outcomeOf(store.read(kShot, {.maximumBytes = 3})) ==
                    failure(ContentError::ResourceTooLarge, "refused"));
    ContentDigest other = entry(1, "x", "bong").digest;
    RAWFRAME_EXPECT(outcomeOf(store.read(PinnedResourceRef{.resource = kShot, .digest = other})) ==
                    failure(ContentError::RevisionMismatch, "refused"));
    RAWFRAME_EXPECT(
        outcomeOf(store.read(PinnedResourceRef{.resource = kShot, .digest = entry(1, "x", "bang").digest})).first == 0);
    // Read, and refused for what the bytes turned out to be.
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(2), .type = kSoundType})) ==
                    failure(ContentError::DigestMismatch, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(3), .type = kSoundType})) ==
                    failure(ContentError::ShortRead, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(4), .type = kSoundType})) ==
                    failure(ContentError::SourceChanged, "failed"));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(5), .type = kSoundType})) ==
                    failure(ContentError::ReadFailed, "failed"));
    RAWFRAME_EXPECT(!ContentSource::memory({{"../up", {}}}).has_value());
}

RAWFRAME_TEST(ADirectorySourceReadsOnlyWithinItsRoot) {
    const std::filesystem::path kBase =
        std::filesystem::temp_directory_path() / ("rawframe-content-" + std::to_string(::getpid()));
    const std::filesystem::path kRoot = kBase / "root";
    std::filesystem::create_directories(kRoot / "sounds");
    std::filesystem::create_directories(kBase / "outside");
    std::ofstream{kRoot / "sounds" / "shot.rfopus"} << "bang";
    std::ofstream{kBase / "outside" / "secret.rfopus"} << "bang";
    // A directory link and a file link, both out of the root.
    std::filesystem::create_directory_symlink(kBase / "outside", kRoot / "escape");
    std::filesystem::create_symlink(kBase / "outside" / "secret.rfopus", kRoot / "sounds" / "linked.rfopus");
    {
        Fixture fixture{std::move(*ContentSource::directory(kRoot))};
        ContentStore& store = *fixture.store;
        store.publish(catalogOf({entry(1, "sounds/shot.rfopus", "bang"),
                                 entry(2, "escape/secret.rfopus", "bang"),
                                 entry(3, "sounds/linked.rfopus", "bang"),
                                 entry(4, "sounds/none.rfopus", "bang")},
                                1));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType})) == read("bang"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(2), .type = kSoundType})) ==
                        failure(ContentError::PathEscape, "failed"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(3), .type = kSoundType})) ==
                        failure(ContentError::PathEscape, "failed"));
        RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(4), .type = kSoundType})) ==
                        failure(ContentError::ReadFailed, "failed"));
    }
    // The same catalog over the same bytes in memory reads the same.
    Fixture inMemory{std::move(*ContentSource::memory({{"sounds/shot.rfopus", bytesOf("bang")}}))};
    inMemory.store->publish(catalogOf({entry(1, "sounds/shot.rfopus", "bang")}, 1));
    RAWFRAME_EXPECT(outcomeOf(inMemory.store->read(ResourceRef{.id = idOf(1), .type = kSoundType})) == read("bang"));
    RAWFRAME_EXPECT(!ContentSource::directory(kBase / "nowhere").has_value());
    std::filesystem::remove_all(kBase);
}

RAWFRAME_TEST(AReadFinishesInTheGenerationItBeganIn) {
    Fixture fixture{std::move(*ContentSource::memory({{"a.rfopus", bytesOf("one")}, {"b.rfopus", bytesOf("two")}}))};
    ContentStore& store = *fixture.store;
    store.publish(catalogOf({entry(1, "a.rfopus", "one")}, 1));
    auto first = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
    // Replaced while the first read may still run: the same resource now
    // lives elsewhere with other bytes.
    store.publish(catalogOf({entry(1, "b.rfopus", "two")}, 2));
    auto second = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
    RAWFRAME_EXPECT(first.has_value() && second.has_value());
    if (first.has_value() && second.has_value()) {
        auto firstOutcome = first->wait();
        auto secondOutcome = second->wait();
        RAWFRAME_EXPECT(firstOutcome.hasValue() && (*firstOutcome).generation() == 1 &&
                        (*firstOutcome).bytes().size() == 3 &&
                        std::to_integer<char>((*firstOutcome).bytes()[0]) == 'o');
        RAWFRAME_EXPECT(secondOutcome.hasValue() && (*secondOutcome).generation() == 2 &&
                        std::to_integer<char>((*secondOutcome).bytes()[0]) == 't');
    }
    // Many at once, on two workers, all verified.
    std::vector<execution::AsyncHandle<VerifiedContent>> many;
    for (int read = 0; read < 32; ++read) {
        auto started = store.read(ResourceRef{.id = idOf(1), .type = kSoundType});
        if (started.has_value()) {
            many.push_back(std::move(*started));
        }
    }
    bool allVerified = many.size() == 32;
    for (auto& handle : many) {
        allVerified = allVerified && handle.wait().hasValue();
    }
    RAWFRAME_EXPECT(allVerified);
}

RAWFRAME_TEST(CancellationAndDeadlinesStopPublication) {
    Fixture fixture{std::move(*ContentSource::memory({{"a.rfopus", bytesOf("one")}}))};
    ContentStore& store = *fixture.store;
    store.publish(catalogOf({entry(1, "a.rfopus", "one")}, 1));
    // A deadline already passed: the bytes are never handed out.
    fixture.clock.advance(execution::MonotonicDuration::fromMilliseconds(10));
    RAWFRAME_EXPECT(outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType},
                                         {.deadline = execution::MonotonicInstant{0}})) ==
                    failure(ContentError::DeadlineExceeded, "failed"));
    // The Runtime cancelling: refused or cancelled, never published.
    fixture.root.cancel(execution::CancelReason::Requested);
    const auto kCancelled = outcomeOf(store.read(ResourceRef{.id = idOf(1), .type = kSoundType}));
    RAWFRAME_EXPECT(kCancelled.first != 0);
}
