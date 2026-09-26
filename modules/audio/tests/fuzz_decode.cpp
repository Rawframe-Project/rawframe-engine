// Coverage-guided fuzzing of the sound decoders (D242): cooked Opus and
// cooked WAVE from content a mod's Build can hold, where libopus decodes
// each packet, and WAVE as import reads it. The limits are small so no one
// input can ask for much.

#include "rawframe/audio/decode.h"

#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    constexpr audio::DecodeLimits kLimits{.maximumBytes = 1U << 16U, .maximumFrames = 48'000};
    static_cast<void>(audio::decodeCooked(kBytes, kLimits));
    static_cast<void>(audio::decodeWav(kBytes, kLimits));
    return 0;
}
