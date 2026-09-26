#include "rawframe/world_snapshot/projection.h"

#include "rawframe/world_snapshot/checkpoint.h"

#include <algorithm>
#include <array>

namespace rawframe::world_snapshot {

std::size_t widthOf(FieldKind kind) noexcept {
    switch (kind) {
    case FieldKind::I8:
    case FieldKind::U8:
    case FieldKind::Bool:
        return 1;
    case FieldKind::I16:
    case FieldKind::U16:
        return 2;
    case FieldKind::I32:
    case FieldKind::U32:
    case FieldKind::F32:
        return 4;
    case FieldKind::I64:
    case FieldKind::U64:
    case FieldKind::F64:
    case FieldKind::Entity:
        return 8;
    }
    return 0;
}

std::size_t rowGroup(const SnapshotComponent& component, const SnapshotLimits& limits) noexcept {
    // A row is its place and its fields.
    std::size_t wire = 8;
    for (const SnapshotField& field : component.fields) {
        wire += widthOf(field.kind);
    }
    return std::max<std::size_t>(1, std::min(limits.rowsPerChunk, limits.maximumChunkBytes / wire));
}

base::Sha256Digest projectionFingerprint(const SnapshotProjection& projection) {
    std::vector<const SnapshotComponent*> sorted;
    for (const SnapshotComponent& component : projection.components) {
        sorted.push_back(&component);
    }
    std::sort(sorted.begin(), sorted.end(), [](const SnapshotComponent* left, const SnapshotComponent* right) {
        return left->id < right->id;
    });
    base::Sha256 hasher;
    hasher.update("rawframe.world_snapshot.projection.v1");
    const auto kWord = [&hasher](std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 0xFFU);
        }
        hasher.update(bytes);
    };
    kWord(sorted.size());
    for (const SnapshotComponent* component : sorted) {
        kWord(component->id.value.high);
        kWord(component->id.value.low);
        kWord(component->size);
        kWord(component->fields.size());
        for (const SnapshotField& field : component->fields) {
            kWord(field.offset);
            kWord(static_cast<std::uint64_t>(field.kind));
        }
    }
    return hasher.finish();
}

bool projectionValid(const SnapshotProjection& projection) noexcept {
    for (std::size_t index = 0; index < projection.components.size(); ++index) {
        const SnapshotComponent& component = projection.components[index];
        for (std::size_t other = 0; other < index; ++other) {
            if (projection.components[other].id == component.id) {
                return false;
            }
        }
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        for (const SnapshotField& field : component.fields) {
            const std::size_t kWidth = widthOf(field.kind);
            if (kWidth == 0 || field.offset > component.size || kWidth > component.size - field.offset) {
                return false;
            }
            spans.emplace_back(field.offset, field.offset + kWidth);
        }
        std::sort(spans.begin(), spans.end());
        for (std::size_t span = 1; span < spans.size(); ++span) {
            if (spans[span].first < spans[span - 1].second) {
                return false;
            }
        }
    }
    return true;
}

} // namespace rawframe::world_snapshot
