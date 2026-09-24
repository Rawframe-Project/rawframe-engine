#pragma once

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::composition {

/// The most entries, and the longest key and value, a configuration holds.
inline constexpr std::size_t kMaximumConfigurationEntries = 256;
inline constexpr std::size_t kMaximumConfigurationKeyBytes = 64;
inline constexpr std::size_t kMaximumConfigurationValueBytes = 1024;

/// A Runtime's immutable configuration snapshot (SPEC-0005 Runtime): text
/// values under dotted lower_snake_case keys, owned by the host and read by
/// participants through their context. Nothing reads the environment.
class Configuration {
public:
    /// Parses `key = value` lines. Blank lines and lines starting with `#` are
    /// ignored. Refuses a malformed line, a bad or repeated key, and anything
    /// past the bounds, all as `invalid_argument`.
    [[nodiscard]] static result::Result<Configuration> parse(std::string_view text);

    [[nodiscard]] std::optional<std::string_view> text(std::string_view key) const;

    /// The value as an unsigned integer: absent gives `fallback`, and a
    /// malformed value is `invalid_argument`.
    [[nodiscard]] result::Result<std::uint64_t> unsignedInteger(std::string_view key, std::uint64_t fallback) const;

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::map<std::string, std::string, std::less<>> entries_;
};

} // namespace rawframe::composition
