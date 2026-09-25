#pragma once

#include "rawframe/base/threads.h"
#include "rawframe/execution/outcome.h"
#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace rawframe::execution {

/// What a scope does when work it owns fails. Isolation is the default: a
/// failure cancels nothing else (SPEC-0005).
enum class FailurePolicy : std::uint8_t {
    Isolate,
    FailFast
};

class CancellationCallback;

/// A node in the cancellation tree. Parentage follows lifetime ownership: a
/// child is created from its parent, must be destroyed before it, and is
/// cancelled when it is. Cancellation is cooperative and flows parent to child
/// only. Not movable, because children and callbacks point at it.
class CancellationScope {
    struct ChildTag {};

public:
    /// A root scope, owned by a Host or Runtime. `clock` enforces deadlines in
    /// this scope and every scope beneath it, and must outlive them.
    explicit CancellationScope(const MonotonicSource& clock, FailurePolicy policy = FailurePolicy::Isolate) noexcept;

    /// A child of `parent`, or `resource_exhausted` when it would exceed
    /// kMaximumCancellationScopeDepth. The scope lives inside the Result; use
    /// it in place.
    [[nodiscard]] static result::Result<CancellationScope> child(CancellationScope& parent,
                                                                 FailurePolicy policy = FailurePolicy::Isolate);

    CancellationScope(ChildTag, CancellationScope& parent, FailurePolicy policy) noexcept;
    CancellationScope(const CancellationScope&) = delete;
    CancellationScope& operator=(const CancellationScope&) = delete;
    ~CancellationScope();

    /// Requests cancellation of this scope and every descendant. Idempotent: the
    /// first reason wins and later requests do nothing.
    void cancel(CancelReason reason) noexcept;

    /// Whether cancellation was requested here or above, including by a
    /// deadline that has passed; noticing an expired deadline requests
    /// cancellation with `deadline_reached`.
    [[nodiscard]] bool cancelled() noexcept;

    /// The reason, once cancelled.
    [[nodiscard]] std::optional<CancelReason> reason() const noexcept;

    /// Cancels this scope at `deadline` on the root's monotonic clock. A later
    /// deadline never extends one already set.
    void setDeadline(MonotonicInstant deadline) noexcept;

    /// Work owned directly by this scope failed. Under FailFast that cancels
    /// the scope, and so all its other work, with `parent_failed`.
    void reportFailure() noexcept;

    [[nodiscard]] std::size_t depth() const noexcept {
        return depth_;
    }
    [[nodiscard]] const MonotonicSource& clock() const noexcept {
        return *clock_;
    }

private:
    friend class CancellationCallback;

    static constexpr std::int64_t kNoDeadline = INT64_MAX;

    [[nodiscard]] bool deadlinePassed() const noexcept;

    const MonotonicSource* clock_;
    CancellationScope* parent_ = nullptr;
    std::size_t depth_ = 0;
    FailurePolicy policy_;
    std::atomic<bool> cancelled_{false};
    std::atomic<std::uint8_t> reason_{0};
    std::atomic<std::int64_t> deadline_{kNoDeadline};

    // Guarded by mutex_. Children and callbacks are intrusive lists, so
    // registering either never allocates.
    mutable base::Mutex mutex_;
    CancellationScope* firstChild_ = nullptr;
    CancellationScope* nextSibling_ = nullptr;
    CancellationScope* previousSibling_ = nullptr;
    std::size_t childCount_ = 0;
    CancellationCallback* firstCallback_ = nullptr;
};

/// What work holds to check for cancellation: a cheap, copyable view of a
/// scope that must outlive it. A default token is never cancelled.
class CancellationToken {
public:
    CancellationToken() noexcept = default;
    explicit CancellationToken(CancellationScope& scope) noexcept : scope_(&scope) {
    }

    [[nodiscard]] bool cancelled() const noexcept {
        return scope_ != nullptr && scope_->cancelled();
    }
    [[nodiscard]] std::optional<CancelReason> reason() const noexcept {
        return scope_ != nullptr ? scope_->reason() : std::nullopt;
    }

private:
    CancellationScope* scope_ = nullptr;
};

/// Runs a function when its scope is cancelled, or at once if it already is.
/// Destroying the registration revokes it, waiting for a call in progress. The
/// function must be bounded and non-blocking and must not use the scope it is
/// registered on.
class CancellationCallback {
public:
    using Function = void (*)(void* context, CancelReason reason) noexcept;

    CancellationCallback(CancellationScope& scope, Function function, void* context) noexcept;
    CancellationCallback(const CancellationCallback&) = delete;
    CancellationCallback& operator=(const CancellationCallback&) = delete;
    ~CancellationCallback();

private:
    friend class CancellationScope;

    CancellationScope* scope_;
    Function function_;
    void* context_;
    CancellationCallback* next_ = nullptr;
    CancellationCallback* previous_ = nullptr;
    bool registered_ = false;
};

} // namespace rawframe::execution
