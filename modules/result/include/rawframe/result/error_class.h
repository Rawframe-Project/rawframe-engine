#pragma once

#include <cstdint>
#include <string_view>

namespace rawframe::result {

/// Why an operation failed, in the closed vocabulary SPEC-0004 fixes. There is
/// no success value and no cancellation value: success is the Result's value and
/// cancellation is a task outcome, not an error. Ordinals are not identity and
/// are never serialized.
enum class ErrorClass : std::uint8_t {
    InvalidArgument,
    FailedPrecondition,
    OutOfRange,
    NotFound,
    AlreadyExists,
    Conflict,
    Unauthenticated,
    PermissionDenied,
    Unsupported,
    ResourceExhausted,
    Unavailable,
    DataLoss,
    Internal,
};

/// The SPEC-0004 snake_case name of a class. Developer-facing text, never
/// identity: nothing durable may be keyed by it.
[[nodiscard]] constexpr std::string_view describe(ErrorClass errorClass) noexcept {
    switch (errorClass) {
    case ErrorClass::InvalidArgument:
        return "invalid_argument";
    case ErrorClass::FailedPrecondition:
        return "failed_precondition";
    case ErrorClass::OutOfRange:
        return "out_of_range";
    case ErrorClass::NotFound:
        return "not_found";
    case ErrorClass::AlreadyExists:
        return "already_exists";
    case ErrorClass::Conflict:
        return "conflict";
    case ErrorClass::Unauthenticated:
        return "unauthenticated";
    case ErrorClass::PermissionDenied:
        return "permission_denied";
    case ErrorClass::Unsupported:
        return "unsupported";
    case ErrorClass::ResourceExhausted:
        return "resource_exhausted";
    case ErrorClass::Unavailable:
        return "unavailable";
    case ErrorClass::DataLoss:
        return "data_loss";
    case ErrorClass::Internal:
        return "internal";
    }
    return "unknown";
}

} // namespace rawframe::result
