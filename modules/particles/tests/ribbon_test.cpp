// Trails and beams in a view's accounting (D354, D357): a trail leaves a
// point each time its pose has gone its spacing, runs from its pose through
// them narrowing over their life, and lets go of the dead; a beam follows
// its bent curve in its segments, its texture repeated along its length;
// ribbons out of view are left alone, the nearest kept up to the limit and
// drawn farthest first, and values past a limit point held and counted.

#include "rawframe/particles/particles.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace rawframe;
using namespace rawframe::particles;

namespace {

bool near(float a, float b) {
    return std::abs(a - b) < 1e-4F;
}

/// One view looking down -Z from the World's origin: it sees what reaches
/// before the eye.
struct Rig {
    Particles particles;
    Frame frame;
    Limits limits;
    std::vector<TrailInstance> trails;
    std::vector<BeamInstance> beams;
    std::uint32_t entities = 0;
    Viewer viewer{.sees = [](const Vector& center, float radius) {
        return center[2] < radius;
    }};

    explicit Rig(Limits held = {}) : limits(held) {
    }

    void place(Trail trail, double x, double z) {
        trails.push_back(
            TrailInstance{.entity = {.slot = ++entities, .generation = 1}, .ribbon = trail, .position = {x, 0, z}});
    }

    void place(Beam beam, double x, double z) {
        beams.push_back(
            BeamInstance{.entity = {.slot = ++entities, .generation = 1}, .ribbon = beam, .position = {x, 0, z}});
    }

    const Frame& update(float elapsed) {
        particles.update(frame, {}, trails, beams, viewer, {}, limits, elapsed);
        return frame;
    }
};

/// A trail living a second, a point every half meter, a fifth of a meter
/// wide narrowing to nothing, white fading out.
Trail streak() {
    return Trail{.lifetime = 1, .spacing = 0.5F, .widthStart = 0.2F, .widthEnd = 0, .colorEnd = 0xFFFFFF00};
}

/// A beam four meters right, bent a meter up, in four segments, its
/// texture repeated every two meters.
Beam arc() {
    return Beam{.toX = 4, .bendY = 1, .segments = 4, .widthStart = 0.2F, .widthEnd = 0.1F, .textureLength = 2};
}

} // namespace

RAWFRAME_TEST(ATrailLeavesPointsAndLetsThemGo) {
    Rig rig;
    rig.place(streak(), 0, -5);
    // Standing still, a point where it stands: nothing to draw.
    RAWFRAME_EXPECT(rig.update(0).ribbons.empty());
    // Gone a meter: from the pose to where it was.
    rig.trails[0].position[0] = 1;
    const Frame& kMoved = rig.update(0.1F);
    RAWFRAME_EXPECT(kMoved.ribbons.size() == 1 && kMoved.ribbonPoints.size() == 2);
    if (kMoved.ribbonPoints.size() == 2) {
        const RibbonPoint& kHead = kMoved.ribbonPoints[0];
        const RibbonPoint& kTail = kMoved.ribbonPoints[1];
        RAWFRAME_EXPECT(near(kHead.place[0], 1) && near(kHead.place[2], -5) && near(kHead.width, 0.2F));
        RAWFRAME_EXPECT(near(kHead.along, 0) && near(kHead.color[3], 1));
        RAWFRAME_EXPECT(near(kTail.place[0], 0) && near(kTail.width, 0.18F) && near(kTail.along, 0.1F));
        RAWFRAME_EXPECT(near(kTail.color[3], 0.9F));
    }
    // A quarter meter more leaves no point; the head follows.
    rig.trails[0].position[0] = 1.25;
    const Frame& kFollowed = rig.update(0.1F);
    RAWFRAME_EXPECT(kFollowed.ribbonPoints.size() == 3 && near(kFollowed.ribbonPoints[0].place[0], 1.25F));
    // A second after it was left, the first point is gone; a second after
    // the other, nothing is left to draw.
    for (int frame = 0; frame < 7; ++frame) {
        static_cast<void>(rig.update(0.1F));
    }
    const Frame& kOlder = rig.update(0.1F);
    RAWFRAME_EXPECT(kOlder.ribbons.size() == 1 && kOlder.ribbonPoints.size() == 2);
    RAWFRAME_EXPECT(rig.update(0.1F).ribbons.empty());
}

RAWFRAME_TEST(ABeamFollowsItsCurve) {
    Rig rig;
    rig.place(arc(), -2, -6);
    const Frame& kFrame = rig.update(0);
    RAWFRAME_EXPECT(kFrame.ribbons.size() == 1 && kFrame.ribbonPoints.size() == 5);
    if (kFrame.ribbonPoints.size() == 5) {
        const RibbonPoint& kMiddle = kFrame.ribbonPoints[2];
        RAWFRAME_EXPECT(near(kMiddle.place[0], 0) && near(kMiddle.place[1], 1) && near(kMiddle.place[2], -6));
        RAWFRAME_EXPECT(near(kMiddle.width, 0.15F) && near(kFrame.ribbonPoints[4].place[0], 2));
        // Along its length, a repeat every two meters: longer than straight.
        RAWFRAME_EXPECT(near(kFrame.ribbonPoints[0].along, 0) && kFrame.ribbonPoints[4].along > 2);
    }
}

RAWFRAME_TEST(RibbonsAreKeptByTheViewAndTheLimits) {
    Rig rig({.maximumRibbons = 2, .maximumBeamSegments = 8});
    // Before the eye, nearer and farther; behind it; one not sound; one cut
    // past the limit; one with nothing to show.
    rig.place(arc(), -2, -5);
    rig.place(arc(), -2, -20);
    rig.place(arc(), -2, 30);
    Beam broken = arc();
    broken.toX = std::numeric_limits<float>::quiet_NaN();
    rig.place(broken, -2, -6);
    Beam fine = arc();
    fine.segments = 100;
    rig.place(fine, -2, -10);
    rig.place(Beam{}, -2, -7);
    const Frame& kFrame = rig.update(0);
    // The nearest two kept, the farthest first; the third in view past the
    // limit and the unsound left out; the fine one held to eight.
    RAWFRAME_EXPECT(kFrame.ribbons.size() == 2 && kFrame.ribbonsLeftOut == 2 && kFrame.ribbonsHeld == 1);
    if (kFrame.ribbons.size() == 2) {
        RAWFRAME_EXPECT(kFrame.ribbons[0].count == 9 && kFrame.ribbons[1].count == 5);
        RAWFRAME_EXPECT(kFrame.ribbons[1].first == 9);
        RAWFRAME_EXPECT(near(kFrame.ribbonPoints[9].place[2], -5));
    }
}
