#pragma once

// Reading a description's lines (game.cpp, mod.cpp): a line's words, the
// words' forms, and a line refused by its number.

#include "rawframe/result/result.h"
#include "rawframe/world_kest/errors.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// A line's words, split at spaces and tabs.
[[nodiscard]] std::vector<std::string_view> words(std::string_view line);

/// Sixteen hexadecimal digits, as identities are written in lines.
[[nodiscard]] std::optional<std::uint64_t> parseHex64(std::string_view word) noexcept;

/// A finite number within a range, as a word.
[[nodiscard]] std::optional<double> parseReal(std::string_view word, double lowest, double highest) noexcept;

/// Lower-case letters, digits, and underscores, at least one.
[[nodiscard]] bool lowerSnake(std::string_view word) noexcept;

/// Line `line` refused as `error`, `bad_game_line` unless said.
[[nodiscard]] std::unexpected<result::Error> badLine(std::size_t line, WorldKestError error, std::string_view why);
[[nodiscard]] std::unexpected<result::Error> badLine(std::size_t line, std::string_view why);

} // namespace rawframe::world_kest
