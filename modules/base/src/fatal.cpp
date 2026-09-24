#include "rawframe/base/fatal.h"

#include "rawframe/base/platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#ifdef _WIN32
// Both names belong to the Windows SDK's own include contract and cannot carry
// the repository's macro prefix.
// NOLINTNEXTLINE(readability-identifier-naming)
#define WIN32_LEAN_AND_MEAN
// NOLINTNEXTLINE(readability-identifier-naming)
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif
#endif

namespace rawframe::base {

namespace {

/// The default handler is also the freeze sentinel, so declare it first.
void defaultFatalHandler(const FatalRecord& record) noexcept;

// The two process-global mutable objects base owns, and the only two.
//
// SPEC-0046 states the reconciliation with ADR-0075's "no global mutable state":
// a fatal path cannot receive its handler as a parameter, because it is entered
// from an assertion macro in arbitrary code that has no handler to pass, and
// because it must work before any composition root exists.
//
// The freeze is not a third object. The handler slot carries three states:
// null means installation is open, the default handler's own address means
// frozen with nothing installed, and any other value means a handler is
// installed, which already closes installation. Freezing is therefore a compare
// and exchange from null to the default handler, and it needs no flag of its own.
//
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::atomic<FatalHandler> fatalHandlerSlot{nullptr};
constinit std::atomic<bool> fatalPathEntered{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// The fatal path may not take a lock: SPEC-0004 forbids one there, and a fatal
// path that blocks on a lock held by the thread that just died never terminates.
static_assert(std::atomic<FatalHandler>::is_always_lock_free,
              "the fatal handler slot must be lock-free on every admitted platform");
static_assert(std::atomic<bool>::is_always_lock_free,
              "the fatal re-entry flag must be lock-free on every admitted platform");

/// Every byte the default handler may produce, including its own framing.
constexpr std::size_t kFatalBufferBytes = 1536;

/// A stack-owned, bounded, allocation-free text accumulator. Every append
/// truncates rather than growing, so the fatal path has no failure mode of its
/// own once it starts writing.
struct BoundedText {
    std::array<char, kFatalBufferBytes> storage{};
    std::size_t used = 0;

    void append(std::string_view text, std::size_t limit = kFatalBufferBytes) noexcept {
        const std::size_t kWanted = text.size() < limit ? text.size() : limit;
        const std::size_t kSpace = kFatalBufferBytes - used;
        const std::size_t kCount = kWanted < kSpace ? kWanted : kSpace;
        std::copy_n(text.begin(), kCount, storage.begin() + static_cast<std::ptrdiff_t>(used));
        used += kCount;
    }

    void appendDecimal(std::uint64_t value) noexcept {
        // A do-while would say this once. The analyzer forbids one, and zero is
        // the only value a pre-tested loop drops, so it is answered first.
        if (value == 0) {
            append("0", 1);
            return;
        }

        // Twenty characters hold every std::uint64_t. The digits arrive least
        // significant first and are replayed in reverse.
        std::array<char, 20> digits{};
        char* const kCursor = digits.data();
        std::size_t count = 0;
        while (value != 0) {
            *(kCursor + count) = static_cast<char>('0' + static_cast<int>(value % 10));
            ++count;
            value /= 10;
        }

        while (count > 0) {
            --count;
            append(std::string_view{kCursor + count, 1}, 1);
        }
    }
};

[[nodiscard]] std::string_view reasonText(FatalReason reason) noexcept {
    switch (reason) {
    case FatalReason::AssertionFailed:
        return "AssertionFailed";
    case FatalReason::CheckFailed:
        return "CheckFailed";
    case FatalReason::Panic:
        return "Panic";
    case FatalReason::FatalPathReentered:
        return "FatalPathReentered";
    }
    return "Unknown";
}

/// Writes to the platform standard-error primitive directly rather than through
/// a buffered stream, because a stream takes a lock and the fatal path may not.
/// A failing write is ignored: the process is terminating either way, and
/// SPEC-0004 requires the path to terminate even when enrichment fails.
void writeStandardError(std::string_view text) noexcept {
#ifdef _WIN32
    void* handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    static_cast<void>(WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
#else
    const auto kResult = ::write(STDERR_FILENO, text.data(), text.size());
    static_cast<void>(kResult);
#endif
}

/// The OS thread identity where it can be read without allocating or locking.
[[nodiscard]] std::uint64_t readThreadIdentity() noexcept {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
    std::uint64_t identity = 0;
    static_cast<void>(pthread_threadid_np(nullptr, &identity));
    return identity;
#else
    // WebAssembly and anything else without a cheap thread identity.
    return 0;
#endif
}

/// Whether a debugger is attached, decided without allocating, locking, or
/// opening a file.
///
/// Windows answers this from the process environment block. Linux has no such
/// answer: every tracer check there reads /proc, which SPEC-0004 forbids on this
/// path. The break is therefore skipped on Linux rather than taken blindly,
/// because an unconditional trap with no debugger attached terminates the
/// process before the record is written, which loses the crash text in exactly
/// the case it matters. A Linux debugger still stops on the SIGABRT that
/// `raiseFatal` raises next, with the failing stack intact.
[[nodiscard]] bool debuggerAttached() noexcept {
#ifdef _WIN32
    return IsDebuggerPresent() != 0;
#else
    return false;
#endif
}

void defaultFatalHandler(const FatalRecord& record) noexcept {
    if (debuggerAttached()) {
        RAWFRAME_DEBUG_BREAK();
    }

    BoundedText text;
    text.append("rawframe fatal: ");
    text.append(reasonText(record.reason), kMaximumFatalTextBytes);
    text.append("\n  at ");
    text.append(std::string_view{record.location.file_name()}, kMaximumFatalTextBytes);
    text.append(":", 1);
    text.appendDecimal(record.location.line());
    text.append(":", 1);
    text.appendDecimal(record.location.column());
    text.append("\n  in ");
    text.append(std::string_view{record.location.function_name()}, kMaximumFatalTextBytes);
    if (!record.condition.empty()) {
        text.append("\n  condition: ");
        text.append(record.condition, kMaximumFatalTextBytes);
    }
    text.append("\n  message: ");
    text.append(record.message, kMaximumFatalTextBytes);
    text.append("\n  thread: ");
    text.appendDecimal(record.threadIdentity);
    text.append("\n", 1);

    writeStandardError(std::string_view{text.storage.data(), text.used});
}

} // namespace

bool installFatalHandler(FatalHandler handler) noexcept {
    if (handler == nullptr) {
        return false;
    }

    FatalHandler expected = nullptr;
    return fatalHandlerSlot.compare_exchange_strong(
        expected, handler, std::memory_order_acq_rel, std::memory_order_acquire);
}

void freezeFatalHandler() noexcept {
    FatalHandler expected = nullptr;
    static_cast<void>(fatalHandlerSlot.compare_exchange_strong(
        expected, &defaultFatalHandler, std::memory_order_acq_rel, std::memory_order_acquire));
}

void raiseFatal(const FatalRecord& record) noexcept {
    if (fatalPathEntered.exchange(true, std::memory_order_acq_rel)) {
        // Re-entry means the failure happened inside a handler, so the record a
        // handler would read is no longer trustworthy and no handler runs.
        // SPEC-0046 still requires the re-entry to be distinguishable in a crash
        // artifact, which is what FatalReason::FatalPathReentered exists for, so
        // one constant line is written. It reads no state and formats nothing.
        writeStandardError("rawframe fatal: FatalPathReentered\n");
        std::abort();
    }

    FatalRecord enriched = record;
    if (enriched.threadIdentity == 0) {
        enriched.threadIdentity = readThreadIdentity();
    }

    FatalHandler handler = fatalHandlerSlot.load(std::memory_order_acquire);
    if (handler == nullptr) {
        handler = &defaultFatalHandler;
    }
    handler(enriched);

    // Unconditional, including when the handler returned despite its contract.
    std::abort();
}

} // namespace rawframe::base
