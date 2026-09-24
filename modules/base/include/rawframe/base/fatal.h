#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace rawframe::base {

/// Why the process is terminating. Closed: a new reason is a specification change.
enum class FatalReason : std::uint8_t {
    AssertionFailed,
    CheckFailed,
    Panic,
    FatalPathReentered,
};

/// What the fatal path knows, bounded by construction.
struct FatalRecord {
    FatalReason reason;
    std::source_location location;
    /// The stringified condition for AssertionFailed and CheckFailed, empty for Panic.
    std::string_view condition;
    /// The call site's message. Static storage by construction: the macros accept
    /// only a string literal, so this view cannot dangle and needs no copy.
    std::string_view message;
    /// The OS thread identity where it can be read without allocating or locking,
    /// otherwise zero. A caller leaves this zero and `raiseFatal` fills it in.
    std::uint64_t threadIdentity;
};

/// A handler must not return. One is installed at most once, before the freeze.
using FatalHandler = void (*)(const FatalRecord&) noexcept;

/// Installs the process fatal handler. Returns false and changes nothing if a
/// handler is already installed, if the freeze point has passed, or if the
/// handler is null. It never terminates: rejecting an installation is the
/// caller's failure to handle, not a reason to kill a healthy process.
[[nodiscard]] bool installFatalHandler(FatalHandler handler) noexcept;

/// Closes installation permanently. The composition root calls this before
/// starting concurrent work. Calling it twice is harmless.
void freezeFatalHandler() noexcept;

/// Enters the fatal path and does not return. Public because the macros expand
/// to it, not because ordinary code should call it.
[[noreturn]] void raiseFatal(const FatalRecord& record) noexcept;

/// The default handler's message and condition truncation limit, in bytes.
inline constexpr std::size_t kMaximumFatalTextBytes = 256;

} // namespace rawframe::base
