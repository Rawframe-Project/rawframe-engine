#include "rawframe/world_kest/replication.h"

#include "rawframe/world_replication/errors.h"

#include <optional>

namespace rawframe::world_kest {

namespace {

std::optional<world_replication::WireKind> wireKind(kest::FieldKind kind) noexcept {
    using world_replication::WireKind;
    switch (kind) {
    case kest::FieldKind::I8:
        return WireKind::I8;
    case kest::FieldKind::I16:
        return WireKind::I16;
    case kest::FieldKind::I32:
        return WireKind::I32;
    case kest::FieldKind::I64:
        return WireKind::I64;
    case kest::FieldKind::U8:
        return WireKind::U8;
    case kest::FieldKind::U16:
        return WireKind::U16;
    case kest::FieldKind::U32:
        return WireKind::U32;
    case kest::FieldKind::U64:
        return WireKind::U64;
    case kest::FieldKind::F32:
        return WireKind::F32;
    case kest::FieldKind::F64:
        return WireKind::F64;
    case kest::FieldKind::Bool:
        return WireKind::Bool;
    case kest::FieldKind::Other:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

result::Result<world_replication::ComponentCodec> codecFor(schema::ComponentTypeId component,
                                                           const kest::TypeLayout& layout) {
    world_replication::ComponentCodec codec{.component = component, .size = layout.size, .fields = {}};
    for (const kest::Field& field : layout.fields) {
        const auto kKind = wireKind(field.kind);
        if (!kKind) {
            return std::unexpected<result::Error>{
                result::fail(result::ErrorClass::Unsupported,
                             world_replication::kReplicationDomain,
                             world_replication::code(world_replication::ReplicationError::FieldUnsupported),
                             "a replicated Kest type has a field that is not a number or a truth")
                    .error()
                    .withContext("field", field.name)};
        }
        codec.fields.push_back(world_replication::WireField{.offset = field.offset, .kind = *kKind});
    }
    if (layout.fields.empty() && layout.size != 0) {
        return result::fail(result::ErrorClass::Unsupported,
                            world_replication::kReplicationDomain,
                            world_replication::code(world_replication::ReplicationError::FieldUnsupported),
                            "a replicated Kest type is read by its tag, not field by field");
    }
    return codec;
}

} // namespace rawframe::world_kest
