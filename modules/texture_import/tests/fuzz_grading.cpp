// Coverage-guided fuzzing of a grading table as the importer decodes it
// (D344): a `.cube` file from anyone meets the line reader through the
// limits, and what decodes is a volume the texture's own writer and reader
// take back exactly.

#include "rawframe/texture/texture.h"
#include "rawframe/texture_import/import.h"

#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kTable = texture_import::decodeGrading(kBytes, {.maximumSide = 8});
    if (!kTable.has_value()) {
        return 0;
    }
    const auto kEncoded = texture::encode(*kTable);
    if (!kEncoded.has_value() || texture::decode(*kEncoded) != *kTable) {
        std::abort();
    }
    return 0;
}
