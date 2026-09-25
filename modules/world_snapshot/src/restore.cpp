#include "codec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>

namespace rawframe::world_snapshot {

using detail::ChunkHeader;
using detail::ChunkKind;
using detail::Input;

namespace {

std::unexpected<result::Error> mismatch(std::string_view why) {
    return detail::fail(result::ErrorClass::FailedPrecondition, SnapshotError::Mismatch, why);
}

std::unexpected<result::Error> badReference(std::string_view why) {
    return detail::fail(result::ErrorClass::DataLoss, SnapshotError::BadReference, why);
}

/// One component's rows, decoded and waiting for the candidate: values in
/// memory layout with entity fields holding places, not handles yet.
struct StagedComponent {
    const SnapshotComponent* component = nullptr;
    std::vector<std::uint64_t> places;
    std::vector<std::byte> values;
};

struct StagedStream {
    std::string owner;
    std::string name;
    world::Pcg32 state{0, 0};
};

/// Reads one field into `value`, refusing what capture never writes: a NaN
/// other than the quiet one, a truth other than nought or one, an entity
/// place past the last entity.
result::Status
readField(Input& in, const SnapshotField& field, std::byte* value, std::uint64_t entities, std::uint64_t& references) {
    std::byte* const kAt = value + field.offset;
    const auto kStore = [kAt](auto word) {
        std::memcpy(kAt, &word, sizeof word);
    };
    switch (field.kind) {
    case FieldKind::I8:
    case FieldKind::U8:
    case FieldKind::Bool: {
        std::uint8_t byte = 0;
        if (!in.u8(byte) || (field.kind == FieldKind::Bool && byte > 1)) {
            return detail::malformed("a row is short, or a truth is neither nought nor one");
        }
        kStore(byte);
        return {};
    }
    case FieldKind::I16:
    case FieldKind::U16: {
        std::uint16_t word = 0;
        if (!in.u16(word)) {
            return detail::malformed("a row is short");
        }
        kStore(word);
        return {};
    }
    case FieldKind::I32:
    case FieldKind::U32:
    case FieldKind::F32: {
        std::uint32_t word = 0;
        if (!in.u32(word)) {
            return detail::malformed("a row is short");
        }
        const bool kNan = (word & 0x7F80'0000U) == 0x7F80'0000U && (word & 0x007F'FFFFU) != 0;
        if (field.kind == FieldKind::F32 && kNan && word != 0x7FC0'0000U) {
            return detail::malformed("a float is a NaN other than the canonical quiet one");
        }
        kStore(word);
        return {};
    }
    case FieldKind::I64:
    case FieldKind::U64:
    case FieldKind::F64: {
        std::uint64_t word = 0;
        if (!in.u64(word)) {
            return detail::malformed("a row is short");
        }
        const bool kNan =
            (word & 0x7FF0'0000'0000'0000ULL) == 0x7FF0'0000'0000'0000ULL && (word & 0x000F'FFFF'FFFF'FFFFULL) != 0;
        if (field.kind == FieldKind::F64 && kNan && word != 0x7FF8'0000'0000'0000ULL) {
            return detail::malformed("a double is a NaN other than the canonical quiet one");
        }
        kStore(word);
        return {};
    }
    case FieldKind::Entity: {
        std::uint64_t place = 0;
        if (!in.u64(place)) {
            return detail::malformed("a row is short");
        }
        if (place > entities) {
            return badReference("an entity field names a place past the last entity");
        }
        references += place != 0 ? 1 : 0;
        kStore(place);
        return {};
    }
    }
    return detail::malformed("a field kind generation 1 does not have");
}

/// Every chunk from the prologue to the footer, in order, each checked
/// against its digest, with the manifest checked against them all.
result::Result<std::vector<ChunkHeader>>
walkChunks(std::span<const std::byte> artifact, const detail::Footer& footer, detail::Manifest& manifest) {
    std::vector<ChunkHeader> chunks;
    std::uint64_t offset = detail::kPrologueSize;
    while (offset < footer.preFooterSize) {
        RAWFRAME_TRY_ASSIGN(const ChunkHeader kChunk, detail::readChunk(artifact.first(footer.preFooterSize), offset));
        if (kChunk.ordinal != chunks.size()) {
            return detail::malformed("chunk ordinals are not contiguous from nought");
        }
        chunks.push_back(kChunk);
        offset += detail::kChunkHeaderSize + kChunk.storedSize;
    }
    if (offset != footer.preFooterSize || chunks.size() < 2 || chunks.size() != footer.chunkCount) {
        return detail::malformed("the chunks do not end exactly at the footer, or their count is wrong");
    }
    const ChunkHeader& kLast = chunks.back();
    if (chunks.front().kind != ChunkKind::WorldHeader || chunks.front().recordCount != 1 ||
        kLast.kind != ChunkKind::Manifest || kLast.offset != footer.manifestOffset ||
        kLast.storedSize != footer.manifestSize || kLast.digest != footer.manifestDigest ||
        kLast.recordCount != chunks.size() - 1) {
        return detail::malformed("the first chunk is not the world header or the last is not the manifest");
    }
    // Kinds in file order: header, directory, rows, streams, manifest; rows
    // of one component together and components in stable order.
    for (std::size_t index = 1; index + 1 < chunks.size(); ++index) {
        const ChunkHeader& kChunk = chunks[index];
        const ChunkHeader& kBefore = chunks[index - 1];
        const bool kSubjectless = kChunk.kind != ChunkKind::ComponentRows;
        if (kChunk.kind == ChunkKind::WorldHeader || kChunk.kind == ChunkKind::Manifest || kChunk.kind < kBefore.kind ||
            (kSubjectless && kChunk.subject != detail::Subject{}) ||
            (kChunk.kind == ChunkKind::RandomStreams && kBefore.kind == ChunkKind::RandomStreams) ||
            (kChunk.kind == ChunkKind::ComponentRows && kBefore.kind == ChunkKind::ComponentRows &&
             kChunk.subject < kBefore.subject)) {
            return detail::malformed("the chunks are not in generation 1's order");
        }
    }
    const std::span<const std::byte> kManifestBytes = artifact.subspan(
        static_cast<std::size_t>(kLast.offset + detail::kChunkHeaderSize), static_cast<std::size_t>(kLast.storedSize));
    RAWFRAME_TRY_ASSIGN(manifest, detail::readManifest(kManifestBytes, kLast.recordCount));
    for (std::size_t index = 0; index + 1 < chunks.size(); ++index) {
        const ChunkHeader& kEntry = manifest.entries[index];
        const ChunkHeader& kChunk = chunks[index];
        if (kEntry.ordinal != kChunk.ordinal || kEntry.kind != kChunk.kind || kEntry.subject != kChunk.subject ||
            kEntry.offset != kChunk.offset || kEntry.storedSize != kChunk.storedSize ||
            kEntry.recordCount != kChunk.recordCount || kEntry.digest != kChunk.digest) {
            return detail::malformed("the manifest disagrees with a chunk header");
        }
    }
    return chunks;
}

std::span<const std::byte> payloadOf(std::span<const std::byte> artifact, const ChunkHeader& chunk) noexcept {
    return artifact.subspan(static_cast<std::size_t>(chunk.offset + detail::kChunkHeaderSize),
                            static_cast<std::size_t>(chunk.storedSize));
}

} // namespace

result::Result<CheckpointFacts> restore(std::span<const std::byte> artifact,
                                        const SnapshotProjection& projection,
                                        const CheckpointIdentity& identity,
                                        const SnapshotLimits& limits,
                                        world::World& candidate) {
    if (artifact.size() > limits.maximumArtifactBytes) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the artifact is larger than the snapshot profile allows");
    }
    if (!projectionValid(projection)) {
        return detail::fail(result::ErrorClass::InvalidArgument,
                            SnapshotError::InvalidCandidate,
                            "the projection has a duplicate component or a field outside its component");
    }
    // Preflight: the container, every digest, and what it is of.
    RAWFRAME_TRY(detail::readPrologue(artifact));
    RAWFRAME_TRY_ASSIGN(const detail::Footer kFooter, detail::readFooter(artifact));
    detail::Manifest manifest;
    RAWFRAME_TRY_ASSIGN(const std::vector<ChunkHeader> kChunks, walkChunks(artifact, kFooter, manifest));
    RAWFRAME_TRY_ASSIGN(const detail::WorldHeader kHeader, detail::readWorldHeader(payloadOf(artifact, kChunks[0])));
    const detail::Fingerprints kExpected{.schema = identity.schema,
                                         .package = {},
                                         .projection = projectionFingerprint(projection),
                                         .toolchain = detail::toolchainFingerprint(),
                                         .profile = detail::profileFingerprint(limits)};
    if (kHeader.fingerprints != manifest.fingerprints || kHeader.totals != manifest.totals) {
        return detail::malformed("the manifest and the world header disagree");
    }
    if (kHeader.fingerprints.schema != kExpected.schema) {
        return mismatch("the checkpoint is of another game or schema");
    }
    if (kHeader.fingerprints != kExpected) {
        return mismatch("the checkpoint was written with another projection, codec, or snapshot profile");
    }
    const detail::Totals& kTotals = kHeader.totals;
    if (kTotals.entities > limits.maximumEntities || kTotals.rows > limits.maximumRows) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the checkpoint holds more than the snapshot profile allows");
    }
    if (kHeader.rateNumerator == 0 || kHeader.rateDenominator == 0 ||
        std::gcd(kHeader.rateNumerator, kHeader.rateDenominator) != 1 || kHeader.rateNumerator > UINT32_MAX ||
        kHeader.rateDenominator > UINT32_MAX) {
        return detail::malformed("the tick rate is not a reduced nonzero fraction");
    }

    // Decode everything into cold staging; the candidate is not touched yet.
    std::uint64_t directory = 0;
    std::uint64_t rows = 0;
    std::uint64_t references = 0;
    std::uint64_t decoded = 0;
    std::vector<StagedComponent> staged;
    std::vector<StagedStream> streams;
    for (std::size_t index = 1; index + 1 < kChunks.size(); ++index) {
        const ChunkHeader& kChunk = kChunks[index];
        const std::span<const std::byte> kPayload = payloadOf(artifact, kChunk);
        decoded += kPayload.size();
        Input in{kPayload};
        // Row groups are the profile's: every chunk of a run but the last is
        // full, so one World has one artifact.
        const ChunkHeader& kBefore = kChunks[index - 1];
        const bool kGrouped = kChunk.kind != ChunkKind::RandomStreams;
        if (kChunk.recordCount == 0 || (kGrouped && kChunk.recordCount > limits.rowsPerChunk) ||
            (kGrouped && kBefore.kind == kChunk.kind && kBefore.subject == kChunk.subject &&
             kBefore.recordCount != limits.rowsPerChunk)) {
            return detail::malformed("a chunk is empty, or not the profile's row group");
        }
        switch (kChunk.kind) {
        case ChunkKind::EntityDirectory:
            for (std::uint64_t record = 0; record < kChunk.recordCount; ++record) {
                std::uint64_t place = 0;
                if (!in.u64(place) || place != ++directory) {
                    return detail::malformed("the entity directory is not the places from one in order");
                }
            }
            break;
        case ChunkKind::ComponentRows: {
            if (staged.empty() || detail::subjectOf(staged.back().component->id) != kChunk.subject) {
                const auto kComponent = std::find_if(
                    projection.components.begin(), projection.components.end(), [&](const SnapshotComponent& each) {
                        return detail::subjectOf(each.id) == kChunk.subject;
                    });
                if (kComponent == projection.components.end()) {
                    return mismatch("the checkpoint holds a component the projection does not");
                }
                staged.push_back(StagedComponent{.component = &*kComponent, .places = {}, .values = {}});
            }
            StagedComponent& component = staged.back();
            const std::size_t kSize = component.component->size;
            for (std::uint64_t record = 0; record < kChunk.recordCount; ++record) {
                std::uint64_t place = 0;
                if (!in.u64(place) || place == 0 || place > kTotals.entities ||
                    (!component.places.empty() && place <= component.places.back())) {
                    return detail::malformed("component rows are not ordered by place, or name no entity");
                }
                component.places.push_back(place);
                component.values.resize(component.values.size() + kSize);
                std::byte* const kValue = component.values.data() + (component.values.size() - kSize);
                for (const SnapshotField& field : component.component->fields) {
                    RAWFRAME_TRY(readField(in, field, kValue, kTotals.entities, references));
                }
            }
            rows += kChunk.recordCount;
            break;
        }
        case ChunkKind::RandomStreams:
            if (kChunk.recordCount > limits.maximumRandomStreams) {
                return detail::fail(result::ErrorClass::ResourceExhausted,
                                    SnapshotError::LimitExceeded,
                                    "the checkpoint holds more random streams than the profile allows");
            }
            for (std::uint64_t record = 0; record < kChunk.recordCount; ++record) {
                StagedStream stream;
                std::uint32_t length = 0;
                std::span<const std::byte> text;
                std::uint64_t state = 0;
                std::uint64_t increment = 0;
                if (!in.u32(length) || !in.take(length, text)) {
                    return detail::malformed("a random stream's owner is short");
                }
                stream.owner.assign(reinterpret_cast<const char*>(text.data()), text.size());
                if (!in.u32(length) || !in.take(length, text) || !in.u64(state) || !in.u64(increment)) {
                    return detail::malformed("a random stream is short");
                }
                stream.name.assign(reinterpret_cast<const char*>(text.data()), text.size());
                if ((increment & 1U) == 0 ||
                    (!streams.empty() &&
                     std::pair{streams.back().owner, streams.back().name} >= std::pair{stream.owner, stream.name})) {
                    return detail::malformed("random streams are out of order, repeated, or have an even increment");
                }
                stream.state = world::Pcg32::fromWords(state, increment);
                streams.push_back(std::move(stream));
            }
            break;
        case ChunkKind::WorldHeader:
        case ChunkKind::Manifest:
            break;
        }
        if (in.remaining() != 0) {
            return detail::malformed("a chunk's payload is longer than its records");
        }
    }
    if (directory != kTotals.entities || rows != kTotals.rows || references != kTotals.references ||
        decoded != kTotals.decodedBytes) {
        return detail::malformed("the totals in the header are not what the chunks hold");
    }

    // The candidate: empty, and with every projected component as projected.
    if (candidate.entityCount() != 0) {
        return detail::fail(
            result::ErrorClass::InvalidArgument, SnapshotError::InvalidCandidate, "a restore needs an empty World");
    }
    std::vector<schema::ComponentRuntimeId> runtimes;
    for (const StagedComponent& component : staged) {
        const auto kRuntime = candidate.registry().find(component.component->id);
        if (!kRuntime.has_value() || !candidate.registry().descriptor(*kRuntime).plainData ||
            candidate.registry().descriptor(*kRuntime).size != component.component->size) {
            return detail::fail(result::ErrorClass::InvalidArgument,
                                SnapshotError::InvalidCandidate,
                                "the candidate's registry does not have a projected component as projected");
        }
        runtimes.push_back(*kRuntime);
    }

    // Two passes: every entity first, so a reference can name any of them,
    // then every value with its references resolved.
    std::vector<world::EntityHandle> handles;
    handles.reserve(static_cast<std::size_t>(kTotals.entities));
    for (std::uint64_t place = 0; place < kTotals.entities; ++place) {
        RAWFRAME_TRY_ASSIGN(const world::EntityHandle kEntity, candidate.create());
        handles.push_back(kEntity);
    }
    for (std::size_t index = 0; index < staged.size(); ++index) {
        StagedComponent& component = staged[index];
        const std::size_t kSize = component.component->size;
        for (std::size_t row = 0; row < component.places.size(); ++row) {
            std::byte* const kValue = component.values.data() + (row * kSize);
            for (const SnapshotField& field : component.component->fields) {
                if (field.kind != FieldKind::Entity) {
                    continue;
                }
                std::uint64_t place = 0;
                std::memcpy(&place, kValue + field.offset, sizeof place);
                const world::EntityHandle kHandle =
                    place == 0 ? world::EntityHandle{} : handles[static_cast<std::size_t>(place - 1)];
                std::memcpy(kValue + field.offset, &kHandle, sizeof kHandle);
            }
            RAWFRAME_TRY(candidate.insertErased(
                handles[static_cast<std::size_t>(component.places[row] - 1)], runtimes[index], kValue));
        }
    }
    for (StagedStream& stream : streams) {
        candidate.restoreRandomStream(std::move(stream.owner), std::move(stream.name), stream.state);
    }
    return CheckpointFacts{.tick = world::TickIndex{kHeader.tick},
                           .rate = world::TickRate{.ticks = static_cast<std::uint32_t>(kHeader.rateNumerator),
                                                   .seconds = static_cast<std::uint32_t>(kHeader.rateDenominator)},
                           .entities = static_cast<std::size_t>(kTotals.entities),
                           .rows = static_cast<std::size_t>(kTotals.rows),
                           .references = static_cast<std::size_t>(kTotals.references),
                           .digest = kFooter.digest};
}

} // namespace rawframe::world_snapshot
