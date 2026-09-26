#include "rawframe/base/usage.h"

#include <algorithm>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#if defined(__linux__)
#include <array>
#include <charconv>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

namespace rawframe::base {

namespace {

/// A field of /proc/self/statm, in bytes: 1 resident, 2 shared (file-backed).
std::optional<std::uint64_t> statm(int field) noexcept {
#if defined(__linux__)
    const int kFile = ::open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    if (kFile < 0) {
        return std::nullopt;
    }
    std::array<char, 128> text{};
    const ssize_t kRead = ::read(kFile, text.data(), text.size() - 1);
    ::close(kFile);
    if (kRead <= 0) {
        return std::nullopt;
    }
    const char* const kEnd = text.data() + kRead;
    const char* at = text.data();
    for (int skipped = 0; skipped < field; ++skipped) {
        while (at != kEnd && *at != ' ') {
            ++at;
        }
        if (at == kEnd) {
            return std::nullopt;
        }
        ++at;
    }
    std::uint64_t pages = 0;
    if (std::from_chars(at, kEnd, pages).ec != std::errc{}) {
        return std::nullopt;
    }
    const long kPage = ::sysconf(_SC_PAGESIZE);
    return kPage > 0 ? std::optional{pages * static_cast<std::uint64_t>(kPage)} : std::nullopt;
#else
    static_cast<void>(field);
    return std::nullopt;
#endif
}

} // namespace

std::optional<std::uint64_t> residentBytes() noexcept {
#if defined(__linux__)
    return statm(1);
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(info.resident_size);
#else
    return std::nullopt;
#endif
}

std::optional<std::uint64_t> fileResidentBytes() noexcept {
    return statm(2);
}

std::optional<HeapUsage> heapUsage() noexcept {
#if defined(__GLIBC__)
    // Small blocks in use and large ones mapped on their own; free space the
    // arenas hold.
    const struct mallinfo2 kInfo = ::mallinfo2();
    return HeapUsage{.inUseBytes = kInfo.uordblks + kInfo.hblkhd, .freeBytes = kInfo.fordblks};
#else
    return std::nullopt;
#endif
}

std::optional<std::uint64_t> peakResidentBytes() noexcept {
#if defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
        return std::nullopt;
    }
    // Kibibytes on Linux, bytes on macOS. The kernel brings its high-water
    // mark up to date only now and then, so what is resident now may be past
    // it.
#if defined(__linux__)
    const std::uint64_t kPeak = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#else
    const auto kPeak = static_cast<std::uint64_t>(usage.ru_maxrss);
#endif
    return std::max(kPeak, residentBytes().value_or(0));
#else
    return std::nullopt;
#endif
}

std::optional<std::uint64_t> cpuNanoseconds() noexcept {
#if defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return std::nullopt;
    }
    const auto kNanoseconds = [](const timeval& value) {
        return (static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000U) +
               (static_cast<std::uint64_t>(value.tv_usec) * 1'000U);
    };
    return kNanoseconds(usage.ru_utime) + kNanoseconds(usage.ru_stime);
#else
    return std::nullopt;
#endif
}

} // namespace rawframe::base
