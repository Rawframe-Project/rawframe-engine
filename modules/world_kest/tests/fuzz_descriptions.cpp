// Coverage-guided fuzzing of the descriptions a Build carries (D242): a
// game's description and a mod's, both read as text at load. Neither has a
// writer, so the search looks for what breaks the readers themselves; the
// seeds are every sample game's and mod's.

#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/mod.h"

#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    static_cast<void>(world_kest::parseGame(kText));
    static_cast<void>(world_kest::parseMod(kText));
    return 0;
}
