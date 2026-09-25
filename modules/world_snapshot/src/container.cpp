#include "container.h"

namespace rawframe::world_snapshot::detail {

namespace {

constexpr std::array<std::byte, 4> kPrologueMagic = {std::byte{'R'}, std::byte{'F'}, std::byte{'S'}, std::byte{'N'}};
constexpr std::array<std::byte, 4> kChunkMagic = {std::byte{'R'}, std::byte{'F'}, std::byte{'C'}, std::byte{'H'}};
constexpr std::array<std::byte, 4> kFooterMagic = {std::byte{'R'}, std::byte{'F'}, std::byte{'S'}, std::byte{'F'}};

bool knownKind(std::uint16_t kind) noexcept {
    return kind >= static_cast<std::uint16_t>(ChunkKind::WorldHeader) &&
           kind <= static_cast<std::uint16_t>(ChunkKind::RandomStreams);
}

} // namespace

void writePrologue(Output& out) {
    out.bytes(kPrologueMagic);
    out.u16(kGeneration);
    out.u16(kPrologueSize);
    out.u8(kLittleEndian);
    out.u8(kSha256);
    out.u8(static_cast<std::uint8_t>(kPayloadCodec));
    // No compression is permitted anywhere in the artifact.
    out.u8(0);
    out.u32(kChunkHeaderSize);
    out.zeros(16);
}

result::Status readPrologue(std::span<const std::byte> artifact) {
    Input in{artifact};
    std::array<std::byte, 4> magic{};
    std::uint16_t generation = 0;
    std::uint16_t size = 0;
    std::uint8_t order = 0;
    std::uint8_t digest = 0;
    std::uint8_t codec = 0;
    std::uint8_t features = 0;
    std::uint32_t chunkHeader = 0;
    if (!in.array(magic) || magic != kPrologueMagic) {
        return malformed("not a Rawframe World checkpoint: the prologue magic is wrong");
    }
    if (!in.u16(generation) || !in.u16(size) || !in.u8(order) || !in.u8(digest) || !in.u8(codec) || !in.u8(features) ||
        !in.u32(chunkHeader) || !in.zeros(16)) {
        return malformed("the prologue is short or has a reserved byte set");
    }
    if (generation != kGeneration || size != kPrologueSize || order != kLittleEndian || digest != kSha256 ||
        codec != kPayloadCodec || features != 0 || chunkHeader != kChunkHeaderSize) {
        return malformed("the prologue names a generation, codec, or feature this reader does not have");
    }
    return {};
}

ChunkHeader writeChunk(Output& out,
                       ChunkKind kind,
                       std::uint64_t ordinal,
                       std::uint64_t recordCount,
                       const Subject& subject,
                       std::span<const std::byte> payload) {
    const ChunkHeader kHeader{.kind = kind,
                              .ordinal = ordinal,
                              .storedSize = payload.size(),
                              .recordCount = recordCount,
                              .digest = base::sha256(payload),
                              .subject = subject,
                              .offset = out.data().size()};
    out.bytes(kChunkMagic);
    out.u16(kChunkHeaderSize);
    out.u16(static_cast<std::uint16_t>(kind));
    out.u16(kPayloadCodec);
    out.u16(kNoCompression);
    out.u32(0);
    out.u64(ordinal);
    out.u64(payload.size());
    // Uncompressed: what is stored is what is decoded.
    out.u64(payload.size());
    out.u64(recordCount);
    out.bytes(kHeader.digest);
    out.bytes(subject);
    out.bytes(payload);
    return kHeader;
}

result::Result<ChunkHeader> readChunk(std::span<const std::byte> artifact, std::uint64_t offset) {
    if (offset > artifact.size() || artifact.size() - offset < kChunkHeaderSize) {
        return malformed("a chunk header runs past the artifact");
    }
    Input in{artifact.subspan(static_cast<std::size_t>(offset))};
    std::array<std::byte, 4> magic{};
    std::uint16_t headerSize = 0;
    std::uint16_t kind = 0;
    std::uint16_t codec = 0;
    std::uint16_t compression = 0;
    std::uint32_t flags = 0;
    std::uint64_t decoded = 0;
    ChunkHeader header{.offset = offset};
    if (!in.array(magic) || magic != kChunkMagic || !in.u16(headerSize) || !in.u16(kind) || !in.u16(codec) ||
        !in.u16(compression) || !in.u32(flags) || !in.u64(header.ordinal) || !in.u64(header.storedSize) ||
        !in.u64(decoded) || !in.u64(header.recordCount) || !in.array(header.digest) || !in.array(header.subject)) {
        return malformed("a chunk header is short or its magic is wrong");
    }
    if (headerSize != kChunkHeaderSize || !knownKind(kind) || codec != kPayloadCodec || compression != kNoCompression ||
        flags != 0 || decoded != header.storedSize) {
        return malformed("a chunk header names a kind, codec, compression, or flag generation 1 does not have");
    }
    header.kind = static_cast<ChunkKind>(kind);
    std::span<const std::byte> payload;
    if (!in.take(static_cast<std::size_t>(std::min<std::uint64_t>(header.storedSize, in.remaining() + 1)), payload)) {
        return malformed("a chunk's payload runs past the artifact");
    }
    if (base::sha256(payload) != header.digest) {
        return fail(result::ErrorClass::DataLoss, SnapshotError::DigestMismatch, "a chunk's payload digest is wrong");
    }
    return header;
}

void writeFooter(Output& out, const Footer& footer) {
    out.bytes(kFooterMagic);
    out.u16(kFooterSize);
    out.u16(kGeneration);
    out.u32(0);
    out.u32(0);
    out.u64(footer.manifestOffset);
    out.u64(footer.manifestSize);
    out.u64(footer.chunkCount);
    out.u64(footer.preFooterSize);
    out.bytes(footer.digest);
    out.bytes(footer.manifestDigest);
    out.zeros(16);
}

result::Result<Footer> readFooter(std::span<const std::byte> artifact) {
    if (artifact.size() < kPrologueSize + kFooterSize) {
        return fail(
            result::ErrorClass::DataLoss, SnapshotError::Incomplete, "the artifact is too short to be complete");
    }
    const std::size_t kAt = artifact.size() - kFooterSize;
    Input in{artifact.subspan(kAt)};
    std::array<std::byte, 4> magic{};
    std::uint16_t size = 0;
    std::uint16_t generation = 0;
    std::uint32_t flags = 0;
    Footer footer;
    if (!in.array(magic) || magic != kFooterMagic) {
        return fail(result::ErrorClass::DataLoss,
                    SnapshotError::Incomplete,
                    "no completion footer: the artifact was not finished");
    }
    if (!in.u16(size) || !in.u16(generation) || !in.u32(flags) || !in.zeros(4) || !in.u64(footer.manifestOffset) ||
        !in.u64(footer.manifestSize) || !in.u64(footer.chunkCount) || !in.u64(footer.preFooterSize) ||
        !in.array(footer.digest) || !in.array(footer.manifestDigest) || !in.zeros(16)) {
        return malformed("the footer is short or has a reserved byte set");
    }
    if (size != kFooterSize || generation != kGeneration || flags != 0 || footer.preFooterSize != kAt) {
        return malformed("the footer's sizes or generation are wrong");
    }
    if (base::sha256(artifact.first(kAt)) != footer.digest) {
        return fail(result::ErrorClass::DataLoss, SnapshotError::DigestMismatch, "the artifact's digest is wrong");
    }
    return footer;
}

Fingerprint profileFingerprint(const SnapshotLimits& limits) {
    Output words;
    words.u64(limits.maximumEntities);
    words.u64(limits.maximumRows);
    words.u64(limits.maximumRandomStreams);
    words.u64(limits.maximumArtifactBytes);
    words.u64(limits.rowsPerChunk);
    base::Sha256 hasher;
    hasher.update("rawframe.world_snapshot.profile.v1");
    hasher.update(words.data());
    return hasher.finish();
}

Fingerprint toolchainFingerprint() {
    return base::sha256("rawframe.world_snapshot.codec.v1");
}

} // namespace rawframe::world_snapshot::detail
