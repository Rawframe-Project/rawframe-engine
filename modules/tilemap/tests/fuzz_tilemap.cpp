// Coverage-guided fuzzing of a tilemap document, which a mod's Build can
// hold and the Runtime reads. What the reader takes, the writer writes in
// a form it reads back to the same map, and that form writes as itself.

#include "rawframe/tilemap/tilemap.h"

#include <cstdlib>
#include <string>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kMap = tilemap::parseTilemap(kText);
    if (!kMap.has_value()) {
        return 0;
    }
    const std::string kWritten = tilemap::writeTilemap(*kMap);
    const auto kAgain = tilemap::parseTilemap(kWritten);
    if (!kAgain.has_value() || *kAgain != *kMap || tilemap::writeTilemap(*kAgain) != kWritten) {
        std::abort();
    }
    for (const tilemap::SolidRun& each : tilemap::solidRuns(*kMap)) {
        if (!(each.width > 0) || !(each.height > 0)) {
            std::abort();
        }
    }
    return 0;
}
