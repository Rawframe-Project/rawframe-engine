#pragma once

#include <array>
#include <span>
#include <string_view>

namespace rawframe::kest_library {

/// One file of the library as the build embedded it: its path as a compile
/// is handed it, and its text.
struct EmbeddedFile {
    std::string_view path;
    std::string_view text;
};

/// Every embedded file, in path order.
[[nodiscard]] std::span<const EmbeddedFile> embedded() noexcept;

} // namespace rawframe::kest_library
