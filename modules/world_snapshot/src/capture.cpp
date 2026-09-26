#include "codec.h"
#include "rawframe/world/archetype.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace rawframe::world_snapshot {

using detail::ChunkHeader;
using detail::ChunkKind;
using detail::Output;

namespace {

/// A projected component resolved against the World being captured.
struct Resolved {
    const SnapshotComponent* component = nullptr;
    schema::ComponentRuntimeId runtime;
};

result::Result<std::vector<Resolved>> resolve(const world::World& world, const SnapshotProjection& projection) {
    if (!projectionValid(projection)) {
        return detail::fail(result::ErrorClass::InvalidArgument,
                            SnapshotError::InvalidCandidate,
                            "the projection has a duplicate component or a field outside its component");
    }
    std::vector<Resolved> resolved;
    for (const SnapshotComponent& component : projection.components) {
        const auto kRuntime = world.registry().find(component.id);
        if (!kRuntime.has_value()) {
            return detail::fail(result::ErrorClass::InvalidArgument,
                                SnapshotError::InvalidCandidate,
                                "the projection names a component the World's registry lacks");
        }
        const schema::ComponentDescriptor& descriptor = world.registry().descriptor(*kRuntime);
        if (!descriptor.plainData || descriptor.size != component.size) {
            return detail::fail(result::ErrorClass::InvalidArgument,
                                SnapshotError::InvalidCandidate,
                                "a projected component is not plain data of its projected size");
        }
        resolved.push_back(Resolved{.component = &component, .runtime = *kRuntime});
    }
    // Chunks follow stable identity, whatever order the projection lists.
    std::sort(resolved.begin(), resolved.end(), [](const Resolved& left, const Resolved& right) {
        return left.component->id < right.component->id;
    });
    return resolved;
}

/// Writes one field of a value: integers as they are, floats with every NaN
/// made the one quiet NaN, and entities as their place in the artifact.
void writeField(Output& out,
                const SnapshotField& field,
                const std::byte* value,
                const std::unordered_map<world::EntityHandle, std::uint64_t>& places,
                std::uint64_t& references) {
    const std::byte* const kAt = value + field.offset;
    switch (field.kind) {
    case FieldKind::I8:
    case FieldKind::U8:
    case FieldKind::Bool:
        out.u8(std::to_integer<std::uint8_t>(*kAt));
        return;
    case FieldKind::I16:
    case FieldKind::U16: {
        std::uint16_t word = 0;
        std::memcpy(&word, kAt, sizeof word);
        out.u16(word);
        return;
    }
    case FieldKind::I32:
    case FieldKind::U32: {
        std::uint32_t word = 0;
        std::memcpy(&word, kAt, sizeof word);
        out.u32(word);
        return;
    }
    case FieldKind::I64:
    case FieldKind::U64: {
        std::uint64_t word = 0;
        std::memcpy(&word, kAt, sizeof word);
        out.u64(word);
        return;
    }
    case FieldKind::F32: {
        float real = 0;
        std::memcpy(&real, kAt, sizeof real);
        out.u32(real != real ? 0x7FC0'0000U : std::bit_cast<std::uint32_t>(real));
        return;
    }
    case FieldKind::F64: {
        double real = 0;
        std::memcpy(&real, kAt, sizeof real);
        out.u64(real != real ? 0x7FF8'0000'0000'0000ULL : std::bit_cast<std::uint64_t>(real));
        return;
    }
    case FieldKind::Entity: {
        world::EntityHandle handle;
        std::memcpy(&handle, kAt, sizeof handle);
        // Null, or an entity that no longer exists, which every door already
        // treats as the null entity: both are written as nought (D29).
        const auto kPlace = handle.isNull() ? places.end() : places.find(handle);
        references += kPlace != places.end() ? 1 : 0;
        out.u64(kPlace != places.end() ? kPlace->second : 0);
        return;
    }
    }
}

} // namespace

struct StagedCheckpoint::State {
    struct Pending {
        ChunkKind kind;
        std::uint64_t records;
        detail::Subject subject;
        std::vector<std::byte> payload;
    };
    std::vector<Pending> chunks;
    detail::Totals totals;
    world::TickIndex tick;
    world::TickRate rate;
    Fingerprint schema{};
    Fingerprint projection{};
    SnapshotLimits limits;
};

StagedCheckpoint::StagedCheckpoint() noexcept = default;
StagedCheckpoint::StagedCheckpoint(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}
StagedCheckpoint::StagedCheckpoint(StagedCheckpoint&&) noexcept = default;
StagedCheckpoint& StagedCheckpoint::operator=(StagedCheckpoint&&) noexcept = default;
StagedCheckpoint::~StagedCheckpoint() = default;

std::size_t StagedCheckpoint::heldBytes() const noexcept {
    return state_ != nullptr ? state_->totals.decodedBytes : 0;
}

result::Result<StagedCheckpoint>
stage(const world::World& world, const SnapshotProjection& projection, const CaptureSettings& settings) {
    const SnapshotLimits& limits = settings.limits;
    if (limits.maximumEntities == 0 || limits.maximumRows == 0 || limits.maximumRandomStreams == 0 ||
        limits.maximumArtifactBytes == 0 || limits.rowsPerChunk == 0) {
        return detail::fail(
            result::ErrorClass::InvalidArgument, SnapshotError::LimitExceeded, "every snapshot limit is required");
    }
    if (!detail::validModSet(settings.identity.mods)) {
        return detail::fail(result::ErrorClass::InvalidArgument,
                            SnapshotError::InvalidCandidate,
                            "the mods are out of subject order, repeated, empty, or past their bounds");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(world, projection));

    // Every live entity, in slot order; its place in the artifact is its
    // position from one.
    std::vector<world::EntityHandle> entities;
    for (const auto& archetype : world.archetypes()) {
        entities.insert(entities.end(), archetype->entities().begin(), archetype->entities().end());
    }
    if (entities.size() > limits.maximumEntities) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the World has more entities than the snapshot profile allows");
    }
    std::sort(entities.begin(), entities.end());
    std::unordered_map<world::EntityHandle, std::uint64_t> places;
    places.reserve(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        places.emplace(entities[index], index + 1);
    }

    auto state = std::make_unique<StagedCheckpoint::State>();
    using Pending = StagedCheckpoint::State::Pending;
    std::vector<Pending>& chunks = state->chunks;
    detail::Totals& totals = state->totals;
    totals.entities = entities.size();

    for (std::size_t first = 0; first < entities.size(); first += limits.rowsPerChunk) {
        const std::size_t kCount = std::min(limits.rowsPerChunk, entities.size() - first);
        Output directory;
        for (std::size_t index = first; index < first + kCount; ++index) {
            directory.u64(index + 1);
        }
        chunks.push_back(Pending{ChunkKind::EntityDirectory, kCount, {}, std::move(directory.data())});
    }

    for (const Resolved& resolved : kResolved) {
        Output rows;
        std::uint64_t count = 0;
        const auto kFlush = [&] {
            if (count != 0) {
                chunks.push_back(Pending{ChunkKind::ComponentRows,
                                         count,
                                         detail::subjectOf(resolved.component->id),
                                         std::move(rows.data())});
                rows = Output{};
                count = 0;
            }
        };
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const auto* value = static_cast<const std::byte*>(world.getErased(entities[index], resolved.runtime));
            if (value == nullptr) {
                continue;
            }
            if (++totals.rows > limits.maximumRows) {
                return detail::fail(result::ErrorClass::ResourceExhausted,
                                    SnapshotError::LimitExceeded,
                                    "the World has more component rows than the snapshot profile allows");
            }
            rows.u64(index + 1);
            for (const SnapshotField& field : resolved.component->fields) {
                writeField(rows, field, value, places, totals.references);
            }
            if (++count == limits.rowsPerChunk) {
                kFlush();
            }
        }
        kFlush();
    }

    const auto& streams = world.randomStreams();
    if (streams.size() > limits.maximumRandomStreams) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the World has more random streams than the snapshot profile allows");
    }
    if (!streams.empty()) {
        Output random;
        for (const auto& [key, stream] : streams) {
            random.u32(static_cast<std::uint32_t>(key.first.size()));
            random.bytes(std::as_bytes(std::span{key.first.data(), key.first.size()}));
            random.u32(static_cast<std::uint32_t>(key.second.size()));
            random.bytes(std::as_bytes(std::span{key.second.data(), key.second.size()}));
            random.u64(stream.stateWord());
            random.u64(stream.incrementWord());
        }
        chunks.push_back(Pending{ChunkKind::RandomStreams, streams.size(), {}, std::move(random.data())});
    }
    if (!settings.identity.mods.empty()) {
        Output mods;
        detail::writeModSet(mods, settings.identity.mods);
        chunks.push_back(Pending{ChunkKind::ModSet, settings.identity.mods.size(), {}, std::move(mods.data())});
    }
    for (const Pending& chunk : chunks) {
        totals.decodedBytes += chunk.payload.size();
    }
    if (totals.decodedBytes > kMaximumStagedBytes) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the staged checkpoint holds more than the snapshot profile allows apart from the World");
    }
    state->tick = settings.tick;
    state->rate = settings.rate;
    state->schema = settings.identity.schema;
    state->projection = projectionFingerprint(projection);
    state->limits = limits;
    return StagedCheckpoint{std::move(state)};
}

result::Result<SealedCheckpoint> seal(StagedCheckpoint staged) {
    const StagedCheckpoint::State* const kState = staged.state();
    if (kState == nullptr) {
        return detail::fail(
            result::ErrorClass::InvalidArgument, SnapshotError::InvalidCandidate, "there is nothing staged to seal");
    }
    const detail::Totals& totals = kState->totals;
    const std::uint64_t kDivisor = std::gcd(std::uint64_t{kState->rate.ticks}, std::uint64_t{kState->rate.seconds});
    const detail::Fingerprints kFingerprints{.schema = kState->schema,
                                             .package = {},
                                             .projection = kState->projection,
                                             .toolchain = detail::toolchainFingerprint(),
                                             .profile = detail::profileFingerprint(kState->limits)};
    Output header;
    detail::writeWorldHeader(header,
                             detail::WorldHeader{.tick = kState->tick.value,
                                                 .rateNumerator = kState->rate.ticks / kDivisor,
                                                 .rateDenominator = kState->rate.seconds / kDivisor,
                                                 .fingerprints = kFingerprints,
                                                 .totals = totals});

    Output out;
    detail::writePrologue(out);
    detail::Manifest manifest{.entries = {}, .fingerprints = kFingerprints, .totals = totals};
    manifest.entries.push_back(detail::writeChunk(out, ChunkKind::WorldHeader, 0, 1, {}, header.data()));
    for (const StagedCheckpoint::State::Pending& chunk : kState->chunks) {
        manifest.entries.push_back(
            detail::writeChunk(out, chunk.kind, manifest.entries.size(), chunk.records, chunk.subject, chunk.payload));
    }
    Output manifestPayload;
    detail::writeManifest(manifestPayload, manifest);
    const ChunkHeader kManifest = detail::writeChunk(
        out, ChunkKind::Manifest, manifest.entries.size(), manifest.entries.size(), {}, manifestPayload.data());
    const std::uint64_t kPreFooter = out.data().size();
    const Fingerprint kDigest = base::sha256(std::span{out.data()}.first(kPreFooter));
    detail::writeFooter(out,
                        detail::Footer{.manifestOffset = kManifest.offset,
                                       .manifestSize = kManifest.storedSize,
                                       .chunkCount = manifest.entries.size() + 1,
                                       .preFooterSize = kPreFooter,
                                       .digest = kDigest,
                                       .manifestDigest = kManifest.digest});
    if (out.data().size() > kState->limits.maximumArtifactBytes) {
        return detail::fail(result::ErrorClass::ResourceExhausted,
                            SnapshotError::LimitExceeded,
                            "the checkpoint is larger than the snapshot profile allows");
    }
    return SealedCheckpoint{.bytes = std::move(out.data()), .digest = kDigest};
}

result::Result<std::vector<std::byte>>
capture(const world::World& world, const SnapshotProjection& projection, const CaptureSettings& settings) {
    RAWFRAME_TRY_ASSIGN(StagedCheckpoint staged, stage(world, projection, settings));
    RAWFRAME_TRY_ASSIGN(SealedCheckpoint sealed, seal(std::move(staged)));
    return std::move(sealed.bytes);
}

} // namespace rawframe::world_snapshot
