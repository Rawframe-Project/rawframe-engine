#pragma once

#include "rawframe/base/threads.h"
#include "rawframe/execution/bounds.h"
#include "rawframe/execution/cancellation.h"
#include "rawframe/execution/errors.h"
#include "rawframe/execution/executor.h"
#include "rawframe/execution/outcome.h"
#include "rawframe/result/result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace rawframe::execution {

class OperationScope;

namespace detail {

template <typename T> struct IsTaskOutcome : std::false_type {};
template <typename T> struct IsTaskOutcome<TaskOutcome<T>> : std::true_type {};

/// What an operation's function returns, TaskOutcome<T> or Result<T>, reduced
/// to T.
template <typename T> struct OperationValueOf;
template <typename T> struct OperationValueOf<TaskOutcome<T>> {
    using Type = T;
};
template <typename T> struct OperationValueOf<result::Result<T>> {
    using Type = T;
};

/// The part of an operation its scope sees. Shared by the handle and the
/// queued task, and freed by whichever lets go last.
class OperationBase {
public:
    OperationBase(OperationScope& scope, std::uint64_t generation) noexcept : scope_(&scope), generation_(generation) {
    }
    virtual ~OperationBase() = default;

    virtual void run() noexcept = 0;

    void release() noexcept {
        if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    [[nodiscard]] bool ready() const noexcept {
        return ready_.load(std::memory_order_acquire);
    }
    [[nodiscard]] OperationScope& scope() const noexcept {
        return *scope_;
    }

protected:
    OperationScope* scope_;
    std::uint64_t generation_;
    std::atomic<bool> ready_{false};
    std::atomic<int> references_{2}; // the handle and the task
};

template <typename T> class OperationState : public OperationBase {
public:
    using OperationBase::OperationBase;

    /// The outcome, once ready and not yet taken.
    std::optional<TaskOutcome<T>> outcome;

protected:
    void complete(TaskOutcome<T>&& value) noexcept;
};

template <typename T, typename Function> class Operation final : public OperationState<T> {
public:
    Operation(OperationScope& scope, std::uint64_t generation, Function&& function)
        : OperationState<T>(scope, generation), function_(std::move(function)) {
    }

    void run() noexcept override;

private:
    Function function_;
};

} // namespace detail

/// Observes one async operation. Dropping the handle neither cancels nor
/// detaches the operation: its OperationScope still owns and joins it.
template <typename T> class AsyncHandle {
public:
    explicit AsyncHandle(detail::OperationState<T>* state) noexcept : state_(state) {
    }
    AsyncHandle(AsyncHandle&& other) noexcept : state_(std::exchange(other.state_, nullptr)) {
    }
    AsyncHandle& operator=(AsyncHandle&& other) noexcept {
        if (this != &other) {
            reset();
            state_ = std::exchange(other.state_, nullptr);
        }
        return *this;
    }
    AsyncHandle(const AsyncHandle&) = delete;
    AsyncHandle& operator=(const AsyncHandle&) = delete;
    ~AsyncHandle() {
        reset();
    }

    [[nodiscard]] bool ready() const noexcept {
        return state_ != nullptr && state_->ready();
    }

    /// The outcome if ready, once; afterwards nothing.
    [[nodiscard]] std::optional<TaskOutcome<T>> take() noexcept {
        if (!ready()) {
            return std::nullopt;
        }
        return std::exchange(state_->outcome, std::nullopt);
    }

    /// Waits for the outcome, helping the executor meanwhile, and takes it.
    /// Only for a handle whose outcome has not been taken.
    [[nodiscard]] TaskOutcome<T> wait() noexcept;

private:
    void reset() noexcept {
        if (state_ != nullptr) {
            std::exchange(state_, nullptr)->release();
        }
    }

    detail::OperationState<T>* state_ = nullptr;
};

/// Owns async operations: work that may outlive the tick that started it but
/// never its owner. Every operation belongs to this scope's cancellation
/// child, and the destructor cancels with `owner_stopping` and joins, so no
/// operation is ever detached (SPEC-0005 async operation).
class OperationScope {
    struct ConstructionTag {};

public:
    [[nodiscard]] static result::Result<OperationScope>
    create(Executor& executor, OwnerId owner, CancellationScope& parent, FailurePolicy policy = FailurePolicy::Isolate);

    OperationScope(
        ConstructionTag, Executor& executor, OwnerId owner, CancellationScope& parent, FailurePolicy policy) noexcept;
    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;
    ~OperationScope();

    /// Starts `function(token)`, which returns TaskOutcome<T> or Result<T>. On a
    /// full queue, quota, or in-flight limit this fails with
    /// `resource_exhausted` and nothing is queued: an async operation is never
    /// run inline, because nobody promised to wait for it (SPEC-0048).
    template <typename Function>
    [[nodiscard]] auto start(Priority priority, Function function) -> result::Result<
        AsyncHandle<typename detail::OperationValueOf<std::invoke_result_t<Function&, CancellationToken>>::Type>>;

    void cancel(CancelReason reason) noexcept {
        scope_->cancel(reason);
    }

    /// Ends the current generation: operations started before this deliver
    /// `cancelled(superseded)` instead of their result, because the state
    /// they were computed against has been replaced.
    void invalidate() noexcept;

    /// Waits for every operation started so far, helping the executor.
    void join() noexcept;

    [[nodiscard]] std::size_t inFlight() const noexcept;
    [[nodiscard]] CancellationToken token() noexcept {
        return CancellationToken{*scope_};
    }

private:
    template <typename T> friend class detail::OperationState;
    template <typename T, typename Function> friend class detail::Operation;
    template <typename T> friend class AsyncHandle;

    [[nodiscard]] result::Result<std::uint64_t> reserve();
    void unreserve() noexcept;
    [[nodiscard]] bool current(std::uint64_t generation) const noexcept;
    void completed(bool failed) noexcept;
    void waitUntil(const detail::OperationBase& operation) noexcept;
    [[nodiscard]] Executor& executor() noexcept {
        return *executor_;
    }
    [[nodiscard]] OwnerId owner() const noexcept {
        return owner_;
    }

    Executor* executor_;
    OwnerId owner_;
    result::Result<CancellationScope> scope_;

    mutable base::Mutex mutex_;
    base::Condition changed_;
    std::size_t inFlight_ = 0;
    std::uint64_t generation_ = 0;
};

namespace detail {

template <typename T> void OperationState<T>::complete(TaskOutcome<T>&& value) noexcept {
    OperationScope& owner = *this->scope_;
    const bool kFailed = value.isError();
    // A completion computed for a replaced generation is not delivered.
    if (owner.current(this->generation_)) {
        outcome.emplace(std::move(value));
    } else {
        outcome.emplace(TaskOutcome<T>::cancelled(CancelReason::Superseded));
    }
    this->ready_.store(true, std::memory_order_release);
    owner.completed(kFailed);
}

template <typename T, typename Function> void Operation<T, Function>::run() noexcept {
    CancellationScope& scope = *this->scope_->scope_;
    // Checked before the work begins: cancelled work never starts.
    if (scope.cancelled()) {
        this->complete(TaskOutcome<T>::cancelled(*scope.reason()));
    } else if constexpr (IsTaskOutcome<std::invoke_result_t<Function&, CancellationToken>>::value) {
        this->complete(function_(CancellationToken{scope}));
    } else {
        this->complete(TaskOutcome<T>::fromResult(function_(CancellationToken{scope})));
    }
    this->release();
}

} // namespace detail

template <typename T> TaskOutcome<T> AsyncHandle<T>::wait() noexcept {
    if (!state_->ready()) {
        state_->scope().waitUntil(*state_);
    }
    return *std::exchange(state_->outcome, std::nullopt);
}

template <typename Function>
auto OperationScope::start(Priority priority, Function function) -> result::Result<
    AsyncHandle<typename detail::OperationValueOf<std::invoke_result_t<Function&, CancellationToken>>::Type>> {
    using T = typename detail::OperationValueOf<std::invoke_result_t<Function&, CancellationToken>>::Type;
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kGeneration, reserve());
    auto* operation = new (std::nothrow) detail::Operation<T, Function>(*this, kGeneration, std::move(function));
    if (operation == nullptr) {
        unreserve();
        return result::fail(result::ErrorClass::ResourceExhausted,
                            kExecutionDomain,
                            code(ExecutionError::TooManyInFlightOperations),
                            "no memory for an async operation");
    }
    if (auto submitted = executor_->submit(owner_, priority, Task{[operation]() noexcept {
                                               operation->run();
                                           }});
        !submitted.has_value()) {
        delete operation;
        unreserve();
        return std::unexpected<result::Error>{std::move(submitted).error()};
    }
    return AsyncHandle<T>{operation};
}

} // namespace rawframe::execution
