// WAVE decoding: 16- and 24-bit PCM of one or two channels, plain or
// extensible, chunks padded as RIFF pads them; everything else refused, and
// hostile bytes never read out of bounds (AddressSanitizer runs this).

#include "rawframe/audio/decode.h"
#include "rawframe/audio/errors.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio;

namespace {

void put(std::vector<std::byte>& out, std::uint32_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFU));
    }
}

void tag(std::vector<std::byte>& out, std::string_view name) {
    for (const char kChar : name) {
        out.push_back(static_cast<std::byte>(kChar));
    }
}

struct Wave {
    std::uint32_t channels = 1;
    std::uint32_t rate = 48'000;
    std::uint32_t bits = 16;
    std::uint32_t formatTag = 1;
    bool extensible = false;
    /// Raw sample values, interleaved, at `bits`.
    std::vector<std::int32_t> samples;
    /// An odd-sized chunk before the data, which RIFF pads.
    bool extraChunk = false;
};

std::vector<std::byte> wave(const Wave& wave) {
    std::vector<std::byte> body;
    tag(body, "WAVE");
    tag(body, "fmt ");
    put(body, wave.extensible ? 40 : 16, 4);
    put(body, wave.extensible ? 0xFFFE : wave.formatTag, 2);
    put(body, wave.channels, 2);
    put(body, wave.rate, 4);
    put(body, wave.rate * wave.channels * wave.bits / 8, 4);
    put(body, wave.channels * wave.bits / 8, 2);
    put(body, wave.bits, 2);
    if (wave.extensible) {
        put(body, 22, 2);
        put(body, wave.bits, 2);
        put(body, 0, 4);
        put(body, wave.formatTag, 2);
        for (const std::uint8_t kByte :
             {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}) {
            body.push_back(static_cast<std::byte>(kByte));
        }
    }
    if (wave.extraChunk) {
        tag(body, "LIST");
        put(body, 3, 4);
        put(body, 0x414243, 3);
        body.push_back(std::byte{0});
    }
    tag(body, "data");
    put(body, static_cast<std::uint32_t>(wave.samples.size() * wave.bits / 8), 4);
    for (const std::int32_t kSample : wave.samples) {
        put(body, static_cast<std::uint32_t>(kSample), wave.bits / 8);
    }
    std::vector<std::byte> file;
    tag(file, "RIFF");
    put(file, static_cast<std::uint32_t>(body.size()), 4);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

bool refused(const std::vector<std::byte>& bytes, const DecodeLimits& limits = {}) {
    const auto kClip = decodeWav(bytes, limits);
    return !kClip.has_value() && kClip.error().code() == code(AudioError::BadSound);
}

} // namespace

RAWFRAME_TEST(PcmWavesDecode) {
    const auto kMono = decodeWav(wave({.samples = {0, 16384, -32768, 32767}}));
    RAWFRAME_EXPECT(kMono.has_value() && kMono->channels == 1 && kMono->rate == 48'000 && kMono->frames() == 4);
    RAWFRAME_EXPECT(kMono.has_value() && kMono->samples[1] == 0.5F && kMono->samples[2] == -1.0F &&
                    kMono->samples[3] == 32767.0F / 32768.0F);
    const auto kStereo = decodeWav(
        wave({.channels = 2, .rate = 44'100, .bits = 24, .samples = {-8388608, 4194304, 1, -1}, .extraChunk = true}));
    RAWFRAME_EXPECT(kStereo.has_value() && kStereo->channels == 2 && kStereo->rate == 44'100 &&
                    kStereo->frames() == 2 && kStereo->samples[0] == -1.0F && kStereo->samples[1] == 0.5F &&
                    kStereo->samples[3] == -1.0F / 8388608.0F);
    const auto kExtensible = decodeWav(wave({.extensible = true, .samples = {8192}}));
    RAWFRAME_EXPECT(kExtensible.has_value() && kExtensible->samples[0] == 0.25F);
}

RAWFRAME_TEST(OtherFormsAreRefused) {
    RAWFRAME_EXPECT(refused(wave({.bits = 8, .samples = {1, 2}})));
    RAWFRAME_EXPECT(refused(wave({.bits = 32, .samples = {1}})));
    RAWFRAME_EXPECT(refused(wave({.channels = 3, .samples = {1, 2, 3}})));
    RAWFRAME_EXPECT(refused(wave({.rate = 4'000, .samples = {1}})));
    // IEEE float, plain and extensible.
    RAWFRAME_EXPECT(refused(wave({.formatTag = 3, .samples = {1}})));
    RAWFRAME_EXPECT(refused(wave({.formatTag = 3, .extensible = true, .samples = {1}})));
    // A frame cut in half, a data chunk longer than the file, no data.
    auto partial = wave({.channels = 2, .samples = {1, 2}});
    partial[partial.size() - 7] = std::byte{6};
    partial.resize(partial.size() - 2);
    RAWFRAME_EXPECT(refused(partial));
    auto truncated = wave({.samples = {1, 2, 3, 4}});
    truncated.resize(truncated.size() - 2);
    RAWFRAME_EXPECT(refused(truncated));
    auto headerOnly = wave({.samples = {}});
    headerOnly.resize(36);
    RAWFRAME_EXPECT(refused(headerOnly));
    RAWFRAME_EXPECT(refused({}));
    // Limits.
    RAWFRAME_EXPECT(refused(wave({.samples = {1, 2, 3}}), {.maximumBytes = 1 << 20, .maximumFrames = 2}));
    RAWFRAME_EXPECT(refused(wave({.samples = {1, 2, 3}}), {.maximumBytes = 40, .maximumFrames = 100}));
}

RAWFRAME_TEST(HostileBytesAreRefusedOrDecodedWithinBounds) {
    const std::vector<std::byte> kSeed =
        wave({.channels = 2, .bits = 24, .samples = {1, -1, 100, -100, 8388607, -8388608}, .extraChunk = true});
    std::uint64_t state = 0x2545F4914F6CDD1DULL;
    const auto kNext = [&state] {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    };
    int decoded = 0;
    for (int round = 0; round < 20'000; ++round) {
        std::vector<std::byte> bytes = kSeed;
        const int kEdits = 1 + static_cast<int>(kNext() % 3);
        for (int edit = 0; edit < kEdits; ++edit) {
            const std::size_t kAt = kNext() % bytes.size();
            if (kNext() % 4 == 0) {
                bytes.resize(kAt);
                if (bytes.empty()) {
                    break;
                }
            } else {
                bytes[kAt] = static_cast<std::byte>(kNext() & 0xFFU);
            }
        }
        const auto kClip = decodeWav(bytes);
        if (!kClip.has_value()) {
            continue;
        }
        ++decoded;
        for (const float kSample : kClip->samples) {
            RAWFRAME_EXPECT(kSample >= -1.0F && kSample < 1.0F);
        }
        RAWFRAME_EXPECT(kClip->samples.size() == kClip->frames() * kClip->channels);
    }
    RAWFRAME_EXPECT(decoded > 100);
}
