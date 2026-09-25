#pragma once

// A game's spawn lines as a scene (D94), for moving its starting entities
// into a scene document: each entity a line spawns becomes one entity of
// the scene, with a new SourceEntityId, the fields the line gives in their
// canonical text (a collision class named by its identity), and each
// component's layout mark (layouts.h).

#include "rawframe/base/bits128.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"
#include "rawframe/world_kest/game.h"

#include <functional>

namespace rawframe::world_kest {

/// `nextId` gives each entity its SourceEntityId: fresh, never nought, never
/// twice. Refused when a line gives a value that is not a number, `true`,
/// or `false`, or a component has no layout.
[[nodiscard]] result::Result<scene::Scene>
spawnsAsScene(const GameDescription& game, const kest::Program& program, const std::function<base::Bits128()>& nextId);

} // namespace rawframe::world_kest
