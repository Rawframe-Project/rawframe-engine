#pragma once

// A game's `field` lines: what its authors are told of a number
// field, beside the engine's own hints for its types' fields.

#include "rawframe/world_kest/game.h"

#include <optional>
#include <span>
#include <string_view>

namespace rawframe::world_kest {

/// The words of a `field` line after `field`: `<component> <field>`, then
/// any of `range <least> <most>`, `unit <word>`, `color`, `asset <kind>`, and `labels`
/// followed by number and name pairs, each at most once, read into `hint`;
/// why not, when they do not read.
[[nodiscard]] std::optional<std::string_view> readFieldLine(std::span<const std::string_view> words,
                                                            GameFieldHint& hint);

} // namespace rawframe::world_kest
