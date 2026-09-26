// Coverage-guided fuzzing of the cooked records a Build holds (D242): a
// cooked game and a cooked mod. Both readers are exact: a record either
// accepts writes back to the very same bytes.

#include "rawframe/world_kest/cooked_game.h"
#include "rawframe/world_kest/cooked_mod.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kBytes{reinterpret_cast<const char*>(data), size};
    if (const auto kGame = world_kest::readCookedGame(kBytes); kGame.has_value()) {
        const auto kWritten = world_kest::writeCookedGame(*kGame);
        if (!kWritten.has_value() || *kWritten != kBytes) {
            std::abort();
        }
    }
    if (const auto kMod = world_kest::readCookedMod(kBytes); kMod.has_value()) {
        const auto kWritten = world_kest::writeCookedMod(*kMod);
        if (!kWritten.has_value() || *kWritten != kBytes) {
            std::abort();
        }
    }
    return 0;
}
