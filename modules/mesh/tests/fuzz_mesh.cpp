// Coverage-guided fuzzing of a cooked mesh read from content (D242), which
// a mod's Build can hold. The reader is exact: a mesh it accepts encodes to
// the very same bytes.

#include "rawframe/mesh/mesh.h"

#include <algorithm>
#include <cstdlib>
#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kMesh = mesh::decode(kBytes);
    if (!kMesh.has_value()) {
        return 0;
    }
    const auto kEncoded = mesh::encode(*kMesh);
    if (!kEncoded.has_value() || !std::ranges::equal(*kEncoded, kBytes)) {
        std::abort();
    }
    return 0;
}
