// Where a solid dragged across the blockout plan lands: X and Z moved on a
// quarter-meter grid, the height carried over exactly as it was written.

#include "../src/blockout_plan.h"
#include "rawframe/test/test.h"

#include <optional>
#include <string>

using namespace rawframe;

RAWFRAME_TEST(ADraggedSolidKeepsItsHeightExactly) {
    // A height six digits cannot hold, and one past nine, carried over
    // byte for byte.
    RAWFRAME_EXPECT(studio::draggedPlace("1 1.0000001 2", {0, 0}) == std::optional<std::string>{"1 1.0000001 2"});
    RAWFRAME_EXPECT(studio::draggedPlace("0 1.23456789012 0", {0.3, -0.6}) ==
                    std::optional<std::string>{"0.25 1.23456789012 -0.5"});
}

RAWFRAME_TEST(ADraggedSolidLandsOnTheQuarterMeterGrid) {
    RAWFRAME_EXPECT(studio::draggedPlace("2 0.5 -3", {1.1, 0.9}) == std::optional<std::string>{"3 0.5 -2"});
    RAWFRAME_EXPECT(studio::draggedPlace("0.25 0 0", {-0.3, 0}) == std::optional<std::string>{"0 0 0"});
    RAWFRAME_EXPECT(!studio::draggedPlace("a b c", {1, 1}).has_value());
    RAWFRAME_EXPECT(!studio::draggedPlace("1 2", {1, 1}).has_value());
}
