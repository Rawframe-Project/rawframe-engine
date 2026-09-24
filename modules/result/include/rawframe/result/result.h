#pragma once

#include "rawframe/result/error.h"

#include <expected>
#include <source_location>
#include <string_view>
#include <utility>

namespace rawframe::result {

/// A value or the Error that prevented it. An alias, not a class, so it cannot
/// drift from std::expected. Every function returning one is [[nodiscard]], and
/// maintained code never calls `.value()` on it (the check rejects that).
template <typename T> using Result = std::expected<T, Error>;

/// Success with no value, or an Error.
using Status = std::expected<void, Error>;

/// The one way to build a failure: `return fail(...)`. Converts to any
/// Result<T> and to Status. Success is `return value;` or `return {};`.
[[nodiscard]] std::unexpected<Error> fail(ErrorClass errorClass,
                                          ErrorDomain domain,
                                          ErrorCode code,
                                          std::string_view description,
                                          std::source_location origin = std::source_location::current());

} // namespace rawframe::result

#define RAWFRAME_RESULT_CONCAT_INNER(a, b) a##b
#define RAWFRAME_RESULT_CONCAT(a, b) RAWFRAME_RESULT_CONCAT_INNER(a, b)
#define RAWFRAME_RESULT_UNIQUE(prefix) RAWFRAME_RESULT_CONCAT(prefix, __LINE__)

/// Evaluates a Status-returning expression once and returns its Error from the
/// enclosing function if it failed. Adds nothing to the Error and emits nothing.
#define RAWFRAME_TRY(expression)                                                                                       \
    do {                                                                                                               \
        auto&& rawframeTryResult = (expression);                                                                       \
        if (!rawframeTryResult.has_value()) {                                                                          \
            return ::std::unexpected<::rawframe::result::Error>{::std::move(rawframeTryResult).error()};               \
        }                                                                                                              \
    } while (false)

/// Evaluates a Result-returning expression once. On failure returns its Error
/// from the enclosing function; on success initializes `declaration` from the
/// moved-out value, which stays in scope for the statements that follow.
#define RAWFRAME_TRY_ASSIGN(declaration, expression)                                                                   \
    auto&& RAWFRAME_RESULT_UNIQUE(rawframeTryAssign) = (expression);                                                   \
    if (!RAWFRAME_RESULT_UNIQUE(rawframeTryAssign).has_value()) {                                                      \
        return ::std::unexpected<::rawframe::result::Error>{                                                           \
            ::std::move(RAWFRAME_RESULT_UNIQUE(rawframeTryAssign)).error()};                                           \
    }                                                                                                                  \
    declaration = ::std::move(*RAWFRAME_RESULT_UNIQUE(rawframeTryAssign))
