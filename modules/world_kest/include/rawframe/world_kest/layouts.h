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
#include "rawframe/world_runtime/component_fields.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

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

/// Whether `component` is of the engine's Kest type `qualified` (such as
/// `rawframe.sound.Emitter`): named by that full name, or by its last part
/// as a component line may name it.
[[nodiscard]] bool ofEngineType(const GameComponent& component, std::string_view qualified);

/// Whether `program` lays `type` out as C++ reads it: `size` bytes, and
/// exactly `fields`, by name and offset, in order. An engine module that
/// reads a component of a type of the engine's Kest library (a sound's
/// emitter, a sprite) checks it so first.
[[nodiscard]] bool laidOutAs(const kest::Program& program,
                             std::string_view type,
                             std::size_t size,
                             std::span<const std::pair<std::string_view, std::size_t>> fields);
[[nodiscard]] bool laidOutAs(const kest::Program& program,
                             std::string_view type,
                             std::size_t size,
                             std::initializer_list<std::pair<std::string_view, std::size_t>> fields);

/// Whether the program's physics types are laid out as the engine's
/// components and query answers; refused (`bad_game_line`) naming the type
/// otherwise. `layouts` are the game's components', in their order.
[[nodiscard]] result::Status checkPhysicsLayouts(const GameDescription& game,
                                                 const kest::Program& program,
                                                 std::span<const kest::TypeLayout> layouts);

/// Whether a layout holds an entity: `rawframe.world.Entity`'s two pieces,
/// `slot` and then `generation` four bytes on, under one field's name or
/// none. A value that crosses between the server's World and a client's
/// may not, for it names one World only.
[[nodiscard]] bool holdsEntity(const kest::TypeLayout& layout);

/// Each of the game's components field by field for a reader of the World
/// (D409), from `layouts` in the components' order: an entity field whole,
/// an enum by its cases, a field of no plain kind left out.
[[nodiscard]] std::vector<world_runtime::ComponentFieldSet>
componentFieldSets(const GameDescription& game, std::span<const kest::TypeLayout> layouts);

} // namespace rawframe::world_kest
