#include "rawframe/content/store.h"

#include "rawframe/base/threads.h"
#include "rawframe/content/errors.h"
#include "source.h"

#include <atomic>
#include <mutex>

namespace rawframe::content {

namespace {

std::unexpected<result::Error> refuse(ContentError error, result::ErrorClass errorClass, std::string_view why) {
    return std::unexpected<result::Error>{result::fail(errorClass, kContentDomain, code(error), why).error()};
}

} // namespace

struct ContentStore::State {
    State(execution::Executor& blockingIo,
          execution::OwnerId owner,
          execution::CancellationScope& parent,
          const execution::MonotonicSource& source,
          std::vector<ContentSource> held)
        : clock(&source), sources(std::move(held)),
          operations(execution::OperationScope::create(blockingIo, owner, parent)) {
    }

    const execution::MonotonicSource* clock;
    std::vector<ContentSource> sources;
    mutable base::Mutex catalogMutex;
    std::shared_ptr<const ContentCatalog> catalog;
    // Last, so it is destroyed first: its destructor cancels and joins every
    // read while the sources are still here. Made in place: it cannot move.
    result::Result<execution::OperationScope> operations;
};

ContentStore::ContentStore(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

ContentStore::~ContentStore() = default;

result::Result<std::unique_ptr<ContentStore>> ContentStore::create(execution::Executor& blockingIo,
                                                                   execution::OwnerId owner,
                                                                   execution::CancellationScope& parent,
                                                                   const execution::MonotonicSource& clock,
                                                                   std::vector<ContentSource> sources) {
    auto state = std::make_unique<State>(blockingIo, owner, parent, clock, std::move(sources));
    if (!state->operations.has_value()) {
        return std::unexpected<result::Error>{std::move(state->operations).error()};
    }
    return std::unique_ptr<ContentStore>{new ContentStore{std::move(state)}};
}

std::size_t ContentStore::sourceCount() const noexcept {
    return state_->sources.size();
}

void ContentStore::publish(std::shared_ptr<const ContentCatalog> catalog) noexcept {
    const std::lock_guard kLock{state_->catalogMutex};
    state_->catalog = std::move(catalog);
}

std::shared_ptr<const ContentCatalog> ContentStore::catalog() const noexcept {
    const std::lock_guard kLock{state_->catalogMutex};
    return state_->catalog;
}

result::Result<execution::AsyncHandle<VerifiedContent>> ContentStore::read(const ResourceRef& reference,
                                                                           const ReadRequest& request) {
    // One generation, captured once: the read resolves and fetches in it.
    std::shared_ptr<const ContentCatalog> generation = catalog();
    if (generation == nullptr) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::FailedPrecondition, "no catalog");
    }
    RAWFRAME_TRY_ASSIGN(const ContentDescriptor* descriptor, generation->resolve(reference));
    return start(std::move(generation), *descriptor, request);
}

result::Result<execution::AsyncHandle<VerifiedContent>> ContentStore::read(const PinnedResourceRef& reference,
                                                                           const ReadRequest& request) {
    std::shared_ptr<const ContentCatalog> generation = catalog();
    if (generation == nullptr) {
        return refuse(ContentError::SourceUnavailable, result::ErrorClass::FailedPrecondition, "no catalog");
    }
    RAWFRAME_TRY_ASSIGN(const ContentDescriptor* descriptor, generation->resolve(reference));
    return start(std::move(generation), *descriptor, request);
}

result::Result<execution::AsyncHandle<VerifiedContent>> ContentStore::start(
    std::shared_ptr<const ContentCatalog> catalog, const ContentDescriptor& descriptor, const ReadRequest& request) {
    if (descriptor.byteLength > request.maximumBytes) {
        return refuse(ContentError::ResourceTooLarge,
                      result::ErrorClass::ResourceExhausted,
                      "the resource is larger than the read takes");
    }
    if (descriptor.source >= state_->sources.size()) {
        return refuse(ContentError::SourceUnavailable,
                      result::ErrorClass::FailedPrecondition,
                      "the catalog names no such source");
    }
    std::shared_ptr<const ContentSource::Implementation> source = state_->sources[descriptor.source].implementation_;
    const execution::MonotonicSource* clock = state_->clock;
    auto started = state_->operations->start(
        execution::Priority::Normal,
        [catalog = std::move(catalog), source = std::move(source), descriptor, clock, deadline = request.deadline](
            execution::CancellationToken token) -> execution::TaskOutcome<VerifiedContent> {
            const auto kLate = [&] {
                return deadline.has_value() && clock->now() >= *deadline;
            };
            const auto kExpired = [] {
                return execution::TaskOutcome<VerifiedContent>::failed(
                    result::fail(result::ErrorClass::Unavailable,
                                 kContentDomain,
                                 code(ContentError::DeadlineExceeded),
                                 "the read's deadline passed")
                        .error());
            };
            if (token.cancelled()) {
                return execution::TaskOutcome<VerifiedContent>::cancelled(*token.reason());
            }
            if (kLate()) {
                return kExpired();
            }
            auto bytes = source->read(descriptor.locator, descriptor.byteLength);
            if (!bytes.has_value()) {
                return execution::TaskOutcome<VerifiedContent>::failed(std::move(bytes).error());
            }
            // Nothing is published before its digest is checked, and nothing
            // after a cancellation or the deadline.
            if (!sameDigest(ContentDigest::of(*bytes), descriptor.digest)) {
                return execution::TaskOutcome<VerifiedContent>::failed(
                    result::fail(result::ErrorClass::DataLoss,
                                 kContentDomain,
                                 code(ContentError::DigestMismatch),
                                 "the bytes are not the ones the catalog names")
                        .error());
            }
            if (token.cancelled()) {
                return execution::TaskOutcome<VerifiedContent>::cancelled(*token.reason());
            }
            if (kLate()) {
                return kExpired();
            }
            return execution::TaskOutcome<VerifiedContent>::success(
                VerifiedContent{std::make_shared<const std::vector<std::byte>>(std::move(*bytes)),
                                descriptor,
                                catalog->generation(),
                                catalog->fingerprint()});
        });
    if (!started.has_value()) {
        return std::unexpected<result::Error>{std::move(started).error().mappedTo(result::ErrorClass::ResourceExhausted,
                                                                                  kContentDomain,
                                                                                  code(ContentError::QuotaExhausted),
                                                                                  "the executor refused the read")};
    }
    return std::move(*started);
}

} // namespace rawframe::content
