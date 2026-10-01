// Coverage-guided fuzzing of the canvas materials a mod's Build can hold
// (D355): the reader takes the canonical form only, so what it accepts
// writes back to the very same text, folding it never fails but by
// refusal, and what folds is cooked and read back as it was.

#include "rawframe/material/canvas.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kRead = material::readCanvas(kText);
    if (!kRead.has_value()) {
        return 0;
    }
    const auto kWritten = material::writeCanvas(*kRead);
    if (!kWritten.has_value() || *kWritten != kText) {
        std::abort();
    }
    if (const auto kCompiled = material::compileCanvas(*kRead); kCompiled.has_value()) {
        const auto kDecoded = material::decodeCanvas(material::encodeCanvas(*kCompiled));
        if (!kDecoded.has_value() || *kDecoded != *kCompiled) {
            std::abort();
        }
    }
    return 0;
}
