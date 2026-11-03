// Coverage-guided fuzzing of a source image as the importer decodes it:
// a file from anyone meets Wuffs's decoders through the limits the engine
// ships, and what decodes cooks into a texture its own reader takes back
// exactly. A run-length BMP of a few bytes may declare the largest side.

#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <utility>

using namespace rawframe;

namespace {

/// The source's texture, or nothing for a source the importer refuses. The
/// image is let go once it is cooked: at the largest side it is 256 MiB,
/// and the round trip holds the texture three times over besides.
std::optional<texture::Texture> cookedFrom(std::span<const std::byte> bytes) {
    const auto kImage = texture_import::decodeImage(bytes, {});
    if (!kImage.has_value()) {
        return std::nullopt;
    }
    auto cooked = texture_import::cookTexture(*kImage, {.exact = true});
    if (!cooked.has_value()) {
        std::abort();
    }
    return std::move(*cooked);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const std::optional<texture::Texture> kCooked = cookedFrom(kBytes);
    if (!kCooked.has_value()) {
        return 0;
    }
    const auto kEncoded = texture::encode(*kCooked);
    if (!kEncoded.has_value() || texture::decode(*kEncoded) != *kCooked) {
        std::abort();
    }
    return 0;
}
