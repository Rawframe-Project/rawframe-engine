// Coverage-guided fuzzing of a sound source as the importer decodes it:
// WAVE, Ogg Vorbis and MP3 from an author's tools meet stb_vorbis,
// miniaudio's MP3 decoder and the WAVE reader through the limits, and what
// decodes is a clip the importer may cook: one or two channels at a rate it
// takes, finite samples, and a cooked WAVE the runtime reads back at the
// same shape.

#include "rawframe/audio/decode.h"
#include "rawframe/audio_import/import.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>
#include <vector>

using namespace rawframe;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> kBytes{reinterpret_cast<const std::byte*>(data), size};
    // The limits the engine ships, so the later packets of a stream are
    // decoded as an author's file would be.
    constexpr audio::DecodeLimits kLimits{};
    const auto kClip = audio_import::importSound(kBytes, kLimits);
    if (!kClip.has_value()) {
        return 0;
    }
    if (kClip->channels < 1 || kClip->channels > 2 || kClip->rate < 8'000 || kClip->rate > 192'000 ||
        kClip->samples.size() % kClip->channels != 0 || kClip->frames() > kLimits.maximumFrames ||
        !std::ranges::all_of(kClip->samples, [](float sample) {
            return std::isfinite(sample);
        })) {
        std::abort();
    }
    // The cooked form read back under limits of what it holds exactly.
    const std::vector<std::byte> kWave = audio_import::cook(*kClip);
    const auto kCooked = audio::decodeWav(kWave, {.maximumBytes = kWave.size(), .maximumFrames = kClip->frames()});
    if (!kCooked.has_value() || kCooked->channels != kClip->channels || kCooked->rate != kClip->rate ||
        kCooked->frames() != kClip->frames()) {
        std::abort();
    }
    return 0;
}
