#include "rawframe/world/persistent.h"

#include "rawframe/base/sha256.h"
#include "rawframe/world/column_query.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace rawframe::world {

namespace {

void feed(base::Sha256& hash, base::Bits128 value) noexcept {
    std::array<std::byte, 16> bytes{};
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<std::byte>((value.high >> (56U - (8U * index))) & 0xFFU);
        bytes[8 + index] = static_cast<std::byte>((value.low >> (56U - (8U * index))) & 0xFFU);
    }
    hash.update(bytes);
}

std::uint64_t word(const base::Sha256Digest& digest, std::size_t from) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | std::to_integer<std::uint64_t>(digest[from + index]);
    }
    return value;
}

} // namespace

PersistentEntityId persistentFromSource(base::Bits128 scene, base::Bits128 entity) noexcept {
    base::Sha256 hash;
    hash.update(std::string_view{"rawframe.world.persistent.source.v1"});
    feed(hash, scene);
    feed(hash, entity);
    const base::Sha256Digest kDigest = hash.finish();
    base::Bits128 value{.high = word(kDigest, 0), .low = word(kDigest, 8)};
    // Nought names nothing; a digest of nought is as unlikely as any other.
    value.low |= value.high == 0 && value.low == 0 ? 1U : 0U;
    return PersistentEntityId{value};
}

PersistentEntityId newPersistentId(World& world) {
    Pcg32& stream = world.randomStream(kPersistentStreamOwner, kPersistentStreamName);
    base::Bits128 value{.high = stream.nextU64(), .low = stream.nextU64()};
    value.low |= value.high == 0 && value.low == 0 ? 1U : 0U;
    return PersistentEntityId{value};
}

std::vector<PersistentEntityId> persistentDuplicates(World& world) {
    const auto kId = world.registry().find(Persistent::kComponentTypeId);
    if (!kId.has_value()) {
        return {};
    }
    const std::array<ColumnTerm, 1> kTerms = {ColumnTerm{*kId, Access::Read}};
    auto query = ColumnQuery::resolve(kTerms, world.registry());
    if (!query.has_value()) {
        return {};
    }
    std::vector<PersistentEntityId> held;
    query->forEachChunk(world, [&held](const ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            Persistent value;
            std::memcpy(static_cast<void*>(&value), chunk.columns[0] + (row * sizeof(Persistent)), sizeof value);
            if (value.named()) {
                held.push_back(value.id());
            }
        }
    });
    std::ranges::sort(held);
    std::vector<PersistentEntityId> twice;
    for (std::size_t index = 1; index < held.size(); ++index) {
        if (held[index] == held[index - 1] && (twice.empty() || twice.back() != held[index])) {
            twice.push_back(held[index]);
        }
    }
    return twice;
}

} // namespace rawframe::world
