#pragma once

// A game's components as the World lays them out (D94): a type of the
// program's own as the program lays it out, with Kest's mark; a component
// the engine owns (its physics components, a player's Perception, the
// persistent identity) as the engine lays it out, with a mark the engine
// derives from that layout, the same whether or not the program names the
// type. The mark is what a scene records it was authored against (D92).

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <cstdint>

namespace rawframe::world_kest {

/// `component`'s layout. Refused (`bad_game_line`) when the program names
/// an engine-owned type and lays it out otherwise than the engine, and
/// (the program's error) when it lays out none for a type of its own.
[[nodiscard]] result::Result<kest::TypeLayout>
componentLayout(const GameDescription& game, const kest::Program& program, const GameComponent& component);

/// The mark of an engine-owned layout: a digest of its size, alignment, and
/// each field's name, offset, and kind.
[[nodiscard]] std::uint64_t engineLayoutMark(const kest::TypeLayout& layout);

/// Whether two layouts are the same shape, field by field; marks aside.
[[nodiscard]] bool sameLayout(const kest::TypeLayout& left, const kest::TypeLayout& right);

} // namespace rawframe::world_kest
