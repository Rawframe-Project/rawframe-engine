// Splines: a path through every point, straight where its points
// are in a line, measured along its length, wrapping where it is closed,
// and the same for the same points.

#include "rawframe/spline/spline.h"
#include "rawframe/test/test.h"

#include <cmath>

using namespace rawframe;
using namespace rawframe::spline;

namespace {

bool near(const Point& point, const Point& expected, double within = 1e-9) {
    return std::abs(point[0] - expected[0]) < within && std::abs(point[1] - expected[1]) < within &&
           std::abs(point[2] - expected[2]) < within;
}

} // namespace

RAWFRAME_TEST(APathInALineIsTheLine) {
    // Ten meters long, half way at five, heading along X; past either end
    // held to it.
    const Walk kWalk = walk(Spline{.points = {{0, 0, 0}, {10, 0, 0}}});
    RAWFRAME_EXPECT(kWalk.points.size() == kPiecesEach + 1 && std::abs(kWalk.length() - 10) < 1e-9);
    const Along kHalf = along(kWalk, 5);
    RAWFRAME_EXPECT(near(kHalf.point, {5, 0, 0}) && near(kHalf.heading, {1, 0, 0}));
    RAWFRAME_EXPECT(near(along(kWalk, 25).point, {10, 0, 0}) && near(along(kWalk, -1).point, {0, 0, 0}));
}

RAWFRAME_TEST(APathPassesThroughEveryPointAndIsLongerThanItsChords) {
    const Spline kBend{.points = {{0, 0, 0}, {10, 0, 5}, {20, 1, 0}}};
    RAWFRAME_EXPECT(spans(kBend) == 2);
    for (std::size_t each = 0; each < kBend.points.size(); ++each) {
        RAWFRAME_EXPECT(near(at(kBend, static_cast<double>(each)), kBend.points[each]));
    }
    const double kChords = std::hypot(10.0, 5.0) + std::hypot(10.0, 1.0, 5.0);
    const Walk kWalk = walk(kBend);
    RAWFRAME_EXPECT(kWalk.length() > kChords && kWalk.length() < kChords * 1.1);
    // The same points, the same path, to the bit.
    RAWFRAME_EXPECT(walk(kBend).points == kWalk.points);
    // A point met twice still makes a path.
    const Walk kDoubled = walk(Spline{.points = {{0, 0, 0}, {0, 0, 0}, {4, 0, 0}}});
    RAWFRAME_EXPECT(std::isfinite(kDoubled.length()) && std::abs(kDoubled.length() - 4) < 1e-6);
}

RAWFRAME_TEST(AClosedPathComesBackAndWraps) {
    const Spline kLoop{.points = {{0, 0, 0}, {10, 0, 0}, {10, 0, 10}, {0, 0, 10}}, .closed = true};
    RAWFRAME_EXPECT(spans(kLoop) == 4 && near(at(kLoop, 4), kLoop.points[0]));
    const Walk kWalk = walk(kLoop);
    RAWFRAME_EXPECT(near(kWalk.points.back(), kWalk.points.front()));
    RAWFRAME_EXPECT(near(along(kWalk, kWalk.length() + 3, true).point, along(kWalk, 3, true).point, 1e-6));
    RAWFRAME_EXPECT(near(along(kWalk, -2, true).point, along(kWalk, kWalk.length() - 2, true).point, 1e-6));
    // Two points cannot close.
    RAWFRAME_EXPECT(spans(Spline{.points = {{0, 0, 0}, {1, 0, 0}}, .closed = true}) == 0);
}
