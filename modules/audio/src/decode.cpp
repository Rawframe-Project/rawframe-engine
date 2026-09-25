#include "rawframe/audio/decode.h"

#include "rawframe/audio/errors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace rawframe::audio {

namespace {

std::unexpected<result::Error> bad(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, kAudioDomain, code(AudioError::BadSound), why).error()};
}

std::uint32_t little(std::span<const std::byte> bytes, std::size_t at, std::size_t width) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + index])) << (8 * index);
    }
    return value;
}

bool tagIs(std::span<const std::byte> bytes, std::size_t at, std::string_view tag) noexcept {
    return at + 4 <= bytes.size() && std::memcmp(bytes.data() + at, tag.data(), 4) == 0;
}

struct Format {
    std::uint32_t channels = 0;
    std::uint32_t rate = 0;
    std::uint32_t bits = 0;
};

/// The tail every PCM subformat GUID of WAVE_FORMAT_EXTENSIBLE shares.
constexpr std::array<std::uint8_t, 14> kPcmGuidTail = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

std::optional<Format> readFormat(std::span<const std::byte> chunk) noexcept {
    if (chunk.size() < 16) {
        return std::nullopt;
    }
    std::uint32_t tag = little(chunk, 0, 2);
    const Format kFormat{.channels = little(chunk, 2, 2), .rate = little(chunk, 4, 4), .bits = little(chunk, 14, 2)};
    if (tag == 0xFFFE) {
        // Extensible: the subformat's first two bytes are the real tag.
        if (chunk.size() < 40 || little(chunk, 16, 2) < 22) {
            return std::nullopt;
        }
        tag = little(chunk, 24, 2);
        for (std::size_t index = 0; index < kPcmGuidTail.size(); ++index) {
            if (std::to_integer<std::uint8_t>(chunk[26 + index]) != kPcmGuidTail[index]) {
                return std::nullopt;
            }
        }
    }
    const std::uint32_t kBlock = little(chunk, 12, 2);
    if (tag != 1 || kFormat.channels < 1 || kFormat.channels > 2 || kFormat.rate < 8'000 || kFormat.rate > 192'000 ||
        (kFormat.bits != 16 && kFormat.bits != 24) || kBlock != kFormat.channels * kFormat.bits / 8) {
        return std::nullopt;
    }
    return kFormat;
}

} // namespace

result::Result<Clip> decodeWav(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    if (bytes.size() > limits.maximumBytes) {
        return bad("the sound is larger than allowed");
    }
    if (bytes.size() < 12 || !tagIs(bytes, 0, "RIFF") || !tagIs(bytes, 8, "WAVE")) {
        return bad("not a RIFF WAVE");
    }
    std::optional<Format> format;
    std::span<const std::byte> data;
    bool haveData = false;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::size_t kSize = little(bytes, at + 4, 4);
        if (kSize > bytes.size() - at - 8) {
            return bad("a chunk runs past the end");
        }
        const std::span<const std::byte> kChunk = bytes.subspan(at + 8, kSize);
        if (tagIs(bytes, at, "fmt ")) {
            if (format) {
                return bad("two format chunks");
            }
            format = readFormat(kChunk);
            if (!format) {
                return bad("not PCM of one or two channels, 16 or 24 bits, 8 to 192 kHz");
            }
        } else if (tagIs(bytes, at, "data")) {
            if (haveData) {
                return bad("two data chunks");
            }
            data = kChunk;
            haveData = true;
        }
        // Chunks are padded to an even size.
        at += 8 + kSize + (kSize % 2);
    }
    if (!format || !haveData) {
        return bad("a WAVE needs a format chunk and a data chunk");
    }
    const std::size_t kWidth = format->bits / 8;
    const std::size_t kFrame = kWidth * format->channels;
    if (data.size() % kFrame != 0) {
        return bad("the data is not whole frames");
    }
    if (data.size() / kFrame > limits.maximumFrames) {
        return bad("the sound is longer than allowed");
    }
    Clip clip;
    clip.channels = format->channels;
    clip.rate = format->rate;
    clip.samples.reserve(data.size() / kWidth);
    for (std::size_t at = 0; at < data.size(); at += kWidth) {
        const std::uint32_t kRaw = little(data, at, kWidth);
        // Sign-extend from the sample's width, then scale to -1 up to 1.
        const auto kShift = static_cast<std::uint32_t>(32 - (8 * kWidth));
        const auto kSigned = static_cast<std::int32_t>(kRaw << kShift) >> kShift;
        clip.samples.push_back(static_cast<float>(kSigned) / static_cast<float>(1U << ((8 * kWidth) - 1)));
    }
    return clip;
}

std::vector<std::byte> encodeWav(const Clip& clip) {
    const auto kData = static_cast<std::uint32_t>(clip.samples.size() * 2);
    std::vector<std::byte> out;
    out.reserve(44 + kData);
    const auto kTag = [&out](std::string_view tag) {
        for (const char kChar : tag) {
            out.push_back(static_cast<std::byte>(kChar));
        }
    };
    const auto kPut = [&out](std::uint32_t value, std::size_t width) {
        for (std::size_t index = 0; index < width; ++index) {
            out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFU));
        }
    };
    kTag("RIFF");
    kPut(36 + kData, 4);
    kTag("WAVEfmt ");
    kPut(16, 4);
    kPut(1, 2);
    kPut(clip.channels, 2);
    kPut(clip.rate, 4);
    kPut(clip.rate * clip.channels * 2, 4);
    kPut(clip.channels * 2, 2);
    kPut(16, 2);
    kTag("data");
    kPut(kData, 4);
    for (const float kSample : clip.samples) {
        const float kClipped = std::isnan(kSample) ? 0.0F : std::clamp(kSample, -1.0F, 32767.0F / 32768.0F);
        kPut(static_cast<std::uint32_t>(static_cast<std::int16_t>(std::lround(kClipped * 32768.0F))), 2);
    }
    return out;
}

} // namespace rawframe::audio
