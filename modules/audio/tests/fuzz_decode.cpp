// Coverage-guided fuzzing of the sound decoders: cooked Opus and
// cooked WAVE from content a mod's Build can hold, where libopus decodes
// each packet, and WAVE as import reads it. The limits are the ones the
// engine ships, so a file's later packets decode as a player's would; what
// one input may ask for is what any sound may.

#include "rawframe/audio/decode.h"

#include <span>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    constexpr audio::DecodeLimits kLimits{};
    static_cast<void>(audio::decodeCooked(kBytes, kLimits));
    static_cast<void>(audio::decodeWav(kBytes, kLimits));
    return 0;
}
