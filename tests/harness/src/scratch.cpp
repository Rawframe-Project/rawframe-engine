#include "rawframe/test/scratch.h"

#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace rawframe::test {

std::filesystem::path scratchDirectory(std::string_view name) {
#if defined(_WIN32)
    const auto kProcess = static_cast<long long>(::_getpid());
#else
    const auto kProcess = static_cast<long long>(::getpid());
#endif
    return std::filesystem::temp_directory_path() / ("rawframe-" + std::string{name} + "-" + std::to_string(kProcess));
}

} // namespace rawframe::test
