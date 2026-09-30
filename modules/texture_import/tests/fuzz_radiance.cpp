// Coverage-guided fuzzing of a Radiance picture as the importer decodes it
// (D321): a file from anyone meets the run-length reader through the
// limits, and what decodes cooks into a cube its own reader takes back
// exactly.

#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kImage = texture_import::decodeRadiance(kBytes, {.maximumSide = 256});
    if (!kImage.has_value()) {
        return 0;
    }
    const auto kCooked = texture_import::cookEnvironment(*kImage, {.side = 4, .levels = 3, .samples = 4});
    if (!kCooked.has_value()) {
        std::abort();
    }
    const auto kEncoded = texture::encode(*kCooked);
    if (!kEncoded.has_value() || texture::decode(*kEncoded) != *kCooked) {
        std::abort();
    }
    return 0;
}
