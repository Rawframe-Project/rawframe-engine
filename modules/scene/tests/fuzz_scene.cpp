// Coverage-guided fuzzing of an authored scene (D242), which a mod's Build
// can hold. The reader is exact: a scene it accepts writes back to the very
// same text.

#include "rawframe/scene/scene.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kScene = scene::readScene(kText);
    if (!kScene.has_value()) {
        return 0;
    }
    const auto kWritten = scene::writeScene(*kScene);
    if (!kWritten.has_value() || *kWritten != kText) {
        std::abort();
    }
    return 0;
}
