#pragma once

// Decoding the cooked forms a client reads into clips (ADR-0058): 16-bit PCM
// WAVE, the short-form tier, and cooked Opus, the lossy tier; import tooling
// writes both. Every decoder is bounded and refuses what it does not fully
// understand.

#include "rawframe/audio/mixer.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::audio {

/// The resource type of every cooked sound clip, in either cooked form: the
/// identity content catalogs and cooks name it by.
inline constexpr base::Bits128 kSoundClipType = base::parseBits128Hex("684215ae3a2a3f665361a5a7b3b260c6").value;

struct DecodeLimits {
    std::size_t maximumBytes = std::size_t{256} * 1024 * 1024;
    /// Ten minutes of 48 kHz.
    std::size_t maximumFrames = std::size_t{48'000} * 600;
};

/// A RIFF WAVE of PCM (plain or extensible), one or two channels, 16 or 24
/// bits, 8 to 192 kHz. Refuses (`BadSound`) anything else, and anything
/// past the limits.
[[nodiscard]] result::Result<Clip> decodeWav(std::span<const std::byte> bytes, const DecodeLimits& limits = {});

/// Cooked Opus as import writes it, all little-endian: the signature, the
/// version, the channels (one or two), the encoder's pre-skip in frames, the
/// clip's frames, the packet count, then each packet as a 16-bit length and
/// its bytes. It decodes at 48 kHz.
inline constexpr std::array<char, 4> kCookedOpusSignature = {'R', 'F', 'O', 'P'};
inline constexpr std::uint8_t kCookedOpusVersion = 1;
inline constexpr std::uint32_t kOpusRate = 48'000;
/// The largest packet Opus makes (RFC 6716, section 3.4).
inline constexpr std::size_t kLargestOpusPacket = 1275;

/// Refuses (`BadSound`) anything but cooked Opus of that version, a packet
/// libopus will not decode, and anything past the limits.
[[nodiscard]] result::Result<Clip> decodeCookedOpus(std::span<const std::byte> bytes, const DecodeLimits& limits = {});

/// Either cooked form, by its signature.
[[nodiscard]] result::Result<Clip> decodeCooked(std::span<const std::byte> bytes, const DecodeLimits& limits = {});

/// A clip as a 16-bit PCM WAVE, samples clipped to the 16-bit range, which
/// `decodeWav` reads back.
[[nodiscard]] std::vector<std::byte> encodeWav(const Clip& clip);

} // namespace rawframe::audio
