#include "rawframe/audio/decode.h"
#include "rawframe/audio/errors.h"

#include <cstring>
#include <memory>
#include <opus.h>

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

/// The most frames one Opus packet decodes to: 120 ms at 48 kHz.
constexpr int kLargestPacketFrames = 5'760;
constexpr std::size_t kHeader = 16;

struct DecoderRelease {
    void operator()(OpusDecoder* decoder) const noexcept {
        opus_decoder_destroy(decoder);
    }
};

} // namespace

result::Result<Clip> decodeCookedOpus(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    if (bytes.size() > limits.maximumBytes) {
        return bad("the sound is larger than the limit");
    }
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), kCookedOpusSignature.data(), 4) != 0 ||
        little(bytes, 4, 1) != kCookedOpusVersion) {
        return bad("not cooked Opus of version 1");
    }
    const std::uint32_t kChannels = little(bytes, 5, 1);
    const std::uint32_t kPreSkip = little(bytes, 6, 2);
    const std::uint32_t kFrames = little(bytes, 8, 4);
    const std::uint32_t kPackets = little(bytes, 12, 4);
    if (kChannels < 1 || kChannels > 2 || kFrames == 0 || kFrames > limits.maximumFrames) {
        return bad("cooked Opus has one or two channels and a length within the limit");
    }
    int status = OPUS_OK;
    const std::unique_ptr<OpusDecoder, DecoderRelease> kDecoder{
        opus_decoder_create(static_cast<opus_int32>(kOpusRate), static_cast<int>(kChannels), &status)};
    if (status != OPUS_OK || kDecoder == nullptr) {
        return bad("the Opus decoder could not be made");
    }
    // Everything decoded, pre-skip included, never past what the header
    // says the packets hold.
    const std::size_t kWanted = static_cast<std::size_t>(kPreSkip) + kFrames;
    std::vector<float> decoded;
    decoded.reserve(kWanted * kChannels);
    std::vector<float> packetFrames(static_cast<std::size_t>(kLargestPacketFrames) * kChannels);
    std::size_t at = kHeader;
    for (std::uint32_t packet = 0; packet < kPackets; ++packet) {
        if (at + 2 > bytes.size()) {
            return bad("cooked Opus ends inside its packets");
        }
        const std::size_t kLength = little(bytes, at, 2);
        at += 2;
        if (kLength == 0 || kLength > kLargestOpusPacket || at + kLength > bytes.size()) {
            return bad("a packet's length is outside Opus's or the file's");
        }
        const int kDecoded = opus_decode_float(kDecoder.get(),
                                               reinterpret_cast<const unsigned char*>(bytes.data() + at),
                                               static_cast<opus_int32>(kLength),
                                               packetFrames.data(),
                                               kLargestPacketFrames,
                                               0);
        at += kLength;
        if (kDecoded < 0) {
            return bad("a packet libopus will not decode");
        }
        const std::size_t kTaken =
            std::min<std::size_t>(static_cast<std::size_t>(kDecoded), kWanted - (decoded.size() / kChannels));
        decoded.insert(decoded.end(), packetFrames.begin(), packetFrames.begin() + (kTaken * kChannels));
    }
    if (at != bytes.size() || decoded.size() != kWanted * kChannels) {
        return bad("cooked Opus's packets do not hold what its header says");
    }
    Clip clip;
    clip.channels = kChannels;
    clip.rate = kOpusRate;
    clip.samples.assign(decoded.begin() + (static_cast<std::ptrdiff_t>(kPreSkip) * kChannels), decoded.end());
    return clip;
}

result::Result<Clip> decodeCooked(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), kCookedOpusSignature.data(), 4) == 0) {
        return decodeCookedOpus(bytes, limits);
    }
    return decodeWav(bytes, limits);
}

} // namespace rawframe::audio
