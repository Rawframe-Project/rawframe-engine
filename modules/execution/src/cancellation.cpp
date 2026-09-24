#include "rawframe/execution/cancellation.h"

#include "rawframe/base/assert.h"
#include "rawframe/execution/bounds.h"
#include "rawframe/execution/errors.h"

namespace rawframe::execution {

namespace {

// reason_ holds zero while running and the reason plus one once cancelled, so a
// single compare-and-swap decides both whether and why.
constexpr std::uint8_t encode(CancelReason reason) noexcept {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(reason) + 1U);
}

} // namespace

CancellationScope::CancellationScope(const MonotonicSource& clock, FailurePolicy policy) noexcept
    : clock_(&clock), depth_(1), policy_(policy) {
}

result::Result<CancellationScope> CancellationScope::child(CancellationScope& parent, FailurePolicy policy) {
    if (parent.depth_ >= kMaximumCancellationScopeDepth) {
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::ScopeTooDeep),
                            "cancellation scopes nest deeper than kMaximumCancellationScopeDepth");
    }
    return result::Result<CancellationScope>{std::in_place, ChildTag{}, parent, policy};
}

CancellationScope::CancellationScope(ChildTag, CancellationScope& parent, FailurePolicy policy) noexcept
    : clock_(parent.clock_), parent_(&parent), depth_(parent.depth_ + 1), policy_(policy) {
    const std::scoped_lock kLock{parent.mutex_};
    nextSibling_ = parent.firstChild_;
    if (nextSibling_ != nullptr) {
        nextSibling_->previousSibling_ = this;
    }
    parent.firstChild_ = this;
    ++parent.childCount_;
    // A child of a cancelled scope starts cancelled, for the same reason.
    if (const std::uint8_t kReason = parent.reason_.load(std::memory_order_acquire); kReason != 0) {
        cancel(static_cast<CancelReason>(kReason - 1U));
    }
}

CancellationScope::~CancellationScope() {
    {
        const std::scoped_lock kLock{mutex_};
        RAWFRAME_CHECK(firstChild_ == nullptr, "a cancellation scope outlived by a child");
        RAWFRAME_CHECK(firstCallback_ == nullptr, "a cancellation scope outlived by a callback registration");
    }
    if (parent_ != nullptr) {
        const std::scoped_lock kLock{parent_->mutex_};
        if (previousSibling_ != nullptr) {
            previousSibling_->nextSibling_ = nextSibling_;
        } else {
            parent_->firstChild_ = nextSibling_;
        }
        if (nextSibling_ != nullptr) {
            nextSibling_->previousSibling_ = previousSibling_;
        }
        --parent_->childCount_;
    }
}

void CancellationScope::cancel(CancelReason reason) noexcept {
    std::uint8_t expected = 0;
    if (!reason_.compare_exchange_strong(expected, encode(reason), std::memory_order_acq_rel)) {
        return;
    }
    // Lock order is always parent before child, so descending while holding
    // this scope's lock cannot deadlock against a child's destructor, which
    // takes only the parent's lock.
    const std::scoped_lock kLock{mutex_};
    for (CancellationCallback* callback = firstCallback_; callback != nullptr; callback = callback->next_) {
        callback->function_(callback->context_, reason);
    }
    for (CancellationScope* scope = firstChild_; scope != nullptr; scope = scope->nextSibling_) {
        scope->cancel(reason);
    }
}

bool CancellationScope::deadlinePassed() const noexcept {
    std::optional<MonotonicInstant> now;
    for (const CancellationScope* scope = this; scope != nullptr; scope = scope->parent_) {
        const std::int64_t kDeadline = scope->deadline_.load(std::memory_order_acquire);
        if (kDeadline == kNoDeadline) {
            continue;
        }
        if (!now) {
            now = clock_->now();
        }
        if (now->nanoseconds >= kDeadline) {
            return true;
        }
    }
    return false;
}

bool CancellationScope::cancelled() noexcept {
    if (reason_.load(std::memory_order_acquire) != 0) {
        return true;
    }
    if (!deadlinePassed()) {
        return false;
    }
    // Cancel the highest scope whose deadline passed, so its whole subtree
    // sees `deadline_reached`, not only this scope.
    const MonotonicInstant kNow = clock_->now();
    CancellationScope* expired = nullptr;
    for (CancellationScope* scope = this; scope != nullptr; scope = scope->parent_) {
        if (scope->deadline_.load(std::memory_order_acquire) <= kNow.nanoseconds) {
            expired = scope;
        }
    }
    if (expired != nullptr) {
        expired->cancel(CancelReason::DeadlineReached);
    }
    return true;
}

std::optional<CancelReason> CancellationScope::reason() const noexcept {
    const std::uint8_t kReason = reason_.load(std::memory_order_acquire);
    if (kReason == 0) {
        return std::nullopt;
    }
    return static_cast<CancelReason>(kReason - 1U);
}

void CancellationScope::setDeadline(MonotonicInstant deadline) noexcept {
    std::int64_t current = deadline_.load(std::memory_order_acquire);
    while (deadline.nanoseconds < current &&
           !deadline_.compare_exchange_weak(current, deadline.nanoseconds, std::memory_order_acq_rel)) {
    }
}

void CancellationScope::reportFailure() noexcept {
    if (policy_ == FailurePolicy::FailFast) {
        cancel(CancelReason::ParentFailed);
    }
}

CancellationCallback::CancellationCallback(CancellationScope& scope, Function function, void* context) noexcept
    : scope_(&scope), function_(function), context_(context) {
    std::optional<CancelReason> alreadyCancelled;
    {
        const std::scoped_lock kLock{scope.mutex_};
        alreadyCancelled = scope.reason();
        if (!alreadyCancelled) {
            next_ = scope.firstCallback_;
            if (next_ != nullptr) {
                next_->previous_ = this;
            }
            scope.firstCallback_ = this;
            registered_ = true;
        }
    }
    if (alreadyCancelled) {
        function_(context_, *alreadyCancelled);
    }
}

CancellationCallback::~CancellationCallback() {
    if (!registered_) {
        return;
    }
    // Taking the scope's lock waits out a cancel that is running callbacks.
    const std::scoped_lock kLock{scope_->mutex_};
    if (previous_ != nullptr) {
        previous_->next_ = next_;
    } else {
        scope_->firstCallback_ = next_;
    }
    if (next_ != nullptr) {
        next_->previous_ = previous_;
    }
}

} // namespace rawframe::execution
