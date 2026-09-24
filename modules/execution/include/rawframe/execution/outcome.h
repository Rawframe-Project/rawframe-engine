#pragma once

#include "rawframe/result/result.h"

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace rawframe::execution {

/// Why work was cancelled (SPEC-0005's minimum set). Machine-readable and
/// stable within the internal contract; never an ErrorClass.
enum class CancelReason : std::uint8_t {
    Requested,
    OwnerStopping,
    DeadlineReached,
    Superseded,
    PeerDisconnected,
    ParentFailed,
};

[[nodiscard]] constexpr std::string_view describe(CancelReason reason) noexcept {
    switch (reason) {
    case CancelReason::Requested:
        return "requested";
    case CancelReason::OwnerStopping:
        return "owner_stopping";
    case CancelReason::DeadlineReached:
        return "deadline_reached";
    case CancelReason::Superseded:
        return "superseded";
    case CancelReason::PeerDisconnected:
        return "peer_disconnected";
    case CancelReason::ParentFailed:
        return "parent_failed";
    }
    return "requested";
}

struct Cancelled {
    CancelReason reason = CancelReason::Requested;
};

/// How a cancellable task completed: exactly one of a value, a cancellation, or
/// an Error. Cancellation is not an Error and carries no error class; neither
/// channel emits a diagnostic on its own (SPEC-0005 TaskOutcome).
template <typename T> class TaskOutcome {
    using Stored = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

public:
    using ValueType = T;

    [[nodiscard]] static TaskOutcome success()
        requires std::is_void_v<T>
    {
        return TaskOutcome{std::in_place_index<0>};
    }

    template <typename U = Stored>
    [[nodiscard]] static TaskOutcome success(U&& value)
        requires(!std::is_void_v<T>)
    {
        return TaskOutcome{std::in_place_index<0>, std::forward<U>(value)};
    }

    [[nodiscard]] static TaskOutcome cancelled(CancelReason reason) noexcept {
        return TaskOutcome{std::in_place_index<1>, Cancelled{reason}};
    }

    [[nodiscard]] static TaskOutcome failed(result::Error error) noexcept {
        return TaskOutcome{std::in_place_index<2>, std::move(error)};
    }

    /// Carries a Result over unchanged: its value or its Error.
    [[nodiscard]] static TaskOutcome fromResult(result::Result<T>&& outcome) {
        if (!outcome.has_value()) {
            return failed(std::move(outcome).error());
        }
        if constexpr (std::is_void_v<T>) {
            return success();
        } else {
            return success(std::move(*outcome));
        }
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return storage_.index() == 0;
    }
    [[nodiscard]] bool isCancelled() const noexcept {
        return storage_.index() == 1;
    }
    [[nodiscard]] bool isError() const noexcept {
        return storage_.index() == 2;
    }

    /// The value. Only when hasValue().
    [[nodiscard]] Stored& operator*() noexcept
        requires(!std::is_void_v<T>)
    {
        return *std::get_if<0>(&storage_);
    }

    /// The reason. Only when isCancelled().
    [[nodiscard]] CancelReason cancelReason() const noexcept {
        return std::get_if<1>(&storage_)->reason;
    }

    /// The Error. Only when isError().
    [[nodiscard]] const result::Error& error() const noexcept {
        return *std::get_if<2>(&storage_);
    }
    [[nodiscard]] result::Error takeError() && noexcept {
        return std::move(*std::get_if<2>(&storage_));
    }

private:
    template <std::size_t Index, typename... Arguments>
    explicit TaskOutcome(std::in_place_index_t<Index> index, Arguments&&... arguments)
        : storage_(index, std::forward<Arguments>(arguments)...) {
    }

    std::variant<Stored, Cancelled, result::Error> storage_;
};

/// The Error a boundary chooses to report cancellation as. Mapping is always
/// explicit and names its destination (SPEC-0005).
struct CancellationMapping {
    result::ErrorClass errorClass;
    result::ErrorDomain domain;
    result::ErrorCode code;
    std::string_view description;
};

/// Converts at an API boundary whose contract defines cancellation as an
/// external failure.
template <typename T>
[[nodiscard]] result::Result<T> toResult(TaskOutcome<T>&& outcome, const CancellationMapping& mapping) {
    if (outcome.isCancelled()) {
        return result::fail(mapping.errorClass, mapping.domain, mapping.code, mapping.description);
    }
    if (outcome.isError()) {
        return std::unexpected<result::Error>{std::move(outcome).takeError()};
    }
    if constexpr (std::is_void_v<T>) {
        return {};
    } else {
        return std::move(*outcome);
    }
}

} // namespace rawframe::execution
