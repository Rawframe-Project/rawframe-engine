// Decoded assets against SPEC-0027: requests that coalesce into one load
// with their own readiness, forms evicted in a deterministic order when the
// budget needs room and never while wanted, handles that fail typed once
// stale while forms shared before stay whole, sticky failures, deadlines
// that fail only their own requester, a load abandoned by everyone
// discarded, a closed scope that names what was still wanted, and a reload
// that replaces what changed in four phases.

#include "rawframe/assets/assets.h"
#include "rawframe/assets/errors.h"
#include "rawframe/content/errors.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if RAWFRAME_THREADS
#include <thread>
#endif

using namespace rawframe;
using namespace rawframe::assets;

namespace {

constexpr content::ResourceTypeId kTextType{base::Bits128{.high = 7, .low = 7}};
constexpr content::ResourceTypeId kOtherType{base::Bits128{.high = 7, .low = 8}};

#if RAWFRAME_THREADS
/// Holds every decode until opened, to catch a load in flight.
std::atomic<bool> gOpen{true};
/// Holds the decode of "bad" alone until opened.
std::atomic<bool> gBadOpen{true};
#endif

/// The test family: bytes to a string, one byte a byte; "bad" does not
/// decode.
result::Result<DecodedForm> decodeText(const content::VerifiedContent& content) {
#if RAWFRAME_THREADS
    while (!gOpen.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
#endif
    const std::span<const std::byte> kBytes = content.bytes();
    std::string text{reinterpret_cast<const char*>(kBytes.data()), kBytes.size()};
#if RAWFRAME_THREADS
    while (text == "bad" && !gBadOpen.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
#endif
    if (text == "bad") {
        return result::fail(result::ErrorClass::InvalidArgument, kAssetsDomain, code(AssetError::DecodeFailed), "bad");
    }
    const std::uint64_t kBytesHeld = text.size();
    return DecodedForm{.value = std::make_shared<const std::string>(std::move(text)), .bytes = kBytesHeld};
}

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
    return {kBytes.begin(), kBytes.end()};
}

content::ResourceRef refOf(std::uint64_t id) {
    return content::ResourceRef{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}}, .type = kTextType};
}

/// A store over text resources 1 to 6 and a set of the text family, as a
/// Runtime would own them. Resource 5's file is not what the catalog says.
/// Files `n2` to `n4` hold new revisions for a catalog to name.
struct Fixture {
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    execution::Executor io{execution::ExecutorSettings{.kind = execution::ExecutorKind::BlockingIo, .workers = 1}};
    execution::Executor cpu{execution::ExecutorSettings{.kind = execution::ExecutorKind::Cpu, .workers = 1}};
    std::unique_ptr<content::ContentStore> store;
    std::unique_ptr<AssetSet> set;
    /// What the catalog holds, as `publish` last built it.
    std::vector<content::ManifestEntry> entries;

    explicit Fixture(std::uint64_t budget = 1'000) {
        RAWFRAME_EXPECT(io.admitOwner(execution::OwnerId{1}, {.maximumPendingTasks = 64}).has_value());
        RAWFRAME_EXPECT(cpu.admitOwner(execution::OwnerId{2}, {.maximumPendingTasks = 64}).has_value());
        const std::vector<std::pair<std::uint64_t, std::string>> kTexts = {
            {1, "hello"}, {2, "abcd"}, {3, "efgh"}, {4, "ijkl"}, {5, "real"}, {6, "bad"}};
        std::vector<std::pair<std::string, std::vector<std::byte>>> files = {
            {"n2", bytesOf("wxyz")}, {"n3", bytesOf("bad")}, {"n4", bytesOf("ijk2")}};
        for (const auto& [kId, kText] : kTexts) {
            const std::string kLocator = "t" + std::to_string(kId);
            files.emplace_back(kLocator, bytesOf(kId == 5 ? "fake" : kText));
            entries.push_back(content::ManifestEntry{.id = refOf(kId).id,
                                                     .type = kTextType,
                                                     .representation = *content::RepresentationId::parse("test.text"),
                                                     .byteLength = kText.size(),
                                                     .digest = content::ContentDigest::of(bytesOf(kText)),
                                                     .locator = kLocator});
        }
        std::vector<content::ContentSource> sources;
        sources.push_back(std::move(*content::ContentSource::memory(std::move(files))));
        store = std::move(*content::ContentStore::create(io, execution::OwnerId{1}, root, clock, std::move(sources)));
        publish(1);
        set = std::move(*AssetSet::create(*store,
                                          cpu,
                                          execution::OwnerId{2},
                                          root,
                                          clock,
                                          {.type = kTextType, .decode = &decodeText, .budgetBytes = budget}));
    }
    ~Fixture() {
        set.reset();
        store.reset();
        cpu.stop();
        io.stop();
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    /// Publishes `entries` as the store's catalog of `generation`.
    void publish(std::uint64_t generation) const {
        const std::vector<content::AdmittedRepresentation> kAdmitted = {
            {.type = kTextType, .representation = *content::RepresentationId::parse("test.text")}};
        const std::vector<content::BoundManifest> kManifests = {{.entries = entries, .source = 0}};
        store->publish(*content::ContentCatalog::build(kManifests, kAdmitted, generation, generation));
    }

    /// Points resource `id` at file `locator`, holding `text`.
    void revise(std::uint64_t id, std::string_view locator, std::string_view text) {
        content::ManifestEntry& entry = entries[id - 1];
        entry.locator = std::string{locator};
        entry.byteLength = text.size();
        entry.digest = content::ContentDigest::of(bytesOf(text));
    }

    /// Updates until `requester` is no longer pending, within ten seconds.
    Readiness settle(RequesterId requester, std::uint64_t tick) {
        const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (set->readiness(requester) == Readiness::Pending && std::chrono::steady_clock::now() < kDeadline) {
            set->update(tick);
#if RAWFRAME_THREADS
            std::this_thread::yield();
#else
            // No workers: the loads run here, as a Host would run them.
            while (io.runOne() || cpu.runOne()) {
            }
#endif
        }
        return set->readiness(requester);
    }

    [[nodiscard]] std::string textOf(RequesterId requester, std::uint64_t tick) const {
        const auto kHandle = set->handle(requester);
        if (!kHandle.has_value()) {
            return "";
        }
        const auto kText = Assets<std::string>{*set}.get(*kHandle, tick);
        return kText.has_value() ? **kText : "";
    }
};

bool failedAs(const AssetSet& set, RequesterId requester, result::ErrorDomain domain, std::uint32_t value) {
    const result::Error* error = set.failure(requester);
    return set.readiness(requester) == Readiness::Failed && error != nullptr && error->domain() == domain &&
           error->code().value == value;
}

} // namespace

RAWFRAME_TEST(RequestsCoalesceIntoOneLoad) {
    Fixture fixture;
    const RequesterId kFirst = *fixture.set->request(refOf(1));
    const RequesterId kSecond = *fixture.set->request(refOf(1));
    RAWFRAME_EXPECT(fixture.set->readiness(kFirst) == Readiness::Pending);
    RAWFRAME_EXPECT(fixture.settle(kFirst, 1) == Readiness::Ready && fixture.settle(kSecond, 1) == Readiness::Ready);
    RAWFRAME_EXPECT(fixture.set->statistics().loads == 1 && fixture.set->statistics().coalesced == 1);
    RAWFRAME_EXPECT(fixture.set->handle(kFirst) == fixture.set->handle(kSecond) &&
                    fixture.textOf(kFirst, 1) == "hello");
    // One requester letting go leaves the form wanted; the last makes it
    // evictable, and a new request brings it back without another load.
    fixture.set->release(kFirst);
    RAWFRAME_EXPECT(fixture.set->readiness(kFirst) == Readiness::NotRequested &&
                    fixture.set->residency(*fixture.set->handle(kSecond)) == Residency::Resident);
    const AssetHandle kHandle = *fixture.set->handle(kSecond);
    fixture.set->release(kSecond);
    RAWFRAME_EXPECT(fixture.set->residency(kHandle) == Residency::Evictable);
    const RequesterId kAgain = *fixture.set->request(refOf(1));
    RAWFRAME_EXPECT(fixture.set->readiness(kAgain) == Readiness::Ready && fixture.set->statistics().loads == 1);
    // Refused before anything loads.
    RAWFRAME_EXPECT(!fixture.set->request(refOf(9)).has_value());
    RAWFRAME_EXPECT(!fixture.set->request(content::ResourceRef{.id = refOf(1).id, .type = kOtherType}).has_value());
}

RAWFRAME_TEST(TheBudgetEvictsTheLeastRecentlyUsedFirst) {
    // Room for two four-byte forms.
    Fixture fixture{8};
    const RequesterId kA = *fixture.set->request(refOf(2));
    const RequesterId kB = *fixture.set->request(refOf(3));
    RAWFRAME_EXPECT(fixture.settle(kA, 1) == Readiness::Ready && fixture.settle(kB, 1) == Readiness::Ready);
    const AssetHandle kHandleA = *fixture.set->handle(kA);
    const AssetHandle kHandleB = *fixture.set->handle(kB);
    // A kept shared across updates, as a mixer keeps a clip.
    const auto kShared = Assets<std::string>{*fixture.set}.share(kHandleA, 5);
    RAWFRAME_EXPECT(kShared.has_value() && **kShared == "abcd");
    // B used later than A; both then unwanted.
    static_cast<void>(fixture.textOf(kA, 5));
    static_cast<void>(fixture.textOf(kB, 9));
    fixture.set->release(kA);
    fixture.set->release(kB);
    const RequesterId kC = *fixture.set->request(refOf(4));
    RAWFRAME_EXPECT(fixture.settle(kC, 10) == Readiness::Ready && fixture.textOf(kC, 10) == "ijkl");
    // A went, B stayed: A's handle is stale, typed.
    const auto kStale = Assets<std::string>{*fixture.set}.get(kHandleA, 10);
    RAWFRAME_EXPECT(!kStale.has_value() && kStale.error().code() == code(AssetError::Evicted));
    const auto kStaleShare = Assets<std::string>{*fixture.set}.share(kHandleA, 10);
    RAWFRAME_EXPECT(!kStaleShare.has_value() && kStaleShare.error().code() == code(AssetError::Evicted));
    // What was shared before is still whole.
    RAWFRAME_EXPECT(kShared.has_value() && **kShared == "abcd");
    RAWFRAME_EXPECT(Assets<std::string>{*fixture.set}.get(kHandleB, 10).has_value());
    RAWFRAME_EXPECT(fixture.set->statistics().evictions == 1 && fixture.set->statistics().residentBytes == 8);
    // A form that is wanted is never evicted: with C and a new B wanted,
    // a third has no room.
    const RequesterId kB2 = *fixture.set->request(refOf(3));
    const RequesterId kHello = *fixture.set->request(refOf(1));
    RAWFRAME_EXPECT(fixture.settle(kHello, 11) == Readiness::Failed &&
                    failedAs(*fixture.set, kHello, kAssetsDomain, code(AssetError::LimitExceeded).value));
    RAWFRAME_EXPECT(fixture.set->readiness(kB2) == Readiness::Ready && fixture.set->readiness(kC) == Readiness::Ready);
}

RAWFRAME_TEST(FailuresAreStickyAndTyped) {
    Fixture fixture;
    const RequesterId kBad = *fixture.set->request(refOf(6));
    RAWFRAME_EXPECT(fixture.settle(kBad, 1) == Readiness::Failed &&
                    failedAs(*fixture.set, kBad, kAssetsDomain, code(AssetError::DecodeFailed).value));
    const RequesterId kBadAgain = *fixture.set->request(refOf(6));
    RAWFRAME_EXPECT(fixture.set->readiness(kBadAgain) == Readiness::Failed && fixture.set->statistics().loads == 1);
    // Bytes that are not the catalog's fail with the content layer's error.
    const RequesterId kFake = *fixture.set->request(refOf(5));
    RAWFRAME_EXPECT(
        fixture.settle(kFake, 1) == Readiness::Failed &&
        failedAs(*fixture.set, kFake, content::kContentDomain, code(content::ContentError::DigestMismatch).value));
}

// Decodes held open while the test goes on need a worker to hold them.
#if RAWFRAME_THREADS
RAWFRAME_TEST(ADeadlineFailsOnlyItsOwnRequester) {
    Fixture fixture;
    gOpen.store(false, std::memory_order_release);
    const RequesterId kHurried = *fixture.set->request(
        refOf(1), {.deadline = execution::MonotonicInstant{0} + execution::MonotonicDuration::fromMilliseconds(5)});
    const RequesterId kPatient = *fixture.set->request(refOf(1));
    fixture.clock.advance(execution::MonotonicDuration::fromMilliseconds(10));
    fixture.set->update(1);
    RAWFRAME_EXPECT(failedAs(*fixture.set, kHurried, kAssetsDomain, code(AssetError::DeadlineExceeded).value));
    RAWFRAME_EXPECT(fixture.set->readiness(kPatient) == Readiness::Pending);
    gOpen.store(true, std::memory_order_release);
    RAWFRAME_EXPECT(fixture.settle(kPatient, 2) == Readiness::Ready);
    // Releasing the failed requester counts nothing twice.
    fixture.set->release(kHurried);
    RAWFRAME_EXPECT(fixture.set->residency(*fixture.set->handle(kPatient)) == Residency::Resident);
}

RAWFRAME_TEST(ALoadNoOneWantsIsDiscarded) {
    Fixture fixture;
    gOpen.store(false, std::memory_order_release);
    const RequesterId kGone = *fixture.set->request(refOf(2));
    fixture.set->cancel(kGone);
    RAWFRAME_EXPECT(fixture.set->readiness(kGone) == Readiness::NotRequested && fixture.set->statistics().loading == 1);
    gOpen.store(true, std::memory_order_release);
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (fixture.set->statistics().loading > 0 && std::chrono::steady_clock::now() < kDeadline) {
        fixture.set->update(1);
        std::this_thread::yield();
    }
    // It finished and was dropped: nothing resident, nothing charged.
    const AssetStatistics kAfter = fixture.set->statistics();
    RAWFRAME_EXPECT(kAfter.loading == 0 && kAfter.resident == 0 && kAfter.evictable == 0 && kAfter.residentBytes == 0);
}

#endif

RAWFRAME_TEST(AClosedSetNamesWhatWasStillWanted) {
    Fixture fixture;
    const RequesterId kKept = *fixture.set->request(refOf(1));
    RAWFRAME_EXPECT(fixture.settle(kKept, 1) == Readiness::Ready);
    const AssetHandle kHandle = *fixture.set->handle(kKept);
    const std::vector<Survivor> kSurvivors = fixture.set->close();
    RAWFRAME_EXPECT(kSurvivors.size() == 1 && kSurvivors[0].id == refOf(1).id && kSurvivors[0].interest == 1);
    const auto kClosed = fixture.set->get(kHandle, 2);
    RAWFRAME_EXPECT(!kClosed.has_value() && kClosed.error().code() == code(AssetError::ScopeClosed));
    RAWFRAME_EXPECT(!fixture.set->request(refOf(1)).has_value());
}

#if RAWFRAME_THREADS
RAWFRAME_TEST(AReloadReplacesWhatChangedInFourPhases) {
    Fixture fixture;
    AssetSet& set = *fixture.set;
    const RequesterId kHello = *set.request(refOf(1));
    const RequesterId kAbcd = *set.request(refOf(2));
    const RequesterId kEfgh = *set.request(refOf(3));
    const RequesterId kIjkl = *set.request(refOf(4));
    RAWFRAME_EXPECT(fixture.settle(kHello, 1) == Readiness::Ready && fixture.settle(kAbcd, 1) == Readiness::Ready &&
                    fixture.settle(kEfgh, 1) == Readiness::Ready && fixture.settle(kIjkl, 1) == Readiness::Ready);
    const AssetHandle kOldAbcd = *set.handle(kAbcd);
    const AssetHandle kOldIjkl = *set.handle(kIjkl);
    // A consumer keeps the old form of 2, as a mixer keeps a clip; no one
    // wants 4 any more.
    std::shared_ptr<const std::string> kept = *Assets<std::string>{set}.share(kOldAbcd, 1);
    set.release(kIjkl);
    const std::uint64_t kBytesBefore = set.statistics().residentBytes;

    // A new catalog: 2 and 4 revised, 3 revised to bytes that do not decode,
    // 1 gone, and the rest as they were.
    fixture.revise(2, "n2", "wxyz");
    fixture.revise(3, "n3", "bad");
    fixture.revise(4, "n4", "ijk2");
    fixture.entries.erase(fixture.entries.begin());
    gOpen.store(false, std::memory_order_release);
    fixture.publish(2);
    for (std::uint64_t tick = 2; tick < 6; ++tick) {
        set.update(tick);
    }
    // Candidates are building: the live forms stay authoritative, and the
    // one no one wanted has retired.
    RAWFRAME_EXPECT(fixture.textOf(kAbcd, 6) == "abcd" && fixture.textOf(kEfgh, 6) == "efgh");
    const auto kGone = Assets<std::string>{set}.get(kOldIjkl, 6);
    RAWFRAME_EXPECT(!kGone.has_value() && kGone.error().code() == code(AssetError::RevisionRetired));
    const std::vector<ReloadEvent> kEarly = set.takeReloadEvents();
    RAWFRAME_EXPECT(kEarly.size() == 1 && kEarly[0].outcome == ReloadOutcome::Failed && kEarly[0].id == refOf(1).id &&
                    kEarly[0].failure.has_value());

    // One candidate ready and the other still building: nothing is
    // published yet.
    gBadOpen.store(false, std::memory_order_release);
    gOpen.store(true, std::memory_order_release);
    const auto kBuilt = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < kBuilt) {
        set.update(7);
        std::this_thread::yield();
    }
    RAWFRAME_EXPECT(fixture.textOf(kAbcd, 7) == "abcd" && set.takeReloadEvents().empty());
    // Every candidate settled: one publication.
    gBadOpen.store(true, std::memory_order_release);
    const auto kDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::vector<ReloadEvent> events;
    while (events.size() < 2 && std::chrono::steady_clock::now() < kDeadline) {
        set.update(7);
        std::ranges::move(set.takeReloadEvents(), std::back_inserter(events));
        std::this_thread::yield();
    }
    RAWFRAME_EXPECT(events.size() == 2);
    if (events.size() == 2) {
        // Slot order: 2's candidate is published, 3's fails and 3 stays.
        const ReloadEvent& published = events[0].outcome == ReloadOutcome::Published ? events[0] : events[1];
        const ReloadEvent& failed = events[0].outcome == ReloadOutcome::Failed ? events[0] : events[1];
        RAWFRAME_EXPECT(published.id == refOf(2).id &&
                        published.oldRevision == content::ContentDigest::of(bytesOf("abcd")) &&
                        published.newRevision == content::ContentDigest::of(bytesOf("wxyz")));
        RAWFRAME_EXPECT(failed.id == refOf(3).id && failed.failure.has_value() &&
                        failed.failure->code() == code(AssetError::DecodeFailed));
    }
    RAWFRAME_EXPECT(fixture.textOf(kAbcd, 8) == "wxyz" && fixture.textOf(kEfgh, 8) == "efgh" &&
                    fixture.textOf(kHello, 8) == "hello");
    const auto kRetired = Assets<std::string>{set}.get(kOldAbcd, 8);
    RAWFRAME_EXPECT(!kRetired.has_value() && kRetired.error().code() == code(AssetError::RevisionRetired));
    // The old form of 2 is still held, and still charged, until let go.
    RAWFRAME_EXPECT(*kept == "abcd" && set.statistics().retiring == 1 && set.statistics().reloads == 1 &&
                    set.statistics().reloadFailures == 2);
    RAWFRAME_EXPECT(set.statistics().residentBytes == kBytesBefore - 4 + 4);
    kept.reset();
    set.update(9);
    RAWFRAME_EXPECT(set.statistics().retiring == 0 && set.statistics().residentBytes == kBytesBefore - 4);
    // A new request finds the new revision.
    const RequesterId kIjk2 = *set.request(refOf(4));
    RAWFRAME_EXPECT(fixture.settle(kIjk2, 10) == Readiness::Ready && fixture.textOf(kIjk2, 10) == "ijk2");
}
#endif
