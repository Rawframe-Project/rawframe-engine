// Coverage-guided fuzzing of the program materials a mod's Build can hold
// (D484): decoding never fails but by refusal, and what it accepts encodes
// back to the very same bytes.

#include "rawframe/material/material.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    const auto kRead = material::decodeProgram(kBytes);
    if (!kRead.has_value()) {
        return 0;
    }
    const std::vector<std::byte> kWritten = material::encodeProgram(*kRead);
    if (!std::ranges::equal(kWritten, kBytes)) {
        std::abort();
    }
    return 0;
}
