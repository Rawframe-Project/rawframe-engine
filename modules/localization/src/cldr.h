#pragma once

// The pinned CLDR data localization needs (ADR-0050), compiled ahead of
// time by tools/generate_cldr.py into the tables under generated/. Every
// table is sorted by its first column, so a lookup is a binary search.

#include <span>
#include <string_view>

namespace rawframe::localization::cldr {

struct Pair {
    std::string_view key;
    std::string_view value;
};

/// A tag (`und` for any language) to its maximal form, language, script,
/// and region.
[[nodiscard]] std::span<const Pair> likelySubtags() noexcept;
/// A tag's parent where it is not the tag less its last subtag; `und` is
/// the root.
[[nodiscard]] std::span<const Pair> parentLocales() noexcept;
/// Deprecated or legacy subtags to their replacements; a language's may
/// bring a script or a region.
[[nodiscard]] std::span<const Pair> languageAliases() noexcept;
[[nodiscard]] std::span<const Pair> scriptAliases() noexcept;
[[nodiscard]] std::span<const Pair> regionAliases() noexcept;
/// The subtags CLDR knows, as its likely subtags name them.
[[nodiscard]] std::span<const std::string_view> languages() noexcept;
[[nodiscard]] std::span<const std::string_view> scripts() noexcept;
[[nodiscard]] std::span<const std::string_view> regions() noexcept;

/// The value `key` maps to in a sorted table, if it has one.
[[nodiscard]] inline const std::string_view* find(std::span<const Pair> table, std::string_view key) noexcept {
    std::size_t low = 0;
    std::size_t high = table.size();
    while (low < high) {
        const std::size_t kMiddle = low + ((high - low) / 2);
        if (table[kMiddle].key < key) {
            low = kMiddle + 1;
        } else {
            high = kMiddle;
        }
    }
    return low < table.size() && table[low].key == key ? &table[low].value : nullptr;
}

} // namespace rawframe::localization::cldr
