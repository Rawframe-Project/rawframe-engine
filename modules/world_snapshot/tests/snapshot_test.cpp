// World checkpoints: a round trip that restores every value, reference, and
// random stream and captures to the same bytes again; canonical NaNs; row
// groups; and refusal of every damaged, foreign, or oversized artifact.

#include "rawframe/test/test.h"
#include "rawframe/world_snapshot/checkpoint.h"
#include "rawframe/world_snapshot/errors.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
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
