#pragma once

// The payloads inside the container: what the world header, entity
// directory, component rows, random streams, and manifest hold, shared by
// capture and restore so both read one description of the bytes.

#include "container.h"

#include <cstdint>
#include <map>
#include <string>

namespace rawframe::world_snapshot::detail {

/// A component's identity as a chunk's subject: its 128 bits, most
/// significant first, as a UUID is written.
[[nodiscard]] Subject subjectOf(schema::ComponentTypeId id) noexcept;

/// The world header, and the totals the manifest repeats.
struct Totals {
    std::uint64_t entities = 0;
    std::uint64_t rows = 0;
    std::uint64_t references = 0;
    std::uint64_t decodedBytes = 0;

    friend bool operator==(const Totals&, const Totals&) noexcept = default;
};

struct Fingerprints {
    Fingerprint schema{};
    Fingerprint package{};
    Fingerprint projection{};
    Fingerprint toolchain{};
    Fingerprint profile{};

    friend bool operator==(const Fingerprints&, const Fingerprints&) noexcept = default;
};

struct WorldHeader {
    std::uint64_t tick = 0;
    std::uint64_t rateNumerator = 0;
    std::uint64_t rateDenominator = 0;
    Fingerprints fingerprints;
    Totals totals;
};

void writeWorldHeader(Output& out, const WorldHeader& header);
[[nodiscard]] result::Result<WorldHeader> readWorldHeader(std::span<const std::byte> payload);

/// The manifest: one entry per preceding chunk, then the fingerprints and
/// totals the world header holds, for a reader to check before anything else.
struct Manifest {
    std::vector<ChunkHeader> entries;
    Fingerprints fingerprints;
    Totals totals;
};

/// A mod set's payload: each mod's subject and version, lengths first.
/// Reading refuses mods out of subject order, repeated, empty, or past their
/// bounds, which writing never makes.
void writeModSet(Output& out, std::span<const CheckpointMod> mods);
[[nodiscard]] result::Result<std::vector<CheckpointMod>> readModSet(std::span<const std::byte> payload,
                                                                    std::uint64_t records);
/// Whether `mods` is a set a checkpoint may record.
[[nodiscard]] bool validModSet(std::span<const CheckpointMod> mods) noexcept;

/// A queue's payload: its declaration (name, value bytes, ticks it keeps a
/// message, messages it holds), each reader by identity with its cursor in
/// identity order, the place the next message takes, then each message
/// held: its tick, its place, and its value. Reading refuses what the
/// World would not take, which writing never makes.
void writeQueue(Output& out, const world::MessageQueue& queue);
struct ReadQueue {
    world::QueueDeclaration declaration;
    std::vector<world::QueuedMessage> messages;
    std::uint64_t nextSequence = 0;
    std::map<std::string, std::uint64_t, std::less<>> cursors;
};
[[nodiscard]] result::Result<ReadQueue> readQueue(std::span<const std::byte> payload);

void writeManifest(Output& out, const Manifest& manifest);
[[nodiscard]] result::Result<Manifest> readManifest(std::span<const std::byte> payload, std::uint64_t entries);

} // namespace rawframe::world_snapshot::detail
