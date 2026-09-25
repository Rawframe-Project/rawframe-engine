#pragma once

// Kest-declared components on the wire: a component's replication codec is
// its Kest type's fields, in memory order, each at its declared width.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/stable_id.h"
#include "rawframe/world_replication/codec.h"

#include <span>
#include <string>

namespace rawframe::world_kest {

/// The codec for a component laid out as `layout`, whose fields named in
/// `entities` hold a `rawframe.world.Entity` (their `.slot` and
/// `.generation`), which cross as the receiver's name for the entity.
/// Refuses (`unsupported`) a type with any other field that is not a number
/// or a truth.
[[nodiscard]] result::Result<world_replication::ComponentCodec>
codecFor(schema::ComponentTypeId component, const kest::TypeLayout& layout, std::span<const std::string> entities);

} // namespace rawframe::world_kest
