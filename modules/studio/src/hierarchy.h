#pragma once

// The Hierarchy as a tree: each entity under the one its attachment
// follows, as `scene.list_entities` gives its parent.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::studio {

struct TreeLine {
    /// The entity's place in the list it was given.
    std::size_t at = 0;
    /// How many ancestors it shows under.
    std::size_t depth = 0;
};

/// `ids` (with `parents`, empty for none, beside them) in the order a tree
/// shows them: each root in the list's order, each followed by its
/// children in the list's order, depth first. A parent the list lacks, or
/// one in a cycle, makes its entity a root, so every entity is shown once.
[[nodiscard]] std::vector<TreeLine> treeOf(std::span<const std::string> ids, std::span<const std::string> parents);

/// Whether `entity` is `ancestor` or under it, by `parents` beside `ids`: a
/// parent that would be its own child refused.
[[nodiscard]] bool isUnder(std::span<const std::string> ids,
                           std::span<const std::string> parents,
                           const std::string& entity,
                           const std::string& ancestor);

} // namespace rawframe::studio
