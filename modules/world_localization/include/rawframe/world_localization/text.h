#pragma once

// A client's text (ADR-0050, SPEC-0033, D147): the string tables and
// translations its game's `text` lines name, read by identity from the
// Runtime's content and built into one catalog, and the locales it is asked
// in. Presentation only: a dedicated server never links this.

#include "rawframe/base/bits128.h"
#include "rawframe/composition/participant.h"
#include "rawframe/localization/catalog.h"
#include "rawframe/localization/locale.h"
#include "rawframe/localization/message.h"
#include "rawframe/result/result.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_localization {

class GameText {
public:
    /// `offered` is what a player may choose among while playing (D539):
    /// the project's default first, then each locale a translation is in,
    /// by its tag.
    GameText(localization::Catalog catalog,
             std::vector<std::pair<std::string, base::Bits128>> tables,
             localization::Locale requested,
             localization::Locale projectDefault,
             std::vector<localization::Locale> offered = {}) noexcept;

    [[nodiscard]] const localization::Catalog& catalog() const noexcept {
        return catalog_;
    }
    /// The locale local player `player` asks for (ADR-0050: per-player
    /// presentation state): the one the client was configured with until
    /// that player chooses another. `player` is a local player's place,
    /// which the client bounds.
    [[nodiscard]] const localization::Locale& requested(std::size_t player) const noexcept {
        return player < asked_.size() ? asked_[player] : requested_;
    }
    /// The project's default, which every key falls back through
    /// (locale.h).
    [[nodiscard]] const localization::Locale& projectDefault() const noexcept {
        return projectDefault_;
    }
    /// The locales a player may choose among, and which of them `player`
    /// asks for: past the last for one configured that none of them is.
    [[nodiscard]] std::span<const localization::Locale> offered() const noexcept {
        return offered_;
    }
    [[nodiscard]] std::size_t chosen(std::size_t player) const noexcept;
    /// `player` asks for the offered locale at `index` from now on, the
    /// other local players keeping theirs (ADR-0050: a change is a revision
    /// every formatted word follows); false for none such. Choosing the one
    /// asked for changes nothing.
    bool choose(std::size_t player, std::size_t index);
    /// Counts the changes of any local player's locale, so what formatted
    /// words formats them again when one moves.
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }
    /// Every key formatted as its key token, the key itself, so a screen
    /// says which key each of its words is: SPEC-0033's display-keys mode,
    /// which only a development build turns on (D540).
    void showKeys() noexcept {
        showingKeys_ = true;
    }
    /// The identity of the table a `text` line names `path`, if it names a
    /// table.
    [[nodiscard]] std::optional<base::Bits128> table(std::string_view path) const noexcept;
    /// A key of the table at `path`, formatted in the locale `player` asks
    /// for or the first it falls back to. Refuses as `Catalog::format`
    /// does, and (`KeyUnknown`) a path that names no table.
    [[nodiscard]] result::Result<std::string> format(std::size_t player,
                                                     std::string_view path,
                                                     std::string_view key,
                                                     std::span<const localization::Argument> arguments) const;

private:
    localization::Catalog catalog_;
    std::vector<std::pair<std::string, base::Bits128>> tables_;
    localization::Locale requested_;
    /// Each local player's locale, up to the last who chose one.
    std::vector<localization::Locale> asked_;
    localization::Locale projectDefault_;
    std::vector<localization::Locale> offered_;
    std::uint64_t revision_ = 0;
    bool showingKeys_ = false;
};

/// The Runtime's text: its game's, when it names any and cooked content holds
/// it; else none, every key unknown (D386).
inline constexpr composition::Capability<GameText> kGameText{"rawframe.world_localization.text"};

} // namespace rawframe::world_localization
