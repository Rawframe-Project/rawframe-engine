#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::execution {

/// The domain of every Error this module returns.
inline constexpr result::ErrorDomain kExecutionDomain{base::parseBits128Hex("89b19632d4c14e348be03efa3775c11b").value};

/// Codes within kExecutionDomain.
enum class ExecutionError : std::uint32_t {
    QueueFull = 1,
    OwnerHasNoQuota = 2,
    OwnerQuotaExhausted = 3,
    ScopeTooDeep = 4,
    TooManyInFlightOperations = 5,
    AdmissionClosed = 6,
    BlockingOnCpuWorker = 7,
    BudgetNotNested = 8,
    QuotaTableFull = 9,
    OwnerAlreadyAdmitted = 10,
    ZeroQuota = 11,
    PriorityNotPermitted = 12,
    OwnerHasPendingWork = 13,
};

[[nodiscard]] constexpr result::ErrorCode code(ExecutionError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::execution
