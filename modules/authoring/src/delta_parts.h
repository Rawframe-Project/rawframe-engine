#pragma once

// What applying deltas (delta.cpp) and their journal's text (journal.cpp)
// share: the kinds' names, their refusals, and each kind's shape.

#include "rawframe/authoring/delta.h"
#include "rawframe/result/result.h"

#include <array>
#include <string_view>

namespace rawframe::authoring {

inline constexpr std::array<std::string_view, 12> kDeltaKindNames = {"create_node",
                                                                     "destroy_node",
                                                                     "reorder",
                                                                     "set_name",
                                                                     "add_component",
                                                                     "remove_component",
                                                                     "set_field",
                                                                     "set_reference",
                                                                     "set_mark",
                                                                     "set_override",
                                                                     "create_instance",
                                                                     "destroy_instance"};
inline constexpr std::array<std::string_view, 3> kPatchKindNames = {"set", "add", "remove"};

[[nodiscard]] std::unexpected<result::Error> deltaInvalid(std::string_view why);
[[nodiscard]] std::unexpected<result::Error> deltaMismatch(std::string_view why);

/// Kinds that name a component; set_override names one or none.
[[nodiscard]] bool componentKind(DeltaKind kind);
/// Kinds that name a field.
[[nodiscard]] bool fieldKind(DeltaKind kind);
/// The delta holds its kind's shape: the names its kind has, and slots
/// holding only its kind's member, before and after as the kind moves.
[[nodiscard]] bool shaped(const Delta& delta);

} // namespace rawframe::authoring
