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
#include <numbers>
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

template <typename Value> bool badSound(const result::Result<Value>& imported) {
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

namespace {

audio::Clip sineClip(std::uint32_t channels, float seconds) {
    audio::Clip clip;
    clip.channels = channels;
    clip.rate = audio::kOpusRate;
    const auto kFrames = static_cast<std::size_t>(seconds * static_cast<float>(audio::kOpusRate));
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const float kHertz = channel == 0 ? 440.0F : 660.0F;
            clip.samples.push_back(0.5F * std::sin(2.0F * std::numbers::pi_v<float> * kHertz *
                                                   static_cast<float>(frame) / static_cast<float>(audio::kOpusRate)));
        }
    }
    return clip;
}

/// The error's power against the original's, in decibels, frame for frame
/// with no shift: how well the decoded clip lines up with what was cooked.
float alignedError(const audio::Clip& original, const audio::Clip& decoded) {
    double signal = 0;
    double error = 0;
    for (std::size_t index = 0; index < original.samples.size(); ++index) {
        signal += static_cast<double>(original.samples[index]) * original.samples[index];
        const double kDifference = static_cast<double>(decoded.samples[index]) - original.samples[index];
        error += kDifference * kDifference;
    }
    return static_cast<float>(10.0 * std::log10(error / signal));
}

} // namespace

RAWFRAME_TEST(OpusCooksAndDecodesInLine) {
    for (const std::uint32_t kChannels : {1U, 2U}) {
        const audio::Clip kOriginal = sineClip(kChannels, 0.5F);
        const auto kCooked = cookOpus(kOriginal);
        RAWFRAME_EXPECT(kCooked.has_value());
        if (!kCooked.has_value()) {
            continue;
        }
        // Far smaller than the samples, and the same bytes every time.
        RAWFRAME_EXPECT(kCooked->size() < kOriginal.samples.size() * 2 / 8);
        RAWFRAME_EXPECT(*cookOpus(kOriginal) == *kCooked);
        const auto kDecoded = audio::decodeCooked(*kCooked);
        RAWFRAME_EXPECT(kDecoded.has_value());
        if (!kDecoded.has_value()) {
            continue;
        }
        RAWFRAME_EXPECT(kDecoded->channels == kChannels && kDecoded->rate == audio::kOpusRate &&
                        kDecoded->frames() == kOriginal.frames());
        const float kError = alignedError(kOriginal, *kDecoded);
        RAWFRAME_EXPECT(kError < -20);
        std::printf("  %u channels: %zu bytes cooked, error %.1f dB\n", kChannels, kCooked->size(), kError);
    }
    // A source at another rate is not resampled into Opus.
    const auto kMp3 = importSound(fixture("tone.mp3"));
    RAWFRAME_EXPECT(kMp3.has_value() && badSound(cookOpus(*kMp3)));
}

RAWFRAME_TEST(CookedOpusIsRefusedWhenItLies) {
    const std::vector<std::byte> kCooked = *cookOpus(sineClip(1, 0.1F));
    const auto kRefused = [](std::vector<std::byte> bytes) {
        return badSound(audio::decodeCookedOpus(bytes));
    };
    const auto kWith = [&kCooked](std::size_t at, std::uint8_t value) {
        std::vector<std::byte> changed = kCooked;
        changed[at] = std::byte{value};
        return changed;
    };
    RAWFRAME_EXPECT(audio::decodeCookedOpus(kCooked).has_value());
    RAWFRAME_EXPECT(kRefused(kWith(0, 'X')));
    RAWFRAME_EXPECT(kRefused(kWith(4, 2)));
    RAWFRAME_EXPECT(kRefused(kWith(5, 3)));
    RAWFRAME_EXPECT(kRefused(kWith(5, 0)));
    // More frames than the packets hold, and fewer packets than there are.
    RAWFRAME_EXPECT(kRefused(kWith(10, 0x7F)));
    RAWFRAME_EXPECT(kRefused(kWith(12, 1)));
    // A first packet longer than Opus allows, or empty.
    RAWFRAME_EXPECT(kRefused(kWith(17, 0x10)));
    std::vector<std::byte> empty = kWith(16, 0);
    empty[17] = std::byte{0};
    RAWFRAME_EXPECT(kRefused(empty));
    std::vector<std::byte> trailing = kCooked;
    trailing.push_back(std::byte{0});
    RAWFRAME_EXPECT(kRefused(trailing));
    std::vector<std::byte> cut = kCooked;
    cut.resize(cut.size() - 1);
    RAWFRAME_EXPECT(kRefused(cut));
    RAWFRAME_EXPECT(badSound(audio::decodeCookedOpus(kCooked, {.maximumFrames = 100})));
    RAWFRAME_EXPECT(badSound(audio::decodeCookedOpus(kCooked, {.maximumBytes = 100})));

    // Damaged packets decode or are refused, never out of bounds;
    // AddressSanitizer watches this in the full check.
    std::mt19937 random{0x0b05};
    std::size_t refused = 0;
    for (int round = 0; round < 500; ++round) {
        std::vector<std::byte> damaged = kCooked;
        for (std::uint32_t change = 0; change < 1 + (random() % 6); ++change) {
            damaged[16 + (random() % (damaged.size() - 16))] = static_cast<std::byte>(random());
        }
        refused += audio::decodeCookedOpus(damaged).has_value() ? 0 : 1;
    }
    std::printf("  %zu of 500 damaged cooked sounds refused\n", refused);
}
