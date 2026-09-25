#pragma once

// A game's animators as the World plays them (D124): each graph compiled
// against the clips it names and their one skeleton, and its parameters
// bound, by name, to the fields of the component its line names.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_animation/animation.h"
#include "rawframe/world_kest/game.h"
#include "rawframe/world_kest/game_files.h"

#include <optional>
#include <span>

namespace rawframe::world_kest {

/// None for a game without animators. `layouts` are the game's components'
/// Kest layouts, in the order of `files.description().components`. Refused
/// (`bad_game_line`, with the animator's graph as context) for a document
/// that does not read, a graph whose clips animate no skeleton or more than
/// one, one that does not compile against them, and a parameter its
/// component has no field of its type for.
[[nodiscard]] result::Result<std::optional<world_animation::AnimationSettings>>
animationSettings(const GameFiles& files, std::span<const kest::TypeLayout> layouts);

} // namespace rawframe::world_kest
