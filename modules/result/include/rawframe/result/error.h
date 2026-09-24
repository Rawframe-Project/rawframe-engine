#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error_class.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <source_location>
#include <span>
#include <string_view>

namespace rawframe::result {

/// The owner of a failure: an explicit stable 128-bit value the owning module
/// declares once as a constant. Never derived from a name, path, type, or hash.
struct ErrorDomain {
    base::Bits128 value;
    friend constexpr bool operator==(const ErrorDomain&, const ErrorDomain&) noexcept = default;
};

/// A failure within its domain. Meaningful only with the domain; two domains may
/// use the same number for unrelated failures. Zero is a valid code.
struct ErrorCode {
    std::uint32_t value = 0;
    friend constexpr bool operator==(const ErrorCode&, const ErrorCode&) noexcept = default;
};

// The bounds SPEC-0004 requires and SPEC-0050 numbers. Exceeding a length
// truncates at a UTF-8 code point boundary and says so; exceeding a count drops
// the excess and counts it. Neither fails.
inline constexpr std::size_t kMaximumDescriptionBytes = 256;
inline constexpr std::size_t kMaximumFrames = 8;
inline constexpr std::size_t kMaximumContextFields = 8;
inline constexpr std::size_t kMaximumContextKeyBytes = 32;
inline constexpr std::size_t kMaximumContextValueBytes = 128;
inline constexpr std::size_t kMaximumCauseDepth = 4;
/// SPEC-0050 bounds everything an Error stores except a frame's description;
/// this fills that gap (work/decisions.md D10).
inline constexpr std::size_t kMaximumFrameDescriptionBytes = 128;

/// One typed context field. A key is owner-scoped schema, not prose. Never put a
/// secret, credential, private content, a full local path, or personal data in
/// either part.
struct ContextField {
    std::string_view key;
    std::string_view value;
};

/// One propagation step a caller chose to record.
struct Frame {
    std::string_view description;
    std::source_location location;
};

struct ErrorDetail;

/// A failure value. Move-only, 16 bytes: the class inline so a policy check
/// reads it without chasing a pointer, everything else behind one owning
/// pointer so that Result<T> stays small. Constructing an Error allocates once,
/// and each of withFrame, withContext, mappedTo, and clone allocates once more:
/// failure is the cold path. A hot path whose failure is ordinary returns a
/// total value type instead of a Result (STD-0004). Allocation failure here is
/// process-fatal.
///
/// Every accessor returns storage the Error owns. Accessors on a moved-from
/// Error are a precondition violation.
class Error {
public:
    Error(Error&&) noexcept;
    Error& operator=(Error&&) noexcept;
    Error(const Error&) = delete;
    Error& operator=(const Error&) = delete;
    ~Error();

    /// A deep copy, for the caller that genuinely needs a second one.
    [[nodiscard]] Error clone() const;

    [[nodiscard]] ErrorClass errorClass() const noexcept {
        return class_;
    }
    [[nodiscard]] ErrorDomain domain() const noexcept;
    [[nodiscard]] ErrorCode code() const noexcept;
    [[nodiscard]] std::string_view description() const noexcept;
    [[nodiscard]] bool descriptionTruncated() const noexcept;
    [[nodiscard]] std::source_location origin() const noexcept;
    [[nodiscard]] std::span<const Frame> frames() const noexcept;
    [[nodiscard]] std::size_t framesDropped() const noexcept;
    [[nodiscard]] std::span<const ContextField> context() const noexcept;
    [[nodiscard]] std::size_t contextFieldsDropped() const noexcept;
    /// True when a context key or value was cut to its bound.
    [[nodiscard]] bool contextTruncated() const noexcept;
    /// The preserved cause, or null at the end of the chain.
    [[nodiscard]] const Error* cause() const noexcept;
    /// True when a cause was dropped because the chain reached its depth bound.
    [[nodiscard]] bool causeDropped() const noexcept;

    [[nodiscard]] Error withFrame(std::string_view description,
                                  std::source_location location = std::source_location::current()) &&;
    [[nodiscard]] Error withContext(std::string_view key, std::string_view value) &&;
    /// Maps this failure to another owner's vocabulary at a boundary, keeping the
    /// original as the cause when the depth bound allows.
    [[nodiscard]] Error mappedTo(ErrorClass errorClass,
                                 ErrorDomain domain,
                                 ErrorCode code,
                                 std::string_view description,
                                 std::source_location location = std::source_location::current()) &&;

private:
    Error(ErrorClass errorClass, std::unique_ptr<ErrorDetail> detail) noexcept;

    friend std::unexpected<Error> fail(ErrorClass, ErrorDomain, ErrorCode, std::string_view, std::source_location);

    ErrorClass class_;
    // Never null for a constructed Error. Immutable through the public surface;
    // not const here so that consuming operations can move the cause chain out.
    std::unique_ptr<ErrorDetail> detail_;
};

} // namespace rawframe::result
