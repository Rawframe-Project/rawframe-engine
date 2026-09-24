#include "rawframe/execution/budget.h"

#include "rawframe/execution/errors.h"

namespace rawframe::execution {

result::Status checkBudgetNesting(MonotonicDuration parent, std::span<const MonotonicDuration> children) {
    MonotonicDuration sum{};
    for (const MonotonicDuration kChild : children) {
        if (kChild.nanoseconds <= 0 || kChild >= parent) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kExecutionDomain,
                                code(ExecutionError::BudgetNotNested),
                                "a nested budget is not positive and strictly less than its parent's");
        }
        sum = sum + kChild;
        if (sum > parent) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kExecutionDomain,
                                code(ExecutionError::BudgetNotNested),
                                "sibling budgets together exceed their parent's");
        }
    }
    return {};
}

} // namespace rawframe::execution
