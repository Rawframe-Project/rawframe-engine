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
    /// The locale the player asked for, and the project's default, which
    /// every key falls back through (locale.h).
    [[nodiscard]] const localization::Locale& requested() const noexcept {
        return requested_;
    }
    [[nodiscard]] const localization::Locale& projectDefault() const noexcept {
        return projectDefault_;
    }
    /// The locales a player may choose among, and which of them is asked
    /// for: past the last for one configured that none of them is.
    [[nodiscard]] std::span<const localization::Locale> offered() const noexcept {
        return offered_;
    }
    [[nodiscard]] std::size_t chosen() const noexcept;
    /// Asks for the offered locale at `index` from now on (ADR-0050: a
    /// change is a revision every formatted word follows); false for none
    /// such. Choosing the one asked for changes nothing.
    bool choose(std::size_t index) noexcept;
    /// Counts the changes of the locale asked for, so what formatted words
    /// formats them again when it moves.
    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }
    /// The identity of the table a `text` line names `path`, if it names a
    /// table.
    [[nodiscard]] std::optional<base::Bits128> table(std::string_view path) const noexcept;
    /// A key of the table at `path`, formatted in the requested locale or
    /// the first it falls back to. Refuses as `Catalog::format` does, and
    /// (`KeyUnknown`) a path that names no table.
    [[nodiscard]] result::Result<std::string>
    format(std::string_view path, std::string_view key, std::span<const localization::Argument> arguments) const;

private:
    localization::Catalog catalog_;
    std::vector<std::pair<std::string, base::Bits128>> tables_;
    localization::Locale requested_;
    localization::Locale projectDefault_;
    std::vector<localization::Locale> offered_;
    std::uint64_t revision_ = 0;
};

/// The Runtime's text: its game's, when it names any and cooked content holds
/// it; else none, every key unknown (D386).
inline constexpr composition::Capability<GameText> kGameText{"rawframe.world_localization.text"};

} // namespace rawframe::world_localization
