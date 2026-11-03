#pragma once

#include <string>

namespace rawframe::base {

/// A number as an authored text document writes it: as nine significant
/// digits (`%.9g`) where those read back to the very same value, so the
/// numbers people write keep their spelling, and otherwise the shortest
/// spelling that does (`std::to_chars`). Either reads back with
/// `std::from_chars`. For a finite value.
[[nodiscard]] std::string numberText(double value);

} // namespace rawframe::base
