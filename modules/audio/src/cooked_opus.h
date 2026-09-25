#pragma once

// Cooked Opus's container read once, for decoding it whole and for
// streaming it: the header checked and every packet found, so decoding only
// ever reads where the table says.

#include "rawframe/audio/decode.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <opus.h>
#include <span>
#include <vector>

namespace rawframe::audio {

struct CookedPacket {
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct CookedOpus {
    std::uint32_t channels = 0;
    std::uint32_t preSkip = 0;
    std::uint32_t frames = 0;
    std::vector<CookedPacket> packets;
};

/// The most frames one Opus packet decodes to: 120 ms at 48 kHz.
inline constexpr int kLargestPacketFrames = 5'760;

/// Refuses (`BadSound`) what `decodeCookedOpus` documents about the
/// container; the packets themselves are libopus's to judge.
[[nodiscard]] result::Result<CookedOpus> readCookedOpus(std::span<const std::byte> bytes, const DecodeLimits& limits);

struct OpusDecoderRelease {
    void operator()(OpusDecoder* decoder) const noexcept {
        opus_decoder_destroy(decoder);
    }
};
using OpusDecoderOwner = std::unique_ptr<OpusDecoder, OpusDecoderRelease>;

[[nodiscard]] result::Result<OpusDecoderOwner> makeOpusDecoder(std::uint32_t channels);

} // namespace rawframe::audio
