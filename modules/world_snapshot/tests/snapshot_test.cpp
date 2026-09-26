// World checkpoints: a round trip that restores every value, reference, and
// random stream and captures to the same bytes again; canonical NaNs; row
// groups; refusal of every damaged, foreign, or oversized artifact; and
// hostile artifacts with matching digests restored only as they capture;
// and a capture staged at the safe point and sealed after the World ran on.

#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_snapshot/checkpoint.h"
#include "rawframe/world_snapshot/errors.h"
#include "sample.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using world_snapshot::FieldKind;
using world_snapshot::SnapshotError;
using namespace world_snapshot::testing;

namespace {

bool failedWith(const auto& outcome, SnapshotError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

} // namespace

RAWFRAME_TEST(ACheckpointRestoresEveryValueReferenceAndStream) {
    Sample sample;
    const auto kArtifact = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kArtifact.has_value());
    if (!kArtifact.has_value()) {
        return;
    }
    world::World candidate{sample.schema};
    const auto kFacts = world_snapshot::restore(*kArtifact, projection(), kIdentity, {}, candidate);
    RAWFRAME_EXPECT(kFacts.has_value());
    if (!kFacts.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(kFacts->tick.value == 1234 && kFacts->rate.ticks == 60 && kFacts->rate.seconds == 1);
    RAWFRAME_EXPECT(kFacts->entities == 6 && kFacts->rows == 8 && kFacts->references == 4);
    RAWFRAME_EXPECT(candidate.entityCount() == 6);

    // The restored entities are the originals in slot order.
    std::vector<world::EntityHandle> restored;
    for (const auto& archetype : candidate.archetypes()) {
        restored.insert(restored.end(), archetype->entities().begin(), archetype->entities().end());
    }
    std::sort(restored.begin(), restored.end());
    RAWFRAME_EXPECT(restored.size() == sample.live.size());
    const auto kBody = *sample.schema->key<Body>();
    const auto kLink = *sample.schema->key<Link>();
    for (std::size_t index = 0; index < restored.size() && index < sample.live.size(); ++index) {
        const Body* original = sample.world.get(sample.live[index], kBody);
        const Body* copy = candidate.get(restored[index], kBody);
        RAWFRAME_EXPECT((original == nullptr) == (copy == nullptr));
        if (original != nullptr && copy != nullptr) {
            RAWFRAME_EXPECT(std::bit_cast<std::uint32_t>(original->x) == std::bit_cast<std::uint32_t>(copy->x));
            RAWFRAME_EXPECT(std::signbit(copy->y) && copy->mass == 1e300 && copy->awake == original->awake);
            RAWFRAME_EXPECT(copy->kind == original->kind && copy->score == original->score);
        }
    }
    // References point at the restored entities; the one to a destroyed
    // entity is the null entity.
    const Link* first = candidate.get(restored[0], kLink);
    const Link* sixth = candidate.get(restored[5], kLink);
    const Link* fourth = candidate.get(restored[3], kLink);
    RAWFRAME_EXPECT(first != nullptr && first->target == restored[5] && first->other == restored[0]);
    RAWFRAME_EXPECT(sixth != nullptr && sixth->target == restored[0] && sixth->other.isNull());
    RAWFRAME_EXPECT(fourth != nullptr && fourth->target.isNull() && fourth->other == restored[2]);
    RAWFRAME_EXPECT(candidate.randomStreams() == sample.world.randomStreams());

    // The restored World captures to the same bytes: the artifact is a
    // function of the state, not of how the World came to hold it.
    const auto kAgain = world_snapshot::capture(candidate, projection(), settings());
    RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == *kArtifact);
}

RAWFRAME_TEST(AStagedCaptureSealsAsTheWorldWasWhenStaged) {
    // D227: only staging reads the World; what is sealed after the World has
    // run on is the artifact of the World as it was staged, with its
    // SnapshotDigest.
    Sample sample;
    const auto kBefore = world_snapshot::capture(sample.world, projection(), settings());
    auto staged = world_snapshot::stage(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kBefore.has_value() && staged.has_value() && staged->heldBytes() > 0);
    if (!kBefore.has_value() || !staged.has_value()) {
        return;
    }
    const auto kBody = *sample.schema->key<Body>();
    sample.world.get(sample.live[0], kBody)->score = 99;
    RAWFRAME_EXPECT(sample.world.destroy(sample.live[1]).has_value());
    const auto kSealed = world_snapshot::seal(std::move(*staged));
    RAWFRAME_EXPECT(kSealed.has_value() && kSealed->bytes == *kBefore);
    RAWFRAME_EXPECT(kSealed.has_value() &&
                    kSealed->digest == base::sha256(std::span{kSealed->bytes}.first(kSealed->bytes.size() - 128)));
    const auto kAfter = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kAfter.has_value() && *kAfter != *kBefore);
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::seal(world_snapshot::StagedCheckpoint{}), SnapshotError::InvalidCandidate));
}

RAWFRAME_TEST(NaNsAreWrittenOneWay) {
    Sample sample;
    const auto kBody = *sample.schema->key<Body>();
    Body* body = sample.world.get(sample.live[0], kBody);
    body->x = std::bit_cast<float>(0x7FA0'0001U);
    body->mass = std::bit_cast<double>(0xFFF0'0000'0000'0042ULL);
    const auto kArtifact = world_snapshot::capture(sample.world, projection(), settings());
    world::World candidate{sample.schema};
    RAWFRAME_EXPECT(kArtifact.has_value() &&
                    world_snapshot::restore(*kArtifact, projection(), kIdentity, {}, candidate).has_value());
    std::vector<world::EntityHandle> restored;
    for (const auto& archetype : candidate.archetypes()) {
        restored.insert(restored.end(), archetype->entities().begin(), archetype->entities().end());
    }
    std::sort(restored.begin(), restored.end());
    const Body* copy = restored.empty() ? nullptr : candidate.get(restored[0], kBody);
    RAWFRAME_EXPECT(copy != nullptr && std::bit_cast<std::uint32_t>(copy->x) == 0x7FC0'0000U &&
                    std::bit_cast<std::uint64_t>(copy->mass) == 0x7FF8'0000'0000'0000ULL);
}

RAWFRAME_TEST(RowGroupsFollowTheProfile) {
    Sample sample;
    const world_snapshot::SnapshotLimits kSmall{.rowsPerChunk = 2};
    const auto kGrouped = world_snapshot::capture(sample.world, projection(), settings(kSmall));
    const auto kWhole = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kGrouped.has_value() && kWhole.has_value() && kGrouped->size() > kWhole->size());
    world::World candidate{sample.schema};
    RAWFRAME_EXPECT(world_snapshot::restore(*kGrouped, projection(), kIdentity, kSmall, candidate).has_value());
    // Another profile is another artifact, and says so.
    world::World other{sample.schema};
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::restore(*kGrouped, projection(), kIdentity, {}, other), SnapshotError::Mismatch));
}

RAWFRAME_TEST(ChunksAreHeldToTheProfile) {
    // SPEC-0013's chunk ceilings (D231): a component's rows wider than the
    // chunk allows split into groups that fit; too many chunks are refused
    // on capture, and on restore before anything is decoded.
    Sample sample;
    // A body row is 35 bytes, so two to a chunk of 80; the random streams'
    // chunk still fits.
    const world_snapshot::SnapshotLimits kNarrow{.maximumChunkBytes = 80};
    RAWFRAME_EXPECT(world_snapshot::rowGroup(projection().components[0], kNarrow) == 2);
    const auto kNarrowed = world_snapshot::capture(sample.world, projection(), settings(kNarrow));
    RAWFRAME_EXPECT(kNarrowed.has_value());
    world::World candidate{sample.schema};
    RAWFRAME_EXPECT(kNarrowed.has_value() &&
                    world_snapshot::restore(*kNarrowed, projection(), kIdentity, kNarrow, candidate).has_value());
    const world_snapshot::SnapshotLimits kFew{.maximumChunks = 3};
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::capture(sample.world, projection(), settings(kFew)), SnapshotError::LimitExceeded));
    const auto kWhole = world_snapshot::capture(sample.world, projection(), settings());
    world::World other{sample.schema};
    RAWFRAME_EXPECT(kWhole.has_value() &&
                    failedWith(world_snapshot::restore(*kWhole, projection(), kIdentity, kFew, other),
                               SnapshotError::LimitExceeded));
}

RAWFRAME_TEST(EveryDamagedArtifactIsRefused) {
    Sample sample;
    const auto kArtifact = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kArtifact.has_value());
    if (!kArtifact.has_value()) {
        return;
    }
    // Any one bit wrong anywhere, and any length short of the whole.
    std::size_t accepted = 0;
    for (std::size_t index = 0; index < kArtifact->size(); ++index) {
        std::vector<std::byte> damaged = *kArtifact;
        damaged[index] ^= std::byte{0x10};
        world::World candidate{sample.schema};
        accepted += world_snapshot::restore(damaged, projection(), kIdentity, {}, candidate).has_value() ? 1 : 0;
    }
    for (std::size_t length = 0; length < kArtifact->size(); length += 7) {
        world::World candidate{sample.schema};
        accepted += world_snapshot::restore(std::span{*kArtifact}.first(length), projection(), kIdentity, {}, candidate)
                            .has_value()
                        ? 1
                        : 0;
    }
    RAWFRAME_EXPECT(accepted == 0);

    world::World candidate{sample.schema};
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::restore(
                       std::span{*kArtifact}.first(kArtifact->size() - 1), projection(), kIdentity, {}, candidate),
                   SnapshotError::Incomplete));
    std::vector<std::byte> payload = *kArtifact;
    payload[32 + 96] ^= std::byte{1};
    RAWFRAME_EXPECT(failedWith(world_snapshot::restore(payload, projection(), kIdentity, {}, candidate),
                               SnapshotError::DigestMismatch));
}

RAWFRAME_TEST(HostileArtifactsWithMatchingDigestsRestoreOnlyAsTheyCapture) {
    Sample sample;
    const world_snapshot::SnapshotLimits kSmall{.rowsPerChunk = 2};
    test::Mutations mutations;
    std::size_t accepted = 0;
    std::size_t refused = 0;
    // Whole chunks without mods, and row groups of two with mods recorded.
    const std::array<std::pair<world_snapshot::SnapshotLimits, world_snapshot::CheckpointIdentity>, 2> kRuns = {
        std::pair{world_snapshot::SnapshotLimits{}, kIdentity}, std::pair{kSmall, modded()}};
    for (const auto& [limits, identity] : kRuns) {
        world_snapshot::CaptureSettings captured = settings(limits);
        captured.identity = identity;
        const auto kArtifact = world_snapshot::capture(sample.world, projection(), captured);
        RAWFRAME_EXPECT(kArtifact.has_value());
        if (!kArtifact.has_value()) {
            return;
        }
        // Resealing an undamaged artifact changes nothing.
        RAWFRAME_EXPECT(resealed(*kArtifact, *kArtifact) == *kArtifact);
        for (int round = 0; round < 3000; ++round) {
            // One to four bytes replaced in place, so the layout holds and
            // the digests can be made to match.
            std::vector<std::byte> damaged = *kArtifact;
            const auto kEdits = 1 + mutations.next() % 4;
            for (std::uint64_t edit = 0; edit < kEdits; ++edit) {
                damaged[static_cast<std::size_t>(mutations.next() % (damaged.size() - 128))] =
                    static_cast<std::byte>(mutations.next() & 0xFFU);
            }
            damaged = resealed(std::move(damaged), *kArtifact);
            world::World candidate{sample.schema};
            const auto kRestored = world_snapshot::restore(damaged, projection(), identity, limits, candidate);
            if (!kRestored.has_value()) {
                ++refused;
                continue;
            }
            // What is accepted is read exactly: the World it made captures
            // to the same bytes.
            ++accepted;
            const auto kAgain = world_snapshot::capture(
                candidate,
                projection(),
                world_snapshot::CaptureSettings{
                    .tick = kRestored->tick, .rate = kRestored->rate, .identity = identity, .limits = limits});
            RAWFRAME_EXPECT(kAgain.has_value() && *kAgain == damaged);
        }
    }
    // Most damage is refused by structure alone.
    RAWFRAME_EXPECT(refused > accepted);
}

RAWFRAME_TEST(ForeignArtifactsAndBadCandidatesAreRefused) {
    Sample sample;
    const auto kArtifact = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kArtifact.has_value());
    if (!kArtifact.has_value()) {
        return;
    }
    world::World candidate{sample.schema};
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::restore(*kArtifact, projection(), {}, {}, candidate), SnapshotError::Mismatch));
    auto narrower = projection();
    narrower.components[1].fields.pop_back();
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::restore(*kArtifact, narrower, kIdentity, {}, candidate), SnapshotError::Mismatch));
    // Only an empty World is a candidate.
    RAWFRAME_EXPECT(candidate.create().has_value());
    RAWFRAME_EXPECT(failedWith(world_snapshot::restore(*kArtifact, projection(), kIdentity, {}, candidate),
                               SnapshotError::InvalidCandidate));
    // A projection that does not fit the World is refused before anything.
    auto oversized = projection();
    oversized.components[0].size += 8;
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::capture(sample.world, oversized, settings()), SnapshotError::InvalidCandidate));
    auto overlapping = projection();
    overlapping.components[0].fields.push_back({offsetof(Body, x) + 2, FieldKind::U16});
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::capture(sample.world, overlapping, settings()), SnapshotError::InvalidCandidate));
    // Limits hold both ways.
    RAWFRAME_EXPECT(failedWith(world_snapshot::capture(sample.world, projection(), settings({.maximumEntities = 5})),
                               SnapshotError::LimitExceeded));
    world::World small{sample.schema};
    RAWFRAME_EXPECT(
        failedWith(world_snapshot::restore(*kArtifact, projection(), kIdentity, {.maximumArtifactBytes = 64}, small),
                   SnapshotError::LimitExceeded));
}

RAWFRAME_TEST(ACheckpointKnowsTheModsItRanWith) {
    Sample sample;
    world_snapshot::CaptureSettings captured = settings();
    captured.identity = modded();
    const auto kArtifact = world_snapshot::capture(sample.world, projection(), captured);
    RAWFRAME_EXPECT(kArtifact.has_value());
    if (!kArtifact.has_value()) {
        return;
    }
    world::World same{sample.schema};
    RAWFRAME_EXPECT(world_snapshot::restore(*kArtifact, projection(), modded(), {}, same).has_value());
    // Another mod set, even with the same schema, is its own refusal naming
    // what changed.
    world_snapshot::CheckpointIdentity other = modded();
    other.mods[0].version = "1.1.0";
    other.mods.erase(other.mods.begin() + 1);
    other.mods.push_back({.subject = "fan/timers", .version = "0.1.0"});
    world::World candidate{sample.schema};
    const auto kRefused = world_snapshot::restore(*kArtifact, projection(), other, {}, candidate);
    RAWFRAME_EXPECT(failedWith(kRefused, SnapshotError::ModSetChanged));
    if (!kRefused.has_value()) {
        std::string context;
        for (const result::ContextField& field : kRefused.error().context()) {
            context += std::string{field.key} + "=" + std::string{field.value} + ";";
        }
        RAWFRAME_EXPECT(context == "added=fan/timers@0.1.0;removed=fan/sting@0.2.0;changed=fan/horde@1.0.0>1.1.0;");
    }
    // No mods then, some now; and a checkpoint without mods keeps its bytes.
    world::World unmodded{sample.schema};
    RAWFRAME_EXPECT(failedWith(world_snapshot::restore(*kArtifact, projection(), kIdentity, {}, unmodded),
                               SnapshotError::ModSetChanged));
    const auto kPlain = world_snapshot::capture(sample.world, projection(), settings());
    RAWFRAME_EXPECT(kPlain.has_value() && kPlain->size() < kArtifact->size());
    // Capture records only a set in subject order, each once and bounded.
    for (const std::vector<world_snapshot::CheckpointMod>& kBad :
         {std::vector<world_snapshot::CheckpointMod>{{"fan/sting", "1"}, {"fan/horde", "1"}},
          std::vector<world_snapshot::CheckpointMod>{{"fan/horde", "1"}, {"fan/horde", "2"}},
          std::vector<world_snapshot::CheckpointMod>{{"fan/horde", ""}},
          std::vector<world_snapshot::CheckpointMod>{{std::string(257, 'a'), "1"}}}) {
        world_snapshot::CaptureSettings bad = settings();
        bad.identity.mods = kBad;
        RAWFRAME_EXPECT(
            failedWith(world_snapshot::capture(sample.world, projection(), bad), SnapshotError::InvalidCandidate));
    }
}
