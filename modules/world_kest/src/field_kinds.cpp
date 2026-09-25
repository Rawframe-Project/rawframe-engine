#include "field_kinds.h"

namespace rawframe::world_kest {

std::optional<world_save::FieldKind> savedKind(kest::FieldKind kind) noexcept {
    switch (kind) {
    case kest::FieldKind::I8:
        return world_save::FieldKind::I8;
    case kest::FieldKind::I16:
        return world_save::FieldKind::I16;
    case kest::FieldKind::I32:
        return world_save::FieldKind::I32;
    case kest::FieldKind::I64:
        return world_save::FieldKind::I64;
    case kest::FieldKind::U8:
        return world_save::FieldKind::U8;
    case kest::FieldKind::U16:
        return world_save::FieldKind::U16;
    case kest::FieldKind::U32:
        return world_save::FieldKind::U32;
    case kest::FieldKind::U64:
        return world_save::FieldKind::U64;
    case kest::FieldKind::F32:
        return world_save::FieldKind::F32;
    case kest::FieldKind::F64:
        return world_save::FieldKind::F64;
    case kest::FieldKind::Bool:
        return world_save::FieldKind::Bool;
    case kest::FieldKind::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<world_snapshot::FieldKind> snapshotKind(kest::FieldKind kind) noexcept {
    switch (kind) {
    case kest::FieldKind::I8:
        return world_snapshot::FieldKind::I8;
    case kest::FieldKind::I16:
        return world_snapshot::FieldKind::I16;
    case kest::FieldKind::I32:
        return world_snapshot::FieldKind::I32;
    case kest::FieldKind::I64:
        return world_snapshot::FieldKind::I64;
    case kest::FieldKind::U8:
        return world_snapshot::FieldKind::U8;
    case kest::FieldKind::U16:
        return world_snapshot::FieldKind::U16;
    case kest::FieldKind::U32:
        return world_snapshot::FieldKind::U32;
    case kest::FieldKind::U64:
        return world_snapshot::FieldKind::U64;
    case kest::FieldKind::F32:
        return world_snapshot::FieldKind::F32;
    case kest::FieldKind::F64:
        return world_snapshot::FieldKind::F64;
    case kest::FieldKind::Bool:
        return world_snapshot::FieldKind::Bool;
    case kest::FieldKind::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace rawframe::world_kest
