#pragma once

// Instances resolved (ADR-0048, D96): a scene with every instance replaced
// by the entities it brings. Each source scene is resolved first, its own
// instances included; its entities take the ids the mapping gives them, and
// references among them follow; then the patch is applied in order. What
// comes out is a scene of entities alone, in the scene's order, then each
// instance's in its source's order.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"

#include <cstddef>
#include <functional>

namespace rawframe::scene {

/// The most scenes deep one instance may reach through others.
inline constexpr std::size_t kMaximumInstanceDepth = 16;

/// The scene a resource identity names, for an instance's source.
using SceneSource = std::function<result::Result<Scene>(base::Bits128 scene)>;

/// Refused (`instance_invalid`) when the mapping is not exactly the source's
/// entities, an override sets or removes a component its entity lacks or
/// adds one it has, a reference names an entity an override removed, a scene instances itself however far down,
/// instances reach deeper than kMaximumInstanceDepth, or two scenes were authored against different layouts of one
/// component; and with `source`'s error when a source cannot be had.
[[nodiscard]] result::Result<Scene> resolveInstances(const Scene& scene, const SceneSource& source);

} // namespace rawframe::scene
