// Sound import against SPEC-0036's ingest list: each admitted form known by
// its signature, decoded at its own rate and channels to the tone it holds,
// refused past its limits or when it cannot be read, and cooked into a WAVE
// the runtime reads back. The fixtures are a quarter second of 440 Hz at
// half scale (and 880 Hz at a quarter on the Vorbis file's right), written
// by libsndfile 1.2.2: tone.ogg at 48 kHz stereo, tone.mp3 at 44.1 kHz mono.

#include "rawframe/audio/errors.h"
#include "rawframe/audio_import/import.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::audio_import;

namespace {

std::vector<std::byte> fixture(std::string_view name) {
    std::ifstream file{std::string{RAWFRAME_AUDIO_IMPORT_DATA} + std::string{name}, std::ios::binary};
    const std::vector<char> kRead{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::vector<std::byte> bytes(kRead.size());
    std::ranges::transform(kRead, bytes.begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return bytes;
}

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char each) {
        return static_cast<std::byte>(each);
    });
    return bytes;
}

/// Upward zero crossings of one channel over the middle fifth of a second,
/// and its peak there: a tone's frequency and level.
std::pair<std::size_t, float> tone(const audio::Clip& clip, std::uint32_t channel) {
    const std::size_t kFrom = clip.rate / 40;
    const std::size_t kTo = kFrom + (clip.rate / 5);
    std::size_t crossings = 0;
    float peak = 0;
    for (std::size_t frame = kFrom; frame < kTo && frame + 1 < clip.frames(); ++frame) {
        const float kHere = clip.samples[(frame * clip.channels) + channel];
        const float kNext = clip.samples[((frame + 1) * clip.channels) + channel];
        crossings += kHere < 0 && kNext >= 0 ? 1 : 0;
        peak = std::max(peak, std::abs(kHere));
    }
    return {crossings, peak};
}

bool badSound(const result::Result<audio::Clip>& imported) {
    return !imported.has_value() && imported.error().code() == code(audio::AudioError::BadSound);
}

} // namespace

RAWFRAME_TEST(FormsAreKnownByTheirSignatures) {
    RAWFRAME_EXPECT(sniff(fixture("tone.ogg")) == SourceForm::Vorbis);
    RAWFRAME_EXPECT(sniff(fixture("tone.mp3")) == SourceForm::Mp3);
    RAWFRAME_EXPECT(sniff(bytesOf("ID3\x04")) == SourceForm::Mp3);
    audio::Clip clip;
    clip.samples = {0.0F, 0.5F};
    RAWFRAME_EXPECT(sniff(cook(clip)) == SourceForm::Wave);
    RAWFRAME_EXPECT(!sniff(bytesOf("fLaC")).has_value());
    RAWFRAME_EXPECT(!sniff(bytesOf("RIFF")).has_value());
    RAWFRAME_EXPECT(!sniff({}).has_value());
}

RAWFRAME_TEST(VorbisAndMp3DecodeToTheirTones) {
    const auto kVorbis = importSound(fixture("tone.ogg"));
    RAWFRAME_EXPECT(kVorbis.has_value());
    if (kVorbis.has_value()) {
        RAWFRAME_EXPECT(kVorbis->channels == 2 && kVorbis->rate == 48'000 && kVorbis->frames() == 12'000);
        const auto [kLeftCrossings, kLeftPeak] = tone(*kVorbis, 0);
        const auto [kRightCrossings, kRightPeak] = tone(*kVorbis, 1);
        RAWFRAME_EXPECT(kLeftCrossings >= 86 && kLeftCrossings <= 90 && std::abs(kLeftPeak - 0.5F) < 0.05F);
        RAWFRAME_EXPECT(kRightCrossings >= 174 && kRightCrossings <= 178 && std::abs(kRightPeak - 0.25F) < 0.05F);
    }
    const auto kMp3 = importSound(fixture("tone.mp3"));
    RAWFRAME_EXPECT(kMp3.has_value());
    if (kMp3.has_value()) {
        // An encoder's delay and padding may lengthen it by up to a frame.
        RAWFRAME_EXPECT(kMp3->channels == 1 && kMp3->rate == 44'100 && kMp3->frames() >= 11'025 &&
                        kMp3->frames() <= 11'025 + 2'304);
        const auto [kCrossings, kPeak] = tone(*kMp3, 0);
        RAWFRAME_EXPECT(kCrossings >= 86 && kCrossings <= 90 && std::abs(kPeak - 0.5F) < 0.05F);
    }
}

RAWFRAME_TEST(ImportsAreRefusedPastLimitsAndWhenUnreadable) {
    RAWFRAME_EXPECT(badSound(importSound(fixture("tone.ogg"), {.maximumFrames = 1'000})));
    RAWFRAME_EXPECT(badSound(importSound(fixture("tone.mp3"), {.maximumFrames = 1'000})));
    RAWFRAME_EXPECT(badSound(importSound(fixture("tone.mp3"), {.maximumBytes = 100})));
    RAWFRAME_EXPECT(badSound(importSound(bytesOf("fLaC and more"))));
    RAWFRAME_EXPECT(badSound(importSound(bytesOf("OggS and nothing that follows"))));
    // A stream cut short decodes what it holds, or is refused; never more.
    for (const std::string_view kName : {"tone.ogg", "tone.mp3"}) {
        std::vector<std::byte> cut = fixture(kName);
        cut.resize(cut.size() / 2);
        const auto kImported = importSound(cut);
        RAWFRAME_EXPECT(badSound(kImported) || (kImported.has_value() && kImported->frames() < 12'000));
    }
}

RAWFRAME_TEST(ACookedSoundReadsBack) {
    const auto kVorbis = importSound(fixture("tone.ogg"));
    RAWFRAME_EXPECT(kVorbis.has_value());
    if (!kVorbis.has_value()) {
        return;
    }
    const std::vector<std::byte> kCooked = cook(*kVorbis);
    const auto kRead = importSound(kCooked);
    RAWFRAME_EXPECT(kRead.has_value() && kRead->channels == 2 && kRead->rate == 48'000 &&
                    kRead->frames() == kVorbis->frames());
    if (kRead.has_value()) {
        float worst = 0;
        for (std::size_t index = 0; index < kRead->samples.size(); ++index) {
            worst = std::max(worst, std::abs(kRead->samples[index] - std::clamp(kVorbis->samples[index], -1.0F, 1.0F)));
        }
        RAWFRAME_EXPECT(worst <= 1.0F / 32'768.0F);
    }
}

RAWFRAME_TEST(DamagedSourcesNeverDecodeOutOfBounds) {
    // Import is trusted with an author's own files (ADR-0058), but a damaged
    // file must still be refused or decoded, never read past its end;
    // AddressSanitizer watches this in the full check.
    std::mt19937 random{0x1d3a};
    std::size_t refused = 0;
    for (const std::string_view kName : {"tone.ogg", "tone.mp3"}) {
        const std::vector<std::byte> kOriginal = fixture(kName);
        for (int round = 0; round < 300; ++round) {
            std::vector<std::byte> damaged = kOriginal;
            const std::uint32_t kChanges = 1 + (random() % 8);
            for (std::uint32_t change = 0; change < kChanges; ++change) {
                damaged[random() % damaged.size()] = static_cast<std::byte>(random());
            }
            const auto kImported = importSound(damaged);
            refused += kImported.has_value() ? 0 : 1;
            RAWFRAME_EXPECT(!kImported.has_value() || kImported->frames() <= 48'000);
        }
    }
    std::printf("  %zu of 600 damaged sources refused\n", refused);
}
