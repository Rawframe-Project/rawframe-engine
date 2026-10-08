#include "rawframe/process/self.h"

#include <string>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file (D237).
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif

namespace rawframe::process {

std::filesystem::path ownExecutable() {
#if defined(_WIN32)
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD kLength = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (kLength == 0) {
            return {};
        }
        if (kLength < path.size()) {
            path.resize(kLength);
            return path;
        }
        path.resize(path.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    static_cast<void>(::_NSGetExecutablePath(nullptr, &size));
    std::string path(size, '\0');
    if (::_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    path.resize(path.find('\0'));
    return path;
#else
    return "/proc/self/exe";
#endif
}

std::filesystem::path besideSelf(std::string_view name) {
#if defined(_WIN32)
    const std::string kName = std::string{name} + ".exe";
#else
    const std::string kName{name};
#endif
    std::error_code error;
    const std::filesystem::path kHere = std::filesystem::canonical(ownExecutable(), error);
    const std::filesystem::path kBeside = kHere.parent_path() / kName;
    return !error && std::filesystem::is_regular_file(kBeside, error) ? kBeside : std::filesystem::path{};
}

} // namespace rawframe::process
