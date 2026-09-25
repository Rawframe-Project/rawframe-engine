#include "rawframe/world_replication/codec.h"

#include "rawframe/world_replication/errors.h"

#include <bit>
#include <cstring>

static_assert(std::endian::native == std::endian::little, "values are read from memory as little-endian");

namespace rawframe::world_replication {

std::size_t ComponentCodec::wireSize() const noexcept {
    std::size_t total = 0;
    for (const WireField& field : fields) {
        total += widthOf(field.kind);
    }
    return total;
}

bool ComponentCodec::valid() const noexcept {
    for (const WireField& field : fields) {
        if (field.offset + memoryWidthOf(field.kind) > size) {
            return false;
        }
    }
    return component.valid();
}

bool ComponentCodec::namesEntities() const noexcept {
    for (const WireField& field : fields) {
        if (field.kind == WireKind::Entity) {
            return true;
        }
    }
    return false;
}

result::Status ComponentCodec::encode(const std::byte* value, network::Writer& writer, const EntityNames* names) const {
    for (const WireField& field : fields) {
        const std::size_t kWidth = widthOf(field.kind);
        // Native bytes into a whole number, then out most significant first.
        std::uint64_t bits = 0;
        if (field.kind == WireKind::Entity) {
            world::EntityHandle entity;
            std::memcpy(&entity, value + field.offset, sizeof entity);
            bits = names != nullptr && !entity.isNull() ? names->netOf(entity) : 0;
        } else {
            std::memcpy(&bits, value + field.offset, kWidth);
        }
        if (field.kind == WireKind::Bool) {
            bits = bits != 0 ? 1 : 0;
        }
        std::byte wire[8];
        for (std::size_t index = 0; index < kWidth; ++index) {
            wire[index] = static_cast<std::byte>((bits >> (8U * (kWidth - 1U - index))) & 0xFFU);
        }
        RAWFRAME_TRY(writer.bytes(std::span{wire, kWidth}));
    }
    return {};
}

result::Status ComponentCodec::decode(network::Reader& reader, std::byte* into, const EntityNames* names) const {
    for (const WireField& field : fields) {
        const std::size_t kWidth = widthOf(field.kind);
        RAWFRAME_TRY_ASSIGN(const std::span<const std::byte> kWire, reader.bytes(kWidth));
        std::uint64_t bits = 0;
        for (const std::byte kByte : kWire) {
            bits = (bits << 8U) | std::to_integer<std::uint8_t>(kByte);
        }
        if (field.kind == WireKind::Bool && bits > 1) {
            return result::fail(result::ErrorClass::InvalidArgument,
                                kReplicationDomain,
                                code(ReplicationError::Malformed),
                                "a truth crossed as something other than nought or one");
        }
        if (field.kind == WireKind::Entity) {
            const world::EntityHandle kEntity = names != nullptr && bits != 0
                                                    ? names->entityOf(static_cast<std::uint32_t>(bits))
                                                    : world::EntityHandle{};
            std::memcpy(into + field.offset, &kEntity, sizeof kEntity);
            continue;
        }
        std::memcpy(into + field.offset, &bits, kWidth);
    }
    return {};
}

} // namespace rawframe::world_replication
