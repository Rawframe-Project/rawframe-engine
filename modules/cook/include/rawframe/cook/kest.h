#pragma once

// The Kest importer (`rawframe.kest`, D87): a game's `kest.project` cooked
// into its Kest files as one resource, every `.kest` file under the
// project's directory, which a process compiles its programs from with the
// engine's own library (D86). The project's other `source` lines name that
// library, which is the engine's and never cooked.

#include "rawframe/cook/cook.h"

namespace rawframe::cook {

[[nodiscard]] Importer kestImporter() noexcept;

} // namespace rawframe::cook
