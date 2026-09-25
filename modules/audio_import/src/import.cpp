#include "rawframe/audio_import/import.h"

#include "decoders.h"
#include "rawframe/audio/errors.h"

#include <algorithm>
#include <cstring>
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

} // namespace rawframe::audio_import
