#pragma once

// Kest-declared components on the wire: a component's replication codec is
// its Kest type's fields, in memory order, each at its declared width.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_replication/codec.h"

namespace rawframe::world_kest {

/// The codec for a component laid out as `layout`. Refuses (`unsupported`)
/// a type with a field that is not a number or a truth, since only those
/// cross the wire in generation 1.
[[nodiscard]] result::Result<world_replication::ComponentCodec> codecFor(schema::ComponentTypeId component,
                                                                         const kest::TypeLayout& layout);

} // namespace rawframe::world_kest
