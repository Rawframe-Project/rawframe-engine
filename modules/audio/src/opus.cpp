#include "cooked_opus.h"
#include "rawframe/audio/errors.h"

#include <algorithm>
#include <cstring>

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

constexpr std::size_t kHeader = 16;

} // namespace

result::Result<CookedOpus> readCookedOpus(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    if (bytes.size() > limits.maximumBytes) {
        return bad("the sound is larger than the limit");
    }
    if (bytes.size() < kHeader || std::memcmp(bytes.data(), kCookedOpusSignature.data(), 4) != 0 ||
        little(bytes, 4, 1) != kCookedOpusVersion) {
        return bad("not cooked Opus of version 1");
    }
    CookedOpus cooked{
        .channels = little(bytes, 5, 1), .preSkip = little(bytes, 6, 2), .frames = little(bytes, 8, 4), .packets = {}};
    const std::uint32_t kPackets = little(bytes, 12, 4);
    if (cooked.channels < 1 || cooked.channels > 2 || cooked.frames == 0 || cooked.frames > limits.maximumFrames) {
        return bad("cooked Opus has one or two channels and a length within the limit");
    }
    // Each packet takes at least three bytes, so the count is bounded by
    // the file before anything is reserved for it.
    if (kPackets > (bytes.size() - kHeader) / 3) {
        return bad("cooked Opus claims more packets than it could hold");
    }
    cooked.packets.reserve(kPackets);
    std::size_t at = kHeader;
    std::uint64_t start = 0;
    for (std::uint32_t packet = 0; packet < kPackets; ++packet) {
        if (at + 2 > bytes.size()) {
            return bad("cooked Opus ends inside its packets");
        }
        const std::size_t kLength = little(bytes, at, 2);
        at += 2;
        if (kLength == 0 || kLength > kLargestOpusPacket || at + kLength > bytes.size()) {
            return bad("a packet's length is outside Opus's or the file's");
        }
        const int kFrames = opus_packet_get_nb_samples(reinterpret_cast<const unsigned char*>(bytes.data() + at),
                                                       static_cast<opus_int32>(kLength),
                                                       static_cast<opus_int32>(kOpusRate));
        if (kFrames <= 0) {
            return bad("a packet whose length in frames cannot be read");
        }
        cooked.packets.push_back(CookedPacket{.offset = at, .length = kLength, .start = start});
        start += static_cast<std::uint64_t>(kFrames);
        at += kLength;
    }
    if (at != bytes.size()) {
        return bad("cooked Opus has bytes after its packets");
    }
    if (start < static_cast<std::uint64_t>(cooked.preSkip) + cooked.frames) {
        return bad("cooked Opus's packets do not hold what its header says");
    }
    return cooked;
}

result::Result<OpusDecoderOwner> makeOpusDecoder(std::uint32_t channels) {
    int status = OPUS_OK;
    OpusDecoderOwner decoder{
        opus_decoder_create(static_cast<opus_int32>(kOpusRate), static_cast<int>(channels), &status)};
    if (status != OPUS_OK || decoder == nullptr) {
        return bad("the Opus decoder could not be made");
    }
    return decoder;
}

result::Result<Clip> decodeCookedOpus(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const CookedOpus kCooked, readCookedOpus(bytes, limits));
    RAWFRAME_TRY_ASSIGN(const OpusDecoderOwner kDecoder, makeOpusDecoder(kCooked.channels));
    // Everything decoded, pre-skip included, never past what the header
    // says the packets hold.
    const std::size_t kWanted = static_cast<std::size_t>(kCooked.preSkip) + kCooked.frames;
    std::vector<float> decoded;
    decoded.reserve(kWanted * kCooked.channels);
    std::vector<float> packetFrames(static_cast<std::size_t>(kLargestPacketFrames) * kCooked.channels);
    for (const CookedPacket& packet : kCooked.packets) {
        const int kDecoded = opus_decode_float(kDecoder.get(),
                                               reinterpret_cast<const unsigned char*>(bytes.data() + packet.offset),
                                               static_cast<opus_int32>(packet.length),
                                               packetFrames.data(),
                                               kLargestPacketFrames,
                                               0);
        if (kDecoded < 0) {
            return bad("a packet libopus will not decode");
        }
        const std::size_t kTaken =
            std::min<std::size_t>(static_cast<std::size_t>(kDecoded), kWanted - (decoded.size() / kCooked.channels));
        decoded.insert(decoded.end(), packetFrames.begin(), packetFrames.begin() + (kTaken * kCooked.channels));
    }
    if (decoded.size() != kWanted * kCooked.channels) {
        return bad("cooked Opus's packets do not hold what its header says");
    }
    Clip clip;
    clip.channels = kCooked.channels;
    clip.rate = kOpusRate;
    clip.samples.assign(decoded.begin() + (static_cast<std::ptrdiff_t>(kCooked.preSkip) * kCooked.channels),
                        decoded.end());
    return clip;
}

result::Result<Clip> decodeCooked(std::span<const std::byte> bytes, const DecodeLimits& limits) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), kCookedOpusSignature.data(), 4) == 0) {
        return decodeCookedOpus(bytes, limits);
    }
    return decodeWav(bytes, limits);
}

} // namespace rawframe::audio
