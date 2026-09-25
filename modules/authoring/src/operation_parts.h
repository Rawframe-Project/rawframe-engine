#pragma once

// What deriving an operation's deltas shares between a scene's own
// entities (operations.cpp) and the entities its instances bring
// (patches.cpp).

#include "rawframe/authoring/operations.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::authoring {

/// Refusals naming the failing operation as `operation`.
[[nodiscard]] std::unexpected<result::Error> notFound(const Operation& operation, std::string_view why);
[[nodiscard]] std::unexpected<result::Error> invalid(const Operation& operation, std::string_view why);
[[nodiscard]] std::unexpected<result::Error> conflict(const Operation& operation, std::string_view why);

/// The mark the scene's schema records for `component`, or nought.
[[nodiscard]] std::uint64_t markOf(const scene::Scene& scene, std::string_view component);

/// Refuses (`ValidationFailed`) a component the scene records against
/// another layout than the catalog's.
[[nodiscard]] result::Status
sameLayout(const scene::Scene& scene, const Operation& operation, const ComponentSchema& component);

/// Whether anything in the scene but `entity`'s own components and patch
/// entries names it.
[[nodiscard]] bool referenced(const scene::Scene& scene, base::Bits128 entity);

/// Whether a reference may name `entity`: one of the scene's own, or one
/// an instance brings and does not remove.
[[nodiscard]] bool present(const scene::Scene& scene, base::Bits128 entity);

/// Whether a recorded value is one a field of `kind` holds: numbers by
/// their text, so an integer field takes no fraction and an unsigned one no
/// sign; a truth for a truth; an entity for a reference.
[[nodiscard]] bool fits(const scene::FieldValue& value, FieldKind kind);

/// The value a field input writes; none for the field's default.
[[nodiscard]] result::Result<std::optional<scene::FieldValue>>
valueOf(const Operation& operation, const FieldInput& input, FieldKind kind);

/// The deltas of `operation` on `entity`, which `instance` brings.
[[nodiscard]] result::Result<Journal> derivePatch(const scene::Scene& scene,
                                                  const Operation& operation,
                                                  const ComponentCatalog& catalog,
                                                  const scene::SceneInstance& instance,
                                                  base::Bits128 entity);

/// The deltas of adding or removing a whole instance.
[[nodiscard]] result::Result<Journal> deriveInstance(const scene::Scene& scene,
                                                     const Operation& operation,
                                                     const scene::SceneSource* sources,
                                                     base::Bits128 document);

} // namespace rawframe::authoring
