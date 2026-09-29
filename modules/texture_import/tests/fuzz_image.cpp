// Coverage-guided fuzzing of a source image as the importer decodes it
// (D253): a file from anyone meets Wuffs's decoders through the limits, and
// what decodes cooks into a texture its own reader takes back exactly.

#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kImage = texture_import::decodeImage(kBytes, {.maximumSide = 256});
    if (!kImage.has_value()) {
        return 0;
    }
    const auto kCooked = texture_import::cookTexture(*kImage, {.exact = true});
    if (!kCooked.has_value()) {
        std::abort();
    }
    const auto kEncoded = texture::encode(*kCooked);
    if (!kEncoded.has_value() || texture::decode(*kEncoded) != *kCooked) {
        std::abort();
    }
    return 0;
}
