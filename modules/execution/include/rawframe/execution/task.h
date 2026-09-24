#pragma once

#include "rawframe/execution/bounds.h"

#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace rawframe::execution {

/// A move-only `void()` callable stored inline, so queueing a task never
/// allocates. A callable larger than kTaskInlineBytes does not compile: it
/// should own its state behind one pointer instead.
class Task {
public:
    Task() noexcept = default;

    template <typename Function>
        requires(!std::same_as<std::remove_cvref_t<Function>, Task> && std::invocable<std::decay_t<Function>&>)
    Task(Function&& function) noexcept // implicit on purpose: a lambda is a task
    {
        using Stored = std::decay_t<Function>;
        static_assert(sizeof(Stored) <= kTaskInlineBytes, "a task's captures exceed kTaskInlineBytes");
        static_assert(alignof(Stored) <= alignof(std::max_align_t), "a task's captures are over-aligned");
        static_assert(std::is_nothrow_move_constructible_v<Stored>, "a task must be nothrow movable");
        ::new (static_cast<void*>(storage_)) Stored(std::forward<Function>(function));
        operations_ = &kOperations<Stored>;
    }

    Task(Task&& other) noexcept {
        take(other);
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            reset();
            take(other);
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        reset();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return operations_ != nullptr;
    }

    /// Runs the callable. The task stays valid and may be run again.
    void operator()() noexcept {
        operations_->invoke(storage_);
    }

private:
    struct Operations {
        void (*invoke)(void* storage) noexcept;
        void (*relocate)(void* from, void* to) noexcept;
        void (*destroy)(void* storage) noexcept;
    };

    template <typename Stored>
    static constexpr Operations kOperations{
        [](void* storage) noexcept {
            (*static_cast<Stored*>(storage))();
        },
        [](void* from, void* to) noexcept {
            ::new (to) Stored(std::move(*static_cast<Stored*>(from)));
            static_cast<Stored*>(from)->~Stored();
        },
        [](void* storage) noexcept {
            static_cast<Stored*>(storage)->~Stored();
        },
    };

    void take(Task& other) noexcept {
        if (other.operations_ != nullptr) {
            other.operations_->relocate(other.storage_, storage_);
            operations_ = std::exchange(other.operations_, nullptr);
        }
    }

    void reset() noexcept {
        if (operations_ != nullptr) {
            operations_->destroy(storage_);
            operations_ = nullptr;
        }
    }

    alignas(std::max_align_t) std::byte storage_[kTaskInlineBytes];
    const Operations* operations_ = nullptr;
};

} // namespace rawframe::execution
