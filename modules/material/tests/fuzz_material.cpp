// Coverage-guided fuzzing of the surface materials a mod's Build can hold:
// the reader takes the canonical form only, so what it accepts writes back
// to the very same text, compiling it never fails but by refusal, and what
// compiles is cooked and read back as it was; writing it as Slang never
// fails but by refusal either (D483).

#include "rawframe/material/material.h"

#include <cstdlib>
#include <string_view>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view kText{reinterpret_cast<const char*>(data), size};
    const auto kRead = material::readMaterial(kText);
    if (!kRead.has_value()) {
        return 0;
    }
    const auto kWritten = material::writeMaterial(*kRead);
    if (!kWritten.has_value() || *kWritten != kText) {
        std::abort();
    }
    if (const auto kGenerated = material::generateSlang(*kRead); kGenerated.has_value()) {
        if (kGenerated->source.empty() || kGenerated->textures.size() > 4) {
            std::abort();
        }
    }
    if (const auto kCompiled = material::compileQualities(*kRead); kCompiled.has_value()) {
        for (const material::Material& each : *kCompiled) {
            static_cast<void>(material::blobOf(each));
        }
        const auto kDecoded = material::decode(material::encode(*kCompiled));
        if (!kDecoded.has_value() || *kDecoded != *kCompiled) {
            std::abort();
        }
    }
    return 0;
}
