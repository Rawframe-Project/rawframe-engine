#include "rawframe/execution/parallelism.h"

#include "rawframe/execution/bounds.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <string>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#elif defined(_WIN32)
#include <bit>
#include <windows.h>
#endif

namespace rawframe::execution {

namespace {

std::optional<std::uint64_t> parseUnsigned(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto kResult = std::from_chars(text.data(), text.data() + text.size(), value);
    if (kResult.ec != std::errc{} || kResult.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

#if defined(__linux__)

/// Reads a small file whole. Startup-only, so a plain stdio read is fine here.
std::string readSmallFile(const char* path) {
    std::string text;
    if (std::FILE* file = std::fopen(path, "r")) {
        std::array<char, 256> buffer{};
        const std::size_t kRead = std::fread(buffer.data(), 1, buffer.size(), file);
        text.assign(buffer.data(), kRead);
        std::fclose(file);
    }
    return text;
}

/// The cgroup v2 CPU quota of this process's own cgroup, if one is set.
std::optional<std::size_t> cgroupCpuLimit() {
    // "0::/path" is the unified hierarchy's line in /proc/self/cgroup.
    const std::string kMembership = readSmallFile("/proc/self/cgroup");
    const std::size_t kStart = kMembership.find("0::");
    if (kStart == std::string::npos) {
        return std::nullopt;
    }
    std::string path = kMembership.substr(kStart + 3);
    path = path.substr(0, path.find('\n'));
    const std::string kFile = "/sys/fs/cgroup" + path + "/cpu.max";
    return cpusFromCpuMax(readSmallFile(kFile.c_str()));
}

#endif

} // namespace

std::optional<std::size_t> cpusFromCpuMax(std::string_view line) noexcept {
    while (!line.empty() && (line.back() == '\n' || line.back() == ' ')) {
        line.remove_suffix(1);
    }
    const std::size_t kSpace = line.find(' ');
    if (kSpace == std::string_view::npos) {
        return std::nullopt;
    }
    const auto kQuota = parseUnsigned(line.substr(0, kSpace));
    const auto kPeriod = parseUnsigned(line.substr(kSpace + 1));
    if (!kQuota || !kPeriod || *kPeriod == 0) {
        return std::nullopt; // includes "max": no quota
    }
    return static_cast<std::size_t>(std::max<std::uint64_t>(1, (*kQuota + *kPeriod - 1) / *kPeriod));
}

std::size_t effectiveParallelism() noexcept {
    std::size_t count = 0;
#if defined(__linux__)
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof mask, &mask) == 0) {
        count = static_cast<std::size_t>(CPU_COUNT(&mask));
    }
    if (const auto kLimit = cgroupCpuLimit(); kLimit && (count == 0 || *kLimit < count)) {
        count = *kLimit;
    }
#elif defined(_WIN32)
    DWORD_PTR process = 0;
    DWORD_PTR system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) != 0) {
        count = static_cast<std::size_t>(std::popcount(static_cast<std::uint64_t>(process)));
    }
#endif
    // Only where no confinement can be read: other platforms, or a failed query.
    if (count == 0) {
        count = std::thread::hardware_concurrency();
    }
    return std::max<std::size_t>(count, 1);
}

std::size_t deriveCpuWorkerCount(std::optional<std::size_t> explicitCount) noexcept {
    std::size_t count = 0;
    if (explicitCount) {
        count = *explicitCount;
    } else {
        const std::size_t kParallelism = effectiveParallelism();
        count = kParallelism > kReservedSchedulingThreads ? kParallelism - kReservedSchedulingThreads : 0;
    }
    return std::clamp(count, kMinimumCpuWorkers, kMaximumCpuWorkers);
}

} // namespace rawframe::execution
