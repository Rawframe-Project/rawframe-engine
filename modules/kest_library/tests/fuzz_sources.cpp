// Coverage-guided fuzzing of a game's sources as a resource holds them
// (D242), which a mod's Build can carry. The reader is exact: files it
// accepts write back to the very same bytes.

#include "rawframe/kest_library/library.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kBytes{reinterpret_cast<const char*>(data), size};
    const auto kFiles = kest_library::readGameSources(kBytes);
    if (!kFiles.has_value()) {
        return 0;
    }
    const auto kWritten = kest_library::writeGameSources(*kFiles);
    if (!kWritten.has_value() || *kWritten != kBytes) {
        std::abort();
    }
    return 0;
}
