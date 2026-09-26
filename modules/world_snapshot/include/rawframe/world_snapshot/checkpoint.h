#pragma once

// World checkpoints (SPEC-0011): a complete, canonical, integrity-checked
// artifact of one committed World state, and the restore that rebuilds it
// into a candidate World off to the side, which its owner then publishes in
// one exchange. Capture and restore take and give bytes; where they are kept
// is their caller's business.
//
// The container is SPEC-0011 generation 1: a prologue, a world header, the
// entity directory, component rows by type in row groups, the World's random
// streams, the mods it ran with when there are any (D197), a manifest, and a
// completion footer, with SHA-256 over every chunk and the whole. What
// differs from the specification, and why, is D29.

#include "rawframe/base/sha256.h"
#include "rawframe/result/result.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"
#include "rawframe/world_snapshot/projection.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_snapshot {

using Fingerprint = base::Sha256Digest;

/// Every bound a capture or restore keeps. None may be zero.
struct SnapshotLimits {
    std::size_t maximumEntities = std::size_t{1} << 20U;
    std::size_t maximumRows = std::size_t{1} << 22U;
    std::size_t maximumRandomStreams = 4096;
    std::size_t maximumArtifactBytes = std::size_t{1} << 30U;
    /// Rows one component chunk holds: the deterministic row group.
    std::size_t rowsPerChunk = 4096;
};

/// A mod a World ran with (SPEC-0042): its subject and exact version.
struct CheckpointMod {
    std::string subject;
    std::string version;

    friend bool operator==(const CheckpointMod&, const CheckpointMod&) = default;
};

/// The most mods a checkpoint records, and the longest subject or version.
inline constexpr std::size_t kMaximumCheckpointMods = 256;
inline constexpr std::size_t kMaximumCheckpointModText = 256;

/// What an artifact is of, besides its World: the game's schema identity,
/// which a restoring process must have too, and the mods it ran with, in
/// subject order, each subject once.
struct CheckpointIdentity {
    Fingerprint schema{};
    std::vector<CheckpointMod> mods;
};

/// SPEC-0042's mod-set change between the mods an artifact recorded and the
/// mods loading it, each list in subject order.
struct ModSetChange {
    struct Changed {
        std::string subject;
        std::string from;
        std::string to;
    };
    std::vector<CheckpointMod> added;
    std::vector<CheckpointMod> removed;
    std::vector<Changed> changed;

    [[nodiscard]] bool empty() const noexcept {
        return added.empty() && removed.empty() && changed.empty();
    }
};

/// What changed from `recorded` to `loading`, both in subject order.
[[nodiscard]] ModSetChange modSetChange(std::span<const CheckpointMod> recorded,
                                        std::span<const CheckpointMod> loading);

struct CaptureSettings {
    /// The committed tick the World is at: the next tick to run.
    world::TickIndex tick;
    world::TickRate rate;
    CheckpointIdentity identity;
    SnapshotLimits limits;
};

/// SPEC-0013's pinned additional memory: the most a staged capture holds
/// apart from its World (D227).
inline constexpr std::size_t kMaximumStagedBytes = std::size_t{128} << 20U;

/// A sealed artifact and its SnapshotDigest, the SHA-256 of everything
/// before its footer.
struct SealedCheckpoint {
    std::vector<std::byte> bytes;
    Fingerprint digest{};
};

/// What a capture read from a World at its safe point: everything the
/// artifact will hold, and nothing of the World, which may run on while it
/// is sealed elsewhere (D227). Move-only; empty when default-made.
class StagedCheckpoint {
public:
    struct State;

    StagedCheckpoint() noexcept;
    explicit StagedCheckpoint(std::unique_ptr<State> state) noexcept;
    StagedCheckpoint(StagedCheckpoint&&) noexcept;
    StagedCheckpoint& operator=(StagedCheckpoint&&) noexcept;
    ~StagedCheckpoint();

    /// The bytes it holds, at most kMaximumStagedBytes.
    [[nodiscard]] std::size_t heldBytes() const noexcept;
    [[nodiscard]] bool empty() const noexcept {
        return state_ == nullptr;
    }
    /// For `seal`.
    [[nodiscard]] State* state() const noexcept {
        return state_.get();
    }

private:
    std::unique_ptr<State> state_;
};

/// Everything an artifact says about itself, from a successful restore.
struct CheckpointFacts {
    world::TickIndex tick;
    world::TickRate rate;
    std::size_t entities = 0;
    std::size_t rows = 0;
    std::size_t references = 0;
    Fingerprint digest{};
};

/// The artifact of `world` as `projection` selects it. The World is only
/// read. Refuses (`limit_exceeded`) a World over the limits and
/// (`invalid_argument`) a projection naming a component the World's registry
/// lacks or that is not plain data of the projected size, or mods out of
/// subject order, repeated, empty, or past their bounds.
[[nodiscard]] result::Result<std::vector<std::byte>>
capture(const world::World& world, const SnapshotProjection& projection, const CaptureSettings& settings);

/// The part of `capture` that reads the World, for its safe point: the same
/// checks, and the staged artifact past kMaximumStagedBytes refused
/// (`limit_exceeded`). Only this needs the World held (D227).
[[nodiscard]] result::Result<StagedCheckpoint>
stage(const world::World& world, const SnapshotProjection& projection, const CaptureSettings& settings);

/// The rest, on any thread: the chunks written with their digests, the
/// manifest, and the footer. `capture` is `seal(stage(...))`. Refuses
/// (`invalid_argument`) an empty staging and (`limit_exceeded`) an artifact
/// past its limit.
[[nodiscard]] result::Result<SealedCheckpoint> seal(StagedCheckpoint staged);

/// Rebuilds `artifact` into `candidate`, which must be empty and built from
/// the registry the projection names. Everything is checked before the
/// candidate is touched: container grammar, every digest, the mods it
/// recorded against `identity`'s (`mod_set_changed`, with the change as
/// context, before anything else of the identity), the identity and
/// projection fingerprints, limits, and every reference. On failure the
/// candidate may hold part of the state and must be discarded; the World in
/// use was never involved.
[[nodiscard]] result::Result<CheckpointFacts> restore(std::span<const std::byte> artifact,
                                                      const SnapshotProjection& projection,
                                                      const CheckpointIdentity& identity,
                                                      const SnapshotLimits& limits,
                                                      world::World& candidate);

} // namespace rawframe::world_snapshot
