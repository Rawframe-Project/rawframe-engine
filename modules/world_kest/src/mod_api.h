#pragma once

// The Mod API lines of a game description (SPEC-0042, D177), read by
// parseGame.

#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// Where each kind of Mod API line was, for the checks made once every
/// line has been read; nought for none.
struct ModLines {
    std::size_t policy = 0;
    std::size_t modApi = 0;
    std::size_t firstApproval = 0;
    std::size_t firstPoint = 0;
    /// The Mod API surface's bytes so far.
    std::size_t surfaceBytes = 0;
    /// Each `prefer` line, its point and subjects, given to the point once
    /// every point is read.
    struct Preference {
        std::size_t line = 0;
        std::string point;
        std::vector<std::string> subjects;
    };
    std::vector<Preference> preferences;
};

/// Whether `keyword` begins a Mod API line.
[[nodiscard]] bool modKeyword(std::string_view keyword) noexcept;

/// Reads one Mod API line into `game`.
[[nodiscard]] result::Status
readModLine(std::span<const std::string_view> words, std::size_t line, GameDescription& game, ModLines& lines);

/// Checks what only the whole description says: a Mod API where there is a
/// policy other than closed or a point, approvals only where curated, each
/// point's component declared, and each `prefer` line on an exclusive point
/// of its own, which it gives the point.
[[nodiscard]] result::Status checkModApi(GameDescription& game, const ModLines& lines);

} // namespace rawframe::world_kest
