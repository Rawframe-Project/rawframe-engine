// The Hierarchy's tree (D785): children under their parents in the list's
// order, an unknown parent or a cycle shown as roots, every entity once,
// and a parent that would be its own descendant found.

#include "../src/hierarchy.h"
#include "rawframe/test/test.h"

using namespace rawframe::studio;

namespace {

std::vector<std::pair<std::size_t, std::size_t>> pairs(const std::vector<TreeLine>& lines) {
    std::vector<std::pair<std::size_t, std::size_t>> made;
    for (const TreeLine& line : lines) {
        made.emplace_back(line.at, line.depth);
    }
    return made;
}

} // namespace

RAWFRAME_TEST(ChildrenFollowTheirParents) {
    // 0 ground; 1 hand under 3; 2 sword under 1; 3 body; 4 lamp.
    const std::vector<std::string> kIds{"ground", "hand", "sword", "body", "lamp"};
    const std::vector<std::string> kParents{"", "body", "hand", "", ""};
    const auto kLines = pairs(treeOf(kIds, kParents));
    const std::vector<std::pair<std::size_t, std::size_t>> kWanted{{0, 0}, {3, 0}, {1, 1}, {2, 2}, {4, 0}};
    RAWFRAME_EXPECT(kLines == kWanted);
}

RAWFRAME_TEST(AnUnknownParentOrACycleIsARoot) {
    const std::vector<std::string> kIds{"a", "b", "c", "d"};
    // a and b follow each other; c follows one not listed; d follows c.
    const std::vector<std::string> kParents{"b", "a", "gone", "c"};
    const auto kLines = treeOf(kIds, kParents);
    RAWFRAME_EXPECT(kLines.size() == 4);
    std::vector<bool> seen(4, false);
    for (const TreeLine& line : kLines) {
        RAWFRAME_EXPECT(!seen[line.at]);
        seen[line.at] = true;
    }
    RAWFRAME_EXPECT(pairs(kLines).back() == (std::pair<std::size_t, std::size_t>{3, 1}));
}

RAWFRAME_TEST(AParentThatWouldBeItsOwnChildIsFound) {
    const std::vector<std::string> kIds{"body", "hand", "sword"};
    const std::vector<std::string> kParents{"", "body", "hand"};
    RAWFRAME_EXPECT(isUnder(kIds, kParents, "sword", "body"));
    RAWFRAME_EXPECT(isUnder(kIds, kParents, "body", "body"));
    RAWFRAME_EXPECT(!isUnder(kIds, kParents, "body", "sword"));
}
