#include "hierarchy.h"

#include <algorithm>

namespace rawframe::studio {

namespace {

/// The place of `id` in `ids`, if there.
std::optional<std::size_t> placeOf(std::span<const std::string> ids, const std::string& id) {
    const auto kFound = std::ranges::find(ids, id);
    return kFound == ids.end() ? std::nullopt : std::optional{static_cast<std::size_t>(kFound - ids.begin())};
}

} // namespace

bool isUnder(std::span<const std::string> ids,
             std::span<const std::string> parents,
             const std::string& entity,
             const std::string& ancestor) {
    std::string at = entity;
    // At most as many steps as there are entities: a cycle ends the walk.
    for (std::size_t step = 0; step <= ids.size(); ++step) {
        if (at == ancestor) {
            return true;
        }
        const std::optional<std::size_t> kPlace = placeOf(ids, at);
        if (!kPlace.has_value() || *kPlace >= parents.size() || parents[*kPlace].empty()) {
            return false;
        }
        at = parents[*kPlace];
    }
    return false;
}

std::vector<TreeLine> treeOf(std::span<const std::string> ids, std::span<const std::string> parents) {
    const std::size_t kCount = ids.size();
    // A root: no parent, a parent not listed, or one in a cycle.
    std::vector<std::optional<std::size_t>> parentAt(kCount);
    for (std::size_t each = 0; each < kCount; ++each) {
        if (each < parents.size() && !parents[each].empty()) {
            parentAt[each] = placeOf(ids, parents[each]);
        }
    }
    for (std::size_t each = 0; each < kCount; ++each) {
        if (parentAt[each].has_value() && isUnder(ids, parents, parents[each], ids[each])) {
            parentAt[each].reset();
        }
    }
    std::vector<TreeLine> lines;
    lines.reserve(kCount);
    std::vector<bool> shown(kCount, false);
    // Depth first without recursion: a stack of places and depths.
    for (std::size_t root = 0; root < kCount; ++root) {
        if (parentAt[root].has_value()) {
            continue;
        }
        std::vector<TreeLine> pending{{.at = root, .depth = 0}};
        while (!pending.empty()) {
            const TreeLine kLine = pending.back();
            pending.pop_back();
            if (shown[kLine.at]) {
                continue;
            }
            shown[kLine.at] = true;
            lines.push_back(kLine);
            // Children pushed last first, so they come out in list order.
            for (std::size_t child = kCount; child-- > 0;) {
                if (parentAt[child] == kLine.at && !shown[child]) {
                    pending.push_back({.at = child, .depth = kLine.depth + 1});
                }
            }
        }
    }
    return lines;
}

} // namespace rawframe::studio
