// Coverage-guided fuzzing of a cooked texture read from content (D253), which
// a mod's Build can hold. The reader is exact: a texture it accepts encodes to
// the very same bytes.

#include "rawframe/texture/texture.h"

#include <algorithm>
#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kTexture = texture::decode(kBytes);
    if (!kTexture.has_value()) {
        return 0;
    }
    const auto kEncoded = texture::encode(*kTexture);
    if (!kEncoded.has_value() || !std::ranges::equal(*kEncoded, kBytes)) {
        std::abort();
    }
    return 0;
}
