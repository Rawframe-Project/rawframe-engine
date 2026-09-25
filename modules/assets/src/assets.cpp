#include "rawframe/assets/assets.h"

#include "rawframe/assets/errors.h"
#include "rawframe/content/errors.h"
#include "rawframe/execution/operation.h"

#include <algorithm>

namespace rawframe::assets {

namespace {

std::unexpected<result::Error> refuse(AssetError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kAssetsDomain, code(error), why).error()};
}

result::Error failureOf(AssetError error, result::ErrorClass errorClass, std::string_view why) {
    return refuse(error, errorClass, why).error();
}

/// One asset's slot: a resource at one revision, while it is used.
struct Entry {
    bool used = false;
    std::uint32_t generation = 0;
    content::ResourceId id;
    content::ContentDigest revision;
    Residency residency = Residency::Absent;
    std::uint32_t interest = 0;
    std::optional<execution::AsyncHandle<content::VerifiedContent>> reading;
    std::optional<execution::AsyncHandle<DecodedForm>> decoding;
    DecodedForm form;
    /// Sticky for this resource and revision (SPEC-0027).
    std::optional<result::Error> failure;
    /// The last tick the form was read through a handle.
    mutable std::uint64_t lastUse = 0;
    /// Why handles to this slot's earlier uses are stale.
    AssetError stale = AssetError::Evicted;
};

struct Requester {
    bool used = false;
    std::uint32_t generation = 0;
    std::uint32_t entry = 0;
    /// The entry's use it counts in; its interest is only ever counted once.
    std::uint32_t entryGeneration = 0;
    bool interested = false;
    std::optional<execution::MonotonicInstant> deadline;
    /// A failure of its own (its deadline), apart from the load's.
    std::optional<result::Error> failure;
};

} // namespace

struct AssetSet::State {
    State(content::ContentStore& contentStore,
          execution::Executor& cpu,
          execution::OwnerId owner,
          execution::CancellationScope& parent,
          const execution::MonotonicSource& source,
          const AssetSettings& chosen)
        : store(&contentStore), clock(&source), settings(chosen), entries(chosen.maximumAssets),
          requesters(chosen.maximumRequesters), operations(execution::OperationScope::create(cpu, owner, parent)) {
    }

    content::ContentStore* store;
    const execution::MonotonicSource* clock;
    AssetSettings settings;
    std::vector<Entry> entries;
    std::vector<Requester> requesters;
    AssetStatistics statistics;
    bool closed = false;
    result::Result<execution::OperationScope> operations;

    [[nodiscard]] const Requester* requesterOf(RequesterId id) const noexcept {
        if (id.slot >= requesters.size()) {
            return nullptr;
        }
        const Requester& requester = requesters[id.slot];
        return requester.used && requester.generation == id.generation ? &requester : nullptr;
    }

    [[nodiscard]] Requester* requesterOf(RequesterId id) noexcept {
        return const_cast<Requester*>(std::as_const(*this).requesterOf(id));
    }

    /// Frees a slot for another use; handles to this one fail with `why`.
    void retire(Entry& entry, AssetError why) noexcept {
        if (entry.residency == Residency::Resident || entry.residency == Residency::Evictable) {
            statistics.residentBytes -= entry.form.bytes;
        }
        entry.residency = Residency::Absent;
        entry.form = {};
        entry.reading.reset();
        entry.decoding.reset();
        entry.failure.reset();
        entry.used = false;
        entry.stale = why;
        ++entry.generation;
    }

    /// One requester's interest gone.
    void drop(Requester& requester) noexcept {
        Entry& entry = entries[requester.entry];
        const bool kCounted = requester.interested && entry.generation == requester.entryGeneration;
        requester.used = false;
        requester.interested = false;
        requester.failure.reset();
        if (kCounted && entry.interest > 0 && --entry.interest == 0) {
            if (entry.residency == Residency::Resident) {
                entry.residency = Residency::Evictable;
            }
            // A failed load no one wants is forgotten with its failure; one
            // still loading finishes and is discarded (update).
            if (entry.residency == Residency::Absent && entry.failure.has_value()) {
                retire(entry, AssetError::Evicted);
            }
        }
    }

    /// The evictable form to go first: lowest retention class (there is one
    /// class so far), then least recently used, then lowest slot.
    [[nodiscard]] Entry* nextEviction() noexcept {
        Entry* chosen = nullptr;
        for (Entry& entry : entries) {
            if (entry.used && entry.residency == Residency::Evictable &&
                (chosen == nullptr || entry.lastUse < chosen->lastUse)) {
                chosen = &entry;
            }
        }
        return chosen;
    }

    result::Result<RequesterId> request(const content::ContentDescriptor& descriptor, const RequestOptions& options) {
        if (closed) {
            return refuse(AssetError::ScopeClosed, result::ErrorClass::FailedPrecondition, "the set is closed");
        }
        const auto kFreeRequester = std::ranges::find(requesters, false, &Requester::used);
        if (kFreeRequester == requesters.end()) {
            return refuse(AssetError::LimitExceeded, result::ErrorClass::ResourceExhausted, "every requester in use");
        }
        // The same resource at the same revision shares its load.
        auto found = std::ranges::find_if(entries, [&descriptor](const Entry& entry) {
            return entry.used && entry.id == descriptor.id && content::sameDigest(entry.revision, descriptor.digest);
        });
        if (found != entries.end()) {
            ++statistics.coalesced;
        } else {
            found = std::ranges::find(entries, false, &Entry::used);
            if (found == entries.end()) {
                return refuse(AssetError::LimitExceeded, result::ErrorClass::ResourceExhausted, "every asset in use");
            }
            RAWFRAME_TRY_ASSIGN(
                execution::AsyncHandle<content::VerifiedContent> reading,
                store->read(content::PinnedResourceRef{.resource = {.id = descriptor.id, .type = descriptor.type},
                                                       .digest = descriptor.digest}));
            found->used = true;
            found->id = descriptor.id;
            found->revision = descriptor.digest;
            found->residency = Residency::Loading;
            found->reading.emplace(std::move(reading));
            ++statistics.loads;
        }
        Entry& entry = *found;
        ++entry.interest;
        if (entry.residency == Residency::Evictable) {
            entry.residency = Residency::Resident;
        }
        Requester& requester = *kFreeRequester;
        requester.used = true;
        ++requester.generation;
        requester.entry = static_cast<std::uint32_t>(found - entries.begin());
        requester.entryGeneration = entry.generation;
        requester.interested = true;
        requester.deadline = options.deadline;
        requester.failure.reset();
        return RequesterId{.slot = static_cast<std::uint32_t>(kFreeRequester - requesters.begin()),
                           .generation = requester.generation};
    }

    /// A decoded form, charged to the budget after evicting what must go;
    /// past the ceiling even then, the load fails `LimitExceeded`.
    void publish(Entry& entry, DecodedForm form) {
        while (statistics.residentBytes + form.bytes > settings.budgetBytes) {
            Entry* evicted = nextEviction();
            if (evicted == nullptr) {
                entry.residency = Residency::Absent;
                entry.failure = failureOf(AssetError::LimitExceeded,
                                          result::ErrorClass::ResourceExhausted,
                                          "the budget has no room for this form");
                return;
            }
            retire(*evicted, AssetError::Evicted);
            ++statistics.evictions;
        }
        entry.form = std::move(form);
        statistics.residentBytes += entry.form.bytes;
        entry.residency = Residency::Resident;
    }
};

AssetSet::AssetSet(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

AssetSet::~AssetSet() {
    static_cast<void>(close());
}

result::Result<std::unique_ptr<AssetSet>> AssetSet::create(content::ContentStore& store,
                                                           execution::Executor& cpu,
                                                           execution::OwnerId owner,
                                                           execution::CancellationScope& parent,
                                                           const execution::MonotonicSource& clock,
                                                           const AssetSettings& settings) {
    if (settings.decode == nullptr || !settings.type.valid() || settings.maximumAssets == 0 ||
        settings.maximumRequesters == 0) {
        return refuse(AssetError::FormUnavailable,
                      result::ErrorClass::InvalidArgument,
                      "an asset set needs a type, a decoder, and room");
    }
    auto state = std::make_unique<State>(store, cpu, owner, parent, clock, settings);
    if (!state->operations.has_value()) {
        return std::unexpected<result::Error>{std::move(state->operations).error()};
    }
    return std::unique_ptr<AssetSet>{new AssetSet{std::move(state)}};
}

result::Result<RequesterId> AssetSet::request(const content::ResourceRef& reference, const RequestOptions& options) {
    const std::shared_ptr<const content::ContentCatalog> kCatalog = state_->store->catalog();
    if (kCatalog == nullptr || reference.type != state_->settings.type) {
        return refuse(AssetError::WrongScope,
                      result::ErrorClass::InvalidArgument,
                      "this set holds another type, or has no catalog");
    }
    RAWFRAME_TRY_ASSIGN(const content::ContentDescriptor* descriptor, kCatalog->resolve(reference));
    return state_->request(*descriptor, options);
}

result::Result<RequesterId> AssetSet::request(const content::PinnedResourceRef& reference,
                                              const RequestOptions& options) {
    const std::shared_ptr<const content::ContentCatalog> kCatalog = state_->store->catalog();
    if (kCatalog == nullptr || reference.resource.type != state_->settings.type) {
        return refuse(AssetError::WrongScope,
                      result::ErrorClass::InvalidArgument,
                      "this set holds another type, or has no catalog");
    }
    RAWFRAME_TRY_ASSIGN(const content::ContentDescriptor* descriptor, kCatalog->resolve(reference));
    return state_->request(*descriptor, options);
}

Readiness AssetSet::readiness(RequesterId id) const noexcept {
    const Requester* requester = state_->requesterOf(id);
    if (requester == nullptr) {
        return Readiness::NotRequested;
    }
    if (requester->failure.has_value()) {
        return Readiness::Failed;
    }
    const Entry& entry = state_->entries[requester->entry];
    if (entry.generation != requester->entryGeneration) {
        return Readiness::NotRequested;
    }
    if (entry.failure.has_value()) {
        return Readiness::Failed;
    }
    return entry.residency == Residency::Resident ? Readiness::Ready : Readiness::Pending;
}

const result::Error* AssetSet::failure(RequesterId id) const noexcept {
    const Requester* requester = state_->requesterOf(id);
    if (requester == nullptr) {
        return nullptr;
    }
    if (requester->failure.has_value()) {
        return &*requester->failure;
    }
    const Entry& entry = state_->entries[requester->entry];
    return entry.generation == requester->entryGeneration && entry.failure.has_value() ? &*entry.failure : nullptr;
}

std::optional<AssetHandle> AssetSet::handle(RequesterId id) const noexcept {
    if (readiness(id) != Readiness::Ready) {
        return std::nullopt;
    }
    const std::uint32_t kEntry = state_->requesterOf(id)->entry;
    return AssetHandle{.slot = kEntry, .generation = state_->entries[kEntry].generation};
}

void AssetSet::release(RequesterId id) noexcept {
    if (Requester* requester = state_->requesterOf(id)) {
        state_->drop(*requester);
    }
}

void AssetSet::cancel(RequesterId id) noexcept {
    // No notification is pending for anyone here, so cancelling and
    // releasing end the same way; they stay two verbs, as SPEC-0027 has it.
    release(id);
}

result::Result<const void*> AssetSet::get(AssetHandle handle, std::uint64_t tick) const {
    if (state_->closed) {
        return refuse(AssetError::ScopeClosed, result::ErrorClass::FailedPrecondition, "the set is closed");
    }
    if (handle.slot >= state_->entries.size()) {
        return refuse(AssetError::WrongScope, result::ErrorClass::InvalidArgument, "not a handle of this set");
    }
    const Entry& entry = state_->entries[handle.slot];
    if (entry.generation != handle.generation) {
        return refuse(entry.stale, result::ErrorClass::FailedPrecondition, "the handle is stale");
    }
    if (entry.residency != Residency::Resident && entry.residency != Residency::Evictable) {
        return refuse(AssetError::FormUnavailable, result::ErrorClass::FailedPrecondition, "not resident");
    }
    entry.lastUse = std::max(entry.lastUse, tick);
    return entry.form.value.get();
}

void AssetSet::update(std::uint64_t tick) {
    State& state = *state_;
    if (state.closed) {
        return;
    }
    for (Entry& entry : state.entries) {
        if (!entry.used || entry.residency != Residency::Loading) {
            continue;
        }
        if (entry.reading.has_value() && entry.reading->ready()) {
            auto outcome = *entry.reading->take();
            entry.reading.reset();
            if (entry.interest == 0 || outcome.isCancelled()) {
                state.retire(entry, AssetError::Evicted);
                continue;
            }
            if (outcome.isError()) {
                entry.residency = Residency::Absent;
                entry.failure = std::move(outcome).takeError();
                continue;
            }
            auto started = state.operations->start(
                execution::Priority::Normal,
                [content = std::move(*outcome), decode = state.settings.decode](
                    execution::CancellationToken token) -> execution::TaskOutcome<DecodedForm> {
                    if (token.cancelled()) {
                        return execution::TaskOutcome<DecodedForm>::cancelled(*token.reason());
                    }
                    auto decoded = decode(content);
                    if (!decoded.has_value()) {
                        return execution::TaskOutcome<DecodedForm>::failed(
                            std::move(decoded).error().mappedTo(result::ErrorClass::InvalidArgument,
                                                                kAssetsDomain,
                                                                code(AssetError::DecodeFailed),
                                                                "the family could not decode it"));
                    }
                    return execution::TaskOutcome<DecodedForm>::success(std::move(*decoded));
                });
            if (!started.has_value()) {
                entry.residency = Residency::Absent;
                entry.failure = std::move(started).error();
                continue;
            }
            entry.decoding.emplace(std::move(*started));
        }
        if (entry.decoding.has_value() && entry.decoding->ready()) {
            auto outcome = *entry.decoding->take();
            entry.decoding.reset();
            if (entry.interest == 0 || outcome.isCancelled()) {
                state.retire(entry, AssetError::Evicted);
                continue;
            }
            if (outcome.isError()) {
                entry.residency = Residency::Absent;
                entry.failure = std::move(outcome).takeError();
                continue;
            }
            entry.lastUse = tick;
            state.publish(entry, std::move(*outcome));
        }
    }
    // Requests past their deadlines fail on their own; the load goes on for
    // anyone else.
    const execution::MonotonicInstant kNow = state.clock->now();
    for (Requester& requester : state.requesters) {
        if (requester.used && !requester.failure.has_value() && requester.deadline.has_value() &&
            kNow >= *requester.deadline && state.entries[requester.entry].residency == Residency::Loading) {
            requester.failure = failureOf(
                AssetError::DeadlineExceeded, result::ErrorClass::Unavailable, "the request's deadline passed");
            Entry& entry = state.entries[requester.entry];
            if (requester.interested && entry.interest > 0) {
                --entry.interest;
            }
            requester.interested = false;
        }
    }
}

Residency AssetSet::residency(AssetHandle handle) const noexcept {
    if (handle.slot >= state_->entries.size() || state_->entries[handle.slot].generation != handle.generation) {
        return Residency::Absent;
    }
    return state_->entries[handle.slot].residency;
}

AssetStatistics AssetSet::statistics() const noexcept {
    AssetStatistics statistics = state_->statistics;
    for (const Entry& entry : state_->entries) {
        statistics.loading += entry.used && entry.residency == Residency::Loading ? 1 : 0;
        statistics.resident += entry.used && entry.residency == Residency::Resident ? 1 : 0;
        statistics.evictable += entry.used && entry.residency == Residency::Evictable ? 1 : 0;
    }
    return statistics;
}

std::vector<Survivor> AssetSet::close() {
    State& state = *state_;
    if (state.closed) {
        return {};
    }
    std::vector<Survivor> survivors;
    for (const Entry& entry : state.entries) {
        if (entry.used && entry.interest > 0) {
            survivors.push_back(Survivor{.id = entry.id, .revision = entry.revision, .interest = entry.interest});
        }
    }
    state.operations->cancel(execution::CancelReason::OwnerStopping);
    state.operations->join();
    for (Entry& entry : state.entries) {
        if (entry.used) {
            state.retire(entry, AssetError::ScopeClosed);
        }
    }
    state.closed = true;
    return survivors;
}

} // namespace rawframe::assets
