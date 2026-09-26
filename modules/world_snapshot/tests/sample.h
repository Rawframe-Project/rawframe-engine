#pragma once

// The checkpoint tests' World and the layout of the artifact it captures to,
// shared by the tests and the fuzz target (D242): a World with holes in its
// slots, references both ways and to itself, one to an entity that is gone,
// and random streams drawn from; and every digest of a damaged artifact made
// to match again.

#include "rawframe/base/sha256.h"
#include "rawframe/world_snapshot/checkpoint.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <vector>

namespace rawframe::world_snapshot::testing {

/// Setting up the sample may not fail.
inline void must(bool held) {
    if (!held) {
        std::abort();
    }
}

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

inline SnapshotProjection projection() {
    return SnapshotProjection{.components = {
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

inline std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add<Body>().add<Link>();
    return *builder.freeze();
}

constexpr CheckpointIdentity kIdentity{.schema = {std::byte{7}}};

/// The same game with two mods.
inline CheckpointIdentity modded() {
    return CheckpointIdentity{
        .schema = kIdentity.schema,
        .mods = {{.subject = "fan/horde", .version = "1.0.0"}, {.subject = "fan/sting", .version = "0.2.0"}}};
}

inline CaptureSettings settings(SnapshotLimits limits = {}) {
    return CaptureSettings{.tick = world::TickIndex{1234},
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
        must(world.destroy(made[1]).has_value());
        must(world.destroy(made[4]).has_value());
        for (const std::size_t kIndex : {0U, 2U, 3U, 5U, 6U, 7U}) {
            live.push_back(made[kIndex]);
        }
        for (std::size_t index = 0; index < live.size(); ++index) {
            if (index != 3) {
                must(world
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
        must(world.insert(live[0], kLink, Link{.target = live[5], .other = live[0], .strength = 9}).has_value());
        must(world.insert(live[5], kLink, Link{.target = live[0], .other = made[4], .strength = 1}).has_value());
        must(world.insert(live[3], kLink, Link{.target = {}, .other = live[2], .strength = 0}).has_value());
        auto& stream = world.randomStream("test.owner", "dice");
        static_cast<void>(stream.nextU32());
        static_cast<void>(world.randomStream("test.owner", "coins").nextU64());
    }
};

inline std::uint64_t u64At(std::span<const std::byte> bytes, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[at + index]) << (8U * index);
    }
    return value;
}

inline void digestAt(std::vector<std::byte>& bytes, std::size_t at, std::span<const std::byte> of) {
    const auto kDigest = base::sha256(of);
    std::ranges::copy(kDigest, bytes.begin() + static_cast<std::ptrdiff_t>(at));
}

/// `damaged` with every digest made to match again, its chunks where they
/// lie in `layout` (SPEC-0011's generation 1): what a hostile writer hands
/// over, which only the reader's structure can refuse. A digest is not a
/// signature.
inline std::vector<std::byte> resealed(std::vector<std::byte> damaged, std::span<const std::byte> layout) {
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

} // namespace rawframe::world_snapshot::testing
