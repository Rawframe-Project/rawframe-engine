#include "rawframe/audio_import/import.h"

#include "decoders.h"
#include "rawframe/audio/errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <opus.h>
#include <string>

namespace rawframe::audio_import {

namespace {

std::unexpected<result::Error> refuse(std::string_view why) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::InvalidArgument, audio::kAudioDomain, code(audio::AudioError::BadSound), why)
            .error()};
}

bool begins(std::span<const std::byte> bytes, std::size_t at, std::string_view text) noexcept {
    return bytes.size() >= at + text.size() && std::memcmp(bytes.data() + at, text.data(), text.size()) == 0;
}

} // namespace

std::string_view describe(SourceForm form) noexcept {
    switch (form) {
    case SourceForm::Wave:
        return "wave";
    case SourceForm::Vorbis:
        return "vorbis";
    case SourceForm::Mp3:
        return "mp3";
    }
    return "unknown";
}

std::optional<SourceForm> sniff(std::span<const std::byte> bytes) noexcept {
    if (begins(bytes, 0, "RIFF") && begins(bytes, 8, "WAVE")) {
        return SourceForm::Wave;
    }
    if (begins(bytes, 0, "OggS")) {
        return SourceForm::Vorbis;
    }
    // An ID3v2 tag, or an MPEG audio frame's eleven sync bits.
    if (begins(bytes, 0, "ID3") ||
        (bytes.size() >= 2 && bytes[0] == std::byte{0xFF} && (bytes[1] & std::byte{0xE0}) == std::byte{0xE0})) {
        return SourceForm::Mp3;
    }
    return std::nullopt;
}

result::Result<audio::Clip> importSound(std::span<const std::byte> bytes, const audio::DecodeLimits& limits) {
    if (bytes.size() > limits.maximumBytes) {
        return refuse("the source is larger than the limit");
    }
    const std::optional<SourceForm> kForm = sniff(bytes);
    if (!kForm.has_value()) {
        return refuse("not a WAVE, Ogg Vorbis, or MP3 source");
    }
    if (*kForm == SourceForm::Wave) {
        return audio::decodeWav(bytes, limits);
    }
    rawframe_decoded decoded{};
    const rawframe_decode_result kResult =
        rawframe_decode_compressed(bytes.data(),
                                   bytes.size(),
                                   *kForm == SourceForm::Vorbis ? rawframe_compressed_vorbis : rawframe_compressed_mp3,
                                   limits.maximumFrames,
                                   &decoded);
    switch (kResult) {
    case rawframe_decode_ok:
        break;
    case rawframe_decode_unreadable:
        return std::unexpected<result::Error>{
            refuse("the stream cannot be read").error().withContext("form", std::string{describe(*kForm)})};
    case rawframe_decode_too_long:
        return refuse("the source is longer than the limit");
    case rawframe_decode_out_of_memory:
        return std::unexpected<result::Error>{result::fail(result::ErrorClass::ResourceExhausted,
                                                           audio::kAudioDomain,
                                                           code(audio::AudioError::BadSound),
                                                           "out of memory decoding the source")
                                                  .error()};
    }
    audio::Clip clip;
    clip.channels = decoded.channels;
    clip.rate = decoded.rate;
    const bool kAdmitted = clip.channels >= 1 && clip.channels <= 2 && clip.rate >= 8'000 && clip.rate <= 192'000;
    if (kAdmitted) {
        clip.samples.assign(decoded.samples, decoded.samples + (decoded.frames * decoded.channels));
    }
    rawframe_release_decoded(&decoded);
    if (!kAdmitted) {
        return refuse("a source has one or two channels at 8 to 192 kHz");
    }
    // Decoders may overshoot full scale a little; the cooked form clips.
    return clip;
}

std::vector<std::byte> cook(const audio::Clip& clip) {
    return audio::encodeWav(clip);
}

namespace {

constexpr int kPacketFrames = 960;

void putLittle(std::vector<std::byte>& out, std::uint32_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFU));
    }
}

struct EncoderRelease {
    void operator()(OpusEncoder* encoder) const noexcept {
        opus_encoder_destroy(encoder);
    }
};

} // namespace

result::Result<std::vector<std::byte>> cookOpus(const audio::Clip& clip, const OpusSettings& settings) {
    if (clip.rate != audio::kOpusRate || clip.channels < 1 || clip.channels > 2 || clip.frames() == 0 ||
        clip.frames() > 0xFFFF'FFFFU) {
        return refuse("cooked Opus holds one or two channels at 48 kHz");
    }
    const auto kChannels = static_cast<int>(clip.channels);
    int status = OPUS_OK;
    const std::unique_ptr<OpusEncoder, EncoderRelease> kEncoder{
        opus_encoder_create(static_cast<opus_int32>(audio::kOpusRate), kChannels, OPUS_APPLICATION_AUDIO, &status)};
    if (status != OPUS_OK || kEncoder == nullptr) {
        return refuse("the Opus encoder could not be made");
    }
    opus_encoder_ctl(kEncoder.get(),
                     OPUS_SET_BITRATE(settings.bitrate == 0 ? OPUS_AUTO : static_cast<opus_int32>(settings.bitrate)));
    opus_encoder_ctl(kEncoder.get(), OPUS_SET_COMPLEXITY(static_cast<opus_int32>(std::min(settings.complexity, 10U))));
    opus_int32 preSkip = 0;
    opus_encoder_ctl(kEncoder.get(), OPUS_GET_LOOKAHEAD(&preSkip));

    // The clip, then silence enough to push its end out past the lookahead,
    // in whole packets.
    const std::size_t kNeeded = clip.frames() + static_cast<std::size_t>(preSkip);
    const std::size_t kPackets = (kNeeded + kPacketFrames - 1) / kPacketFrames;
    std::vector<float> input(kPackets * kPacketFrames * clip.channels, 0.0F);
    std::ranges::copy(clip.samples, input.begin());

    std::vector<std::byte> out;
    out.insert(out.end(),
               reinterpret_cast<const std::byte*>(audio::kCookedOpusSignature.data()),
               reinterpret_cast<const std::byte*>(audio::kCookedOpusSignature.data()) + 4);
    putLittle(out, audio::kCookedOpusVersion, 1);
    putLittle(out, clip.channels, 1);
    putLittle(out, static_cast<std::uint32_t>(preSkip), 2);
    putLittle(out, static_cast<std::uint32_t>(clip.frames()), 4);
    putLittle(out, static_cast<std::uint32_t>(kPackets), 4);
    std::array<unsigned char, audio::kLargestOpusPacket> packet{};
    for (std::size_t index = 0; index < kPackets; ++index) {
        const opus_int32 kLength = opus_encode_float(kEncoder.get(),
                                                     input.data() + (index * kPacketFrames * clip.channels),
                                                     kPacketFrames,
                                                     packet.data(),
                                                     static_cast<opus_int32>(packet.size()));
        if (kLength <= 0) {
            return refuse("the Opus encoder refused a packet");
        }
        putLittle(out, static_cast<std::uint32_t>(kLength), 2);
        out.insert(out.end(),
                   reinterpret_cast<const std::byte*>(packet.data()),
                   reinterpret_cast<const std::byte*>(packet.data()) + kLength);
    }
    return out;
}

} // namespace rawframe::audio_import
