#pragma once

// The Mod API lines of a game description (SPEC-0042, D177), read by
// parseGame.

#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace rawframe::world_kest {

/// Where each kind of Mod API line was, for the checks made once every
/// line has been read; nought for none.
struct ModLines {
    std::size_t policy = 0;
    std::size_t modApi = 0;
    std::size_t firstApproval = 0;
    std::size_t firstPoint = 0;
};

/// Whether `keyword` begins a Mod API line.
[[nodiscard]] bool modKeyword(std::string_view keyword) noexcept;

/// Reads one Mod API line into `game`.
[[nodiscard]] result::Status
readModLine(std::span<const std::string_view> words, std::size_t line, GameDescription& game, ModLines& lines);

/// Checks what only the whole description says: a Mod API where there is a
/// policy other than closed or a point, approvals only where curated, and
/// each point's component declared.
[[nodiscard]] result::Status checkModApi(const GameDescription& game, const ModLines& lines);

} // namespace rawframe::world_kest
