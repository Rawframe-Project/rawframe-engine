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

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::world_localization {

class GameText {
public:
    GameText(localization::Catalog catalog,
             std::vector<std::pair<std::string, base::Bits128>> tables,
             localization::Locale requested,
             localization::Locale projectDefault) noexcept;

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
};

/// The Runtime's text, when its game names any and cooked content holds it.
inline constexpr composition::Capability<GameText> kGameText{"rawframe.world_localization.text"};

} // namespace rawframe::world_localization
