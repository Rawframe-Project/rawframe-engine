#pragma once

// The generation-1 container's fixed-width pieces (SPEC-0011): little-endian
// integers, the prologue, chunk headers, and the footer.

#include "rawframe/result/result.h"
#include "rawframe/world_snapshot/checkpoint.h"
#include "rawframe/world_snapshot/errors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rawframe::world_snapshot::detail {

inline constexpr std::size_t kPrologueSize = 32;
inline constexpr std::size_t kChunkHeaderSize = 96;
inline constexpr std::size_t kFooterSize = 128;
inline constexpr std::uint16_t kGeneration = 1;
inline constexpr std::uint8_t kLittleEndian = 1;
inline constexpr std::uint8_t kSha256 = 1;
/// Rawframe's canonical field records rather than FlatBuffers (D29).
inline constexpr std::uint16_t kPayloadCodec = 2;
inline constexpr std::uint16_t kNoCompression = 0;

enum class ChunkKind : std::uint16_t {
    WorldHeader = 1,
    EntityDirectory = 2,
    ComponentRows = 3,
    Manifest = 4,
    /// The World's random streams, which D18 keeps in the World (D29).
    RandomStreams = 5,
};

using Subject = std::array<std::byte, 16>;

struct ChunkHeader {
    ChunkKind kind = ChunkKind::WorldHeader;
    std::uint64_t ordinal = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t recordCount = 0;
    Fingerprint digest{};
    Subject subject{};
    /// Where the header starts in the artifact: the manifest says so, the
    /// header itself does not.
    std::uint64_t offset = 0;
};

[[nodiscard]] inline std::unexpected<result::Error>
fail(result::ErrorClass errorClass, SnapshotError error, std::string_view why) {
    return result::fail(errorClass, kSnapshotDomain, code(error), why);
}

[[nodiscard]] inline std::unexpected<result::Error> malformed(std::string_view why) {
    return fail(result::ErrorClass::DataLoss, SnapshotError::Malformed, why);
}

/// Appends little-endian values.
class Output {
public:
    void u8(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }
    void u16(std::uint16_t value) {
        little(value, 2);
    }
    void u32(std::uint32_t value) {
        little(value, 4);
    }
    void u64(std::uint64_t value) {
        little(value, 8);
    }
    void bytes(std::span<const std::byte> data) {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }
    void zeros(std::size_t count) {
        bytes_.insert(bytes_.end(), count, std::byte{0});
    }
    [[nodiscard]] std::vector<std::byte>& data() noexcept {
        return bytes_;
    }

private:
    void little(std::uint64_t value, int width) {
        for (int index = 0; index < width; ++index) {
            bytes_.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xFFU));
        }
    }

    std::vector<std::byte> bytes_;
};

/// Reads little-endian values off a bounded range. A read past the end fails
/// and so does every read after it.
class Input {
public:
    explicit Input(std::span<const std::byte> input) noexcept : input_(input) {
    }

    [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
        std::uint64_t wide = 0;
        const bool kRead = little(wide, 1);
        value = static_cast<std::uint8_t>(wide);
        return kRead;
    }
    [[nodiscard]] bool u16(std::uint16_t& value) noexcept {
        std::uint64_t wide = 0;
        const bool kRead = little(wide, 2);
        value = static_cast<std::uint16_t>(wide);
        return kRead;
    }
    [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
        std::uint64_t wide = 0;
        const bool kRead = little(wide, 4);
        value = static_cast<std::uint32_t>(wide);
        return kRead;
    }
    [[nodiscard]] bool u64(std::uint64_t& value) noexcept {
        return little(value, 8);
    }
    [[nodiscard]] bool take(std::size_t count, std::span<const std::byte>& taken) noexcept {
        if (failed_ || count > input_.size() - at_) {
            failed_ = true;
            return false;
        }
        taken = input_.subspan(at_, count);
        at_ += count;
        return true;
    }
    template <std::size_t kCount> [[nodiscard]] bool array(std::array<std::byte, kCount>& into) noexcept {
        std::span<const std::byte> taken;
        if (!take(kCount, taken)) {
            return false;
        }
        std::copy(taken.begin(), taken.end(), into.begin());
        return true;
    }
    [[nodiscard]] bool zeros(std::size_t count) noexcept {
        std::span<const std::byte> taken;
        return take(count, taken) && std::ranges::all_of(taken, [](std::byte value) {
                   return value == std::byte{0};
               });
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return input_.size() - at_;
    }

private:
    [[nodiscard]] bool little(std::uint64_t& value, std::size_t width) noexcept {
        std::span<const std::byte> taken;
        if (!take(width, taken)) {
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < width; ++index) {
            value |= std::to_integer<std::uint64_t>(taken[index]) << (8U * index);
        }
        return true;
    }

    std::span<const std::byte> input_;
    std::size_t at_ = 0;
    bool failed_ = false;
};

void writePrologue(Output& out);
[[nodiscard]] result::Status readPrologue(std::span<const std::byte> artifact);

/// Appends a chunk: its header, with the payload's size and digest, then the
/// payload. Returns the header as the manifest records it.
ChunkHeader writeChunk(Output& out,
                       ChunkKind kind,
                       std::uint64_t ordinal,
                       std::uint64_t recordCount,
                       const Subject& subject,
                       std::span<const std::byte> payload);
/// The chunk header at `offset`, its payload checked against its digest.
[[nodiscard]] result::Result<ChunkHeader> readChunk(std::span<const std::byte> artifact, std::uint64_t offset);

struct Footer {
    std::uint64_t manifestOffset = 0;
    std::uint64_t manifestSize = 0;
    std::uint64_t chunkCount = 0;
    std::uint64_t preFooterSize = 0;
    Fingerprint digest{};
    Fingerprint manifestDigest{};
};

void writeFooter(Output& out, const Footer& footer);
/// The footer at the end of `artifact`, with the digest of everything before
/// it checked.
[[nodiscard]] result::Result<Footer> readFooter(std::span<const std::byte> artifact);

/// The digest a profile's limits are known by.
[[nodiscard]] Fingerprint profileFingerprint(const SnapshotLimits& limits);
/// The digest this codec is known by: a change to how bytes are written
/// changes it.
[[nodiscard]] Fingerprint toolchainFingerprint();

} // namespace rawframe::world_snapshot::detail
