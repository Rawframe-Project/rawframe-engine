// Coverage-guided fuzzing of the cook's font sanitizer (ADR-0078 section 2,
// D385): a font from anyone meets OTS through the limits, and what it
// rebuilds it takes again.

#include "rawframe/font_import/sanitize.h"

#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kRebuilt = font_import::sanitize(kBytes, {.maximumBytes = 1U << 20U});
    if (!kRebuilt.has_value()) {
        return 0;
    }
    if (!font_import::sanitize(*kRebuilt, {.maximumBytes = 1U << 21U}).has_value()) {
        std::abort();
    }
    return 0;
}
