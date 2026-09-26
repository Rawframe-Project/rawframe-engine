// World checkpoints: a round trip that restores every value, reference, and
// random stream and captures to the same bytes again; canonical NaNs; row
// groups; refusal of every damaged, foreign, or oversized artifact; and
// hostile artifacts with matching digests restored only as they capture.

#include "rawframe/base/sha256.h"
#include "rawframe/test/mutations.h"
#include "rawframe/test/test.h"
#include "rawframe/world_snapshot/checkpoint.h"
#include "rawframe/world_snapshot/errors.h"

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

namespace {

struct Body {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("7d0c5b9e-3f1a-4c62-9e8b-2a4f6d1c0e37");
    static constexpr std::string_view kComponentName = "test.body";
    float x = 0;
    float y = 0;
    double mass = 0;
    bool awake = false;
    std::uint16_t kind = 0;
    std::int64_t score = 0;
};

struct Link {
    static constexpr schema::ComponentTypeId kComponentTypeId =
        schema::ComponentTypeId::fromText("b3e81f24-6a9d-4d05-8c17-5f2e0a9b4c68");
    static constexpr std::string_view kComponentName = "test.link";
    world::EntityHandle target;
    world::EntityHandle other;
    std::uint8_t strength = 0;
};

world_snapshot::SnapshotProjection projection() {
    return world_snapshot::SnapshotProjection{.components = {
                                                  {.id = Body::kComponentTypeId,
                                                   .size = sizeof(Body),
                                                   .fields = {{offsetof(Body, x), FieldKind::F32},
                                                              {offsetof(Body, y), FieldKind::F32},
                                                              {offsetof(Body, mass), FieldKind::F64},
                                                              {offsetof(Body, awake), FieldKind::Bool},
                                                              {offsetof(Body, kind), FieldKind::U16},
                                                              {offsetof(Body, score), FieldKind::I64}}},
                                                  {.id = Link::kComponentTypeId,
                                                   .size = sizeof(Link),
                                                   .fields = {{offsetof(Link, target), FieldKind::Entity},
                                                              {offsetof(Link, other), FieldKind::Entity},
                                                              {offsetof(Link, strength), FieldKind::U8}}},
                                              }};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body>().add<Link>();
    return *builder.freeze();
}

constexpr world_snapshot::CheckpointIdentity kIdentity{.schema = {std::byte{7}}};

/// The same game with two mods.
world_snapshot::CheckpointIdentity modded() {
    return world_snapshot::CheckpointIdentity{
        .schema = kIdentity.schema,
        .mods = {{.subject = "fan/horde", .version = "1.0.0"}, {.subject = "fan/sting", .version = "0.2.0"}}};
}

world_snapshot::CaptureSettings settings(world_snapshot::SnapshotLimits limits = {}) {
    return world_snapshot::CaptureSettings{.tick = world::TickIndex{1234},
                                           .rate = world::TickRate{.ticks = 120, .seconds = 2},
                                           .identity = kIdentity,
                                           .limits = limits};
}

/// A World with holes in its slots, references both ways and to itself, a
/// reference to an entity that is gone, and a random stream drawn from.
struct Sample {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::vector<world::EntityHandle> live;

    Sample() {
        const auto kBody = *schema->key<Body>();
        const auto kLink = *schema->key<Link>();
        std::vector<world::EntityHandle> made;
        for (int index = 0; index < 8; ++index) {
            made.push_back(*world.create());
        }
        RAWFRAME_EXPECT(world.destroy(made[1]).has_value());
        RAWFRAME_EXPECT(world.destroy(made[4]).has_value());
        for (const std::size_t kIndex : {0U, 2U, 3U, 5U, 6U, 7U}) {
            live.push_back(made[kIndex]);
        }
        for (std::size_t index = 0; index < live.size(); ++index) {
            if (index != 3) {
                RAWFRAME_EXPECT(world
                                    .insert(live[index],
                                            kBody,
                                            Body{.x = static_cast<float>(index) * 1.5F,
                                                 .y = -0.0F,
                                                 .mass = 1e300,
                                                 .awake = index % 2 == 0,
                                                 .kind = static_cast<std::uint16_t>(65000 + index),
                                                 .score = -static_cast<std::int64_t>(index) * 1'000'000'000'000})
                                    .has_value());
            }
        }
        RAWFRAME_EXPECT(
            world.insert(live[0], kLink, Link{.target = live[5], .other = live[0], .strength = 9}).has_value());
        RAWFRAME_EXPECT(
            world.insert(live[5], kLink, Link{.target = live[0], .other = made[4], .strength = 1}).has_value());
        RAWFRAME_EXPECT(world.insert(live[3], kLink, Link{.target = {}, .other = live[2], .strength = 0}).has_value());
        auto& stream = world.randomStream("test.owner", "dice");
        static_cast<void>(stream.nextU32());
        static_cast<void>(world.randomStream("test.owner", "coins").nextU64());
    }
};

bool failedWith(const auto& outcome, SnapshotError error) {
    return !outcome.has_value() && outcome.error().code() == code(error);
}

std::uint64_t u64At(std::span<const std::byte> bytes, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[at + index]) << (8U * index);
    }
    return value;
}

void digestAt(std::vector<std::byte>& bytes, std::size_t at, std::span<const std::byte> of) {
    const auto kDigest = base::sha256(of);
    std::ranges::copy(kDigest, bytes.begin() + static_cast<std::ptrdiff_t>(at));
}

/// `damaged` with every digest made to match again, its chunks where they
/// lie in `layout` (SPEC-0011's generation 1): what a hostile writer hands
/// over, which only the reader's structure can refuse. A digest is not a
/// signature.
std::vector<std::byte> resealed(std::vector<std::byte> damaged, std::span<const std::byte> layout) {
    constexpr std::size_t kHeader = 96;
    constexpr std::size_t kHeaderDigest = 48;
    constexpr std::size_t kEntry = 104;
    constexpr std::size_t kEntryDigest = 72;
    const std::size_t kFooter = layout.size() - 128;
    std::vector<std::size_t> chunks;
    for (std::size_t at = 32; at < kFooter; at += kHeader + static_cast<std::size_t>(u64At(layout, at + 24))) {
        chunks.push_back(at);
    }
    const std::size_t kManifest = chunks.back() + kHeader;
    const auto kPayload = [&damaged, &layout](std::size_t chunk) {
        return std::span<const std::byte>{damaged}.subspan(chunk + kHeader,
                                                           static_cast<std::size_t>(u64At(layout, chunk + 24)));
    };
    for (std::size_t index = 0; index + 1 < chunks.size(); ++index) {
        digestAt(damaged, chunks[index] + kHeaderDigest, kPayload(chunks[index]));
        digestAt(damaged, kManifest + index * kEntry + kEntryDigest, kPayload(chunks[index]));
    }
    digestAt(damaged, chunks.back() + kHeaderDigest, kPayload(chunks.back()));
    digestAt(damaged, kFooter + 80, kPayload(chunks.back()));
    digestAt(damaged, kFooter + 48, std::span<const std::byte>{damaged}.first(kFooter));
    return damaged;
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
