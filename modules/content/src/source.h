#pragma once

// A source's reading, inside the module: exactly `length` bytes of a
// locator, or the typed error SPEC-0008 names.

#include "rawframe/content/source.h"

namespace rawframe::content {

struct ContentSource::Implementation {
    virtual ~Implementation() = default;
    /// Blocks: run on the blocking-I/O executor only.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> read(std::string_view locator,
                                                                      std::uint64_t length) const = 0;
};

} // namespace rawframe::content
