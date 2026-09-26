#include "rawframe/world_replication/checksum.h"

#include "rawframe/base/sha256.h"

#include <array>

namespace rawframe::world_replication {

namespace {

std::uint64_t firstEight(const base::Sha256Digest& digest) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= std::to_integer<std::uint64_t>(digest[index]) << (8U * index);
    }
    return value;
}

} // namespace

std::uint64_t scopeFingerprint(std::span<const schema::ComponentTypeId> predicted) noexcept {
    base::Sha256 hasher;
    hasher.update("rawframe.prediction.scope.v1");
    for (const schema::ComponentTypeId& component : predicted) {
        std::array<std::byte, 16> bits{};
        for (std::size_t index = 0; index < 8; ++index) {
            bits[index] = static_cast<std::byte>((component.value.high >> (8U * (7 - index))) & 0xFFU);
            bits[8 + index] = static_cast<std::byte>((component.value.low >> (8U * (7 - index))) & 0xFFU);
        }
        hasher.update(bits);
    }
    return firstEight(hasher.finish());
}

std::uint64_t predictedChecksum(std::span<const std::span<const std::byte>> encoded) noexcept {
    base::Sha256 hasher;
    hasher.update("rawframe.prediction.checksum.v1");
    for (const std::span<const std::byte> value : encoded) {
        std::array<std::byte, 4> length{};
        for (std::size_t index = 0; index < length.size(); ++index) {
            length[index] = static_cast<std::byte>((value.size() >> (8U * index)) & 0xFFU);
        }
        hasher.update(length);
        hasher.update(value);
    }
    return firstEight(hasher.finish());
}

} // namespace rawframe::world_replication
