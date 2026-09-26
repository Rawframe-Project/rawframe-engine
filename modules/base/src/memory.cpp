#include "rawframe/base/memory.h"

#include <algorithm>

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

std::optional<std::uint64_t> residentBytes() noexcept {
#if defined(__linux__)
    // statm: total pages, then resident pages.
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
    while (at != kEnd && *at != ' ') {
        ++at;
    }
    std::uint64_t pages = 0;
    if (at == kEnd || std::from_chars(at + 1, kEnd, pages).ec != std::errc{}) {
        return std::nullopt;
    }
    const long kPage = ::sysconf(_SC_PAGESIZE);
    return kPage > 0 ? std::optional{pages * static_cast<std::uint64_t>(kPage)} : std::nullopt;
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

} // namespace rawframe::base
