#pragma once

// A Kest field's kind as the save and checkpoint
// formats name it: none for a kind neither can hold.

#include "rawframe/kest/program.h"
#include "rawframe/world_save/save.h"
#include "rawframe/world_snapshot/projection.h"

#include <optional>

namespace rawframe::world_kest {

[[nodiscard]] std::optional<world_save::FieldKind> savedKind(kest::FieldKind kind) noexcept;
[[nodiscard]] std::optional<world_snapshot::FieldKind> snapshotKind(kest::FieldKind kind) noexcept;

} // namespace rawframe::world_kest
