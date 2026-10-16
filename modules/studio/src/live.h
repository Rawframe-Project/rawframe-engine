#pragma once

// The Hierarchy and the Inspector switched to the played game's World as
// its server holds it (D808): its living entities listed, and the one
// chosen read field by field, again twice a second, read only.

#include "rawframe/ui/tree.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rawframe::studio {

struct LiveShown {
    /// Whether the Hierarchy and the Inspector show the live World.
    bool on = false;
    /// The Hierarchy's switch.
    ui::Node button{};
    /// The listed entities' rows and names, as the server names them.
    std::vector<std::pair<ui::Node, std::string>> rows;
    /// The entity the Inspector reads, and when next.
    std::string chosen;
    double nextRead = 0;
    /// Times the World was listed and the chosen entity read, and its
    /// components when last read.
    std::uint64_t listed = 0;
    std::uint64_t reads = 0;
    std::uint64_t components = 0;
};

} // namespace rawframe::studio
