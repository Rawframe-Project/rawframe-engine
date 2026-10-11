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
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file (D237).
#include <windows.h>
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif
#endif
#if !defined(_WIN32) && !defined(__wasm__)
#include <csignal>
#include <unwind.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <link.h>
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

    /// `value` as sixteen hexadecimal digits after "0x".
    void appendHex(std::uint64_t value) noexcept {
        std::array<char, 18> digits{'0', 'x'};
        for (std::size_t at = 0; at < 16; ++at) {
            const auto kNibble = static_cast<unsigned>((value >> ((15 - at) * 4)) & 0xFU);
            digits.at(2 + at) = static_cast<char>(kNibble < 10 ? '0' + kNibble : 'a' + kNibble - 10);
        }
        append(std::string_view{digits.data(), digits.size()});
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

#if !defined(__wasm__)

/// The return addresses a crash record holds at most.
constexpr std::size_t kCrashFrames = 32;

/// A crash record's frames, gathered by the platform's unwinder or its stack
/// walk, never by anything that allocates.
struct CrashFrames {
    std::array<std::uint64_t, kCrashFrames> addresses{};
    std::size_t count = 0;
};

/// A crash's minimal bounded evidence (ADR-0017, D617), written before the
/// process ends as it would have.
void writeCrashRecord(std::string_view what,
                      std::uint64_t code,
                      std::uint64_t address,
                      std::uint64_t program,
                      const CrashFrames& frames) noexcept {
    BoundedText text;
    text.append("rawframe crash: ");
    text.append(what, kMaximumFatalTextBytes);
    text.append("\n  code: ");
    text.appendDecimal(code);
    text.append("\n  address: ");
    text.appendHex(address);
    text.append("\n  thread: ");
    text.appendDecimal(readThreadIdentity());
    text.append("\n  program: ");
    text.appendHex(program);
    text.append("\n  frames:");
    for (std::size_t at = 0; at < frames.count; ++at) {
        text.append(" ", 1);
        text.appendHex(frames.addresses.at(at));
    }
    text.append("\n", 1);
    writeStandardError(std::string_view{text.storage.data(), text.used});
}

#endif

#if !defined(_WIN32) && !defined(__wasm__)

/// The faults a crash record is written for, the abort last.
constexpr std::array<int, 5> kCrashSignals = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

// The crash handler's state, which a signal handler can reach in no other
// way, as the fatal path's (SPEC-0046): the handlers it replaced, the
// program's load address, and the stack it runs on when the thread's own
// overflowed. Each is set once, by `installCrashCapture`, before any crash.
//
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
constinit std::array<struct sigaction, kCrashSignals.size()> crashPrevious{};
constinit std::uint64_t crashProgram = 0;
constinit std::atomic<bool> crashInstalled{false};
alignas(16) constinit std::array<char, 64 * 1024> crashStack{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] std::string_view signalText(int signal) noexcept {
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGABRT:
        return "SIGABRT";
    default:
        return "signal";
    }
}

_Unwind_Reason_Code collectFrame(struct _Unwind_Context* context, void* into) {
    auto& frames = *static_cast<CrashFrames*>(into);
    if (frames.count >= frames.addresses.size()) {
        return _URC_END_OF_STACK;
    }
    frames.addresses.at(frames.count) = static_cast<std::uint64_t>(_Unwind_GetIP(context));
    ++frames.count;
    return _URC_NO_REASON;
}

void crashed(int signal, siginfo_t* info, void*) {
    const auto kSlot = static_cast<std::size_t>(std::ranges::find(kCrashSignals, signal) - kCrashSignals.begin());
    // The fatal path's own abort has said everything already, and a fault in
    // this handler says nothing more.
    if (!fatalPathEntered.exchange(true, std::memory_order_acq_rel)) {
        CrashFrames frames;
        _Unwind_Backtrace(&collectFrame, &frames);
        writeCrashRecord(signalText(signal),
                         static_cast<std::uint64_t>(signal),
                         info != nullptr ? reinterpret_cast<std::uint64_t>(info->si_addr) : 0,
                         crashProgram,
                         frames);
    }
    // The process ends as it would have: the fault runs again under the
    // handler before this one, a sanitizer's or the default; an abort is
    // raised again.
    if (kSlot < crashPrevious.size()) {
        sigaction(signal, &crashPrevious.at(kSlot), nullptr);
    }
    if (signal == SIGABRT || (info != nullptr && info->si_code <= 0)) {
        static_cast<void>(raise(signal));
    }
}

#if !defined(__APPLE__)
int firstObject(struct dl_phdr_info* info, std::size_t, void* into) {
    *static_cast<std::uint64_t*>(into) = static_cast<std::uint64_t>(info->dlpi_addr);
    return 1;
}
#endif

#endif

#ifdef _WIN32
LONG WINAPI crashedOnException(EXCEPTION_POINTERS* exception) {
    if (!fatalPathEntered.exchange(true, std::memory_order_acq_rel) && exception != nullptr &&
        exception->ExceptionRecord != nullptr) {
        CrashFrames frames;
        std::array<void*, kCrashFrames> captured{};
        const USHORT kCaptured =
            RtlCaptureStackBackTrace(0, static_cast<DWORD>(captured.size()), captured.data(), nullptr);
        for (USHORT at = 0; at < kCaptured; ++at) {
            frames.addresses.at(at) = reinterpret_cast<std::uint64_t>(captured.at(at));
        }
        frames.count = kCaptured;
        writeCrashRecord("exception",
                         static_cast<std::uint64_t>(exception->ExceptionRecord->ExceptionCode),
                         reinterpret_cast<std::uint64_t>(exception->ExceptionRecord->ExceptionAddress),
                         reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),
                         frames);
    }
    // Windows ends the process as it would have, its error reporting too.
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

} // namespace

bool installCrashCapture() noexcept {
#if defined(_WIN32)
    static constinit std::atomic<bool> installed{false};
    if (installed.exchange(true)) {
        return false;
    }
    SetUnhandledExceptionFilter(&crashedOnException);
    return true;
#elif defined(__wasm__)
    return false;
#else
    if (crashInstalled.exchange(true)) {
        return false;
    }
#if defined(__APPLE__)
    crashProgram = reinterpret_cast<std::uint64_t>(_dyld_get_image_header(0));
#else
    dl_iterate_phdr(&firstObject, &crashProgram);
#endif
    // The unwinder loads what it needs on its first walk, which a signal
    // handler may not: it walks once here.
    CrashFrames warm;
    _Unwind_Backtrace(&collectFrame, &warm);
    // A thread whose stack overflowed has none left for the handler; this
    // one gets a stack of its own (other threads' overflows end unrecorded).
    stack_t alternate{};
    alternate.ss_sp = crashStack.data();
    alternate.ss_size = crashStack.size();
    static_cast<void>(sigaltstack(&alternate, nullptr));
    struct sigaction action{};
    action.sa_sigaction = &crashed;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t at = 0; at < kCrashSignals.size(); ++at) {
        sigaction(kCrashSignals.at(at), &action, &crashPrevious.at(at));
    }
    return true;
#endif
}

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
