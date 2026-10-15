#pragma once

// The engine's importers in one list (D800): what the cook tool cooks
// with, and what an authoring session describes and checks settings by.

#include "rawframe/cook/cook.h"
#include "rawframe/cook/material.h"

#include <optional>
#include <string_view>
#include <vector>

namespace rawframe::cook {

/// Every importer the engine has, materials folded with `tools` when given.
[[nodiscard]] std::vector<Importer> engineImporters(std::optional<ShaderTools> tools = std::nullopt);

/// The importer named `identity` in `importers`, if any.
[[nodiscard]] const Importer* importerNamed(std::span<const Importer> importers, std::string_view identity) noexcept;

} // namespace rawframe::cook
