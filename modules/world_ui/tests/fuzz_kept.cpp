// Coverage-guided fuzzing of the player's kept settings (D626), a file a
// player may edit: read, at most as many values as are kept, each finite,
// and what is read written and read again the same.

#include "../src/keeper.h"

#include <cmath>
#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kKept = world_ui::readKept(kText);
    if (!kKept.has_value()) {
        return 0;
    }
    if (kKept->size() > world_ui::kMostKeptValues) {
        std::abort();
    }
    for (const auto& [kName, kValue] : *kKept) {
        if (kName.empty() || !std::isfinite(kValue)) {
            std::abort();
        }
    }
    if (world_ui::readKept(world_ui::writeKept(*kKept)) != *kKept) {
        std::abort();
    }
    return 0;
}
