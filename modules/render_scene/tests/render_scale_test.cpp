// A local player's view's render scale as its frames keep up with the
// device (D533): it shrinks while most frames certainly take well over
// their budget, by about the share of pixels that brings them within it,
// grows a step after windows of frames done well within it, tries a step
// more after windows of frames within it, waiting twice as long after a
// try that fails, and never leaves the bounds a client allows.

#include "rawframe/render_scene/render_scale.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace rawframe;
using render_scene::RenderScale;

namespace {

constexpr execution::MonotonicDuration kBudget{16'666'667};

execution::MonotonicDuration milliseconds(double value) {
    return execution::MonotonicDuration{static_cast<std::int64_t>(value * 1'000'000)};
}

/// `frames` frames each certainly taking `atLeast` and at most `atMost`
/// milliseconds; how many times the scale changed.
int pace(RenderScale& scale, int frames, double atLeast, double atMost) {
    int changes = 0;
    for (int frame = 0; frame < frames; ++frame) {
        changes += scale.paced(milliseconds(atLeast), milliseconds(atMost), kBudget) ? 1 : 0;
    }
    return changes;
}

/// A device whose frame takes `full` milliseconds at the whole scale,
/// less as the pixels drawn are fewer, seen by a Host iterating every
/// `iteration` milliseconds: certainly the iterations it was found still
/// running in, at most one more.
int device(RenderScale& scale, int frames, double full, double iteration) {
    int changes = 0;
    for (int frame = 0; frame < frames; ++frame) {
        const double kShare = static_cast<double>(scale.percent()) / 100.0;
        const double kTook = full * kShare * kShare;
        const auto kRunning = static_cast<int>(kTook / iteration);
        changes +=
            scale.paced(milliseconds(kRunning * iteration), milliseconds((kRunning + 1) * iteration), kBudget) ? 1 : 0;
    }
    return changes;
}

} // namespace

RAWFRAME_TEST(AScaleAClientAllowsNoLowerNeverChanges) {
    RenderScale scale{75, 75};
    RAWFRAME_EXPECT(!scale.follows());
    RAWFRAME_EXPECT(pace(scale, 64, 200, 210) == 0);
    RAWFRAME_EXPECT(scale.percent() == 75);
    RAWFRAME_EXPECT(scale.scale() == 0.75F);
    // A least above the most is the most.
    RenderScale above{60, 90};
    RAWFRAME_EXPECT(!above.follows());
    RAWFRAME_EXPECT(above.percent() == 60);
}

RAWFRAME_TEST(FramesOverTheirBudgetShrinkTheScaleToFitIt) {
    RenderScale scale{100, 25};
    RAWFRAME_EXPECT(scale.follows());
    RAWFRAME_EXPECT(scale.percent() == 100);
    // Seven frames are not a window yet.
    RAWFRAME_EXPECT(pace(scale, 7, 40, 45) == 0);
    RAWFRAME_EXPECT(scale.percent() == 100);
    // The eighth is: frames of 40 ms fit in a sixtieth of a second at
    // about 64 hundredths each way, a whole step down.
    RAWFRAME_EXPECT(pace(scale, 1, 40, 45) == 1);
    RAWFRAME_EXPECT(scale.percent() == 60);
    // Over, but not by a quarter: a display's wait, not the device.
    RAWFRAME_EXPECT(pace(scale, 64, 20, 21) == 0);
    RAWFRAME_EXPECT(scale.percent() == 60);
    // Just past a quarter over, about seven eighths each way.
    RAWFRAME_EXPECT(pace(scale, 8, 22, 25) == 1);
    RAWFRAME_EXPECT(scale.percent() == 50);
    // Never below the least a client allows.
    RAWFRAME_EXPECT(pace(scale, 8, 1000, 1010) == 1);
    RAWFRAME_EXPECT(scale.percent() == 25);
    RAWFRAME_EXPECT(pace(scale, 32, 1000, 1010) == 0);
    RAWFRAME_EXPECT(scale.percent() == 25);
}

RAWFRAME_TEST(AFewSlowFramesOrOnesThatMightBeFastChangeNothing) {
    RenderScale scale{100, 50};
    // Half a window over is not more than half.
    for (int window = 0; window < 4; ++window) {
        RAWFRAME_EXPECT(pace(scale, 4, 40, 45) == 0);
        RAWFRAME_EXPECT(pace(scale, 4, 5, 8) == 0);
    }
    RAWFRAME_EXPECT(scale.percent() == 100);
    // Frames found done only after a long wait, none found running past
    // their budget, are not known to be the device's: a Host iterating
    // slowly for its own reasons.
    RAWFRAME_EXPECT(pace(scale, 64, 0, 200) == 0);
    RAWFRAME_EXPECT(scale.percent() == 100);
}

RAWFRAME_TEST(RoomyWindowsGrowTheScaleAStepAtATime) {
    RenderScale scale{90, 25};
    RAWFRAME_EXPECT(pace(scale, 8, 1000, 1010) == 1);
    RAWFRAME_EXPECT(scale.percent() == 25);
    // Two roomy windows, then one with a frame not roomy: no growth.
    RAWFRAME_EXPECT(pace(scale, 16, 0, 5) == 0);
    RAWFRAME_EXPECT(pace(scale, 7, 0, 5) == 0);
    RAWFRAME_EXPECT(pace(scale, 1, 0, 12) == 0);
    RAWFRAME_EXPECT(scale.percent() == 25);
    // Three in a row grow it one step.
    RAWFRAME_EXPECT(pace(scale, 23, 0, 5) == 0);
    RAWFRAME_EXPECT(pace(scale, 1, 0, 5) == 1);
    RAWFRAME_EXPECT(scale.percent() == 30);
    // And on, never past the most.
    RAWFRAME_EXPECT(pace(scale, 24 * 20, 0, 5) == 12);
    RAWFRAME_EXPECT(scale.percent() == 90);
}

RAWFRAME_TEST(ADeviceTooSlowForTheWholeScaleSettles) {
    // A frame of 60 ms at the whole scale, seen by a Host iterating at 120
    // Hz: the scale falls once to where frames fit, then tries steps up
    // while no frame is a quarter over, rarely past the last that held.
    RenderScale scale{100, 25};
    RAWFRAME_EXPECT(device(scale, 8, 60, 1000.0 / 120) == 1);
    RAWFRAME_EXPECT(scale.percent() == 50);
    const int kChanges = device(scale, 2000, 60, 1000.0 / 120);
    RAWFRAME_EXPECT(kChanges <= 12);
    RAWFRAME_EXPECT(scale.percent() == 60);
    // The same device fast enough grows back to the whole scale.
    RenderScale fast{100, 25};
    RAWFRAME_EXPECT(device(fast, 8, 400, 1000.0 / 120) == 1);
    RAWFRAME_EXPECT(device(fast, 24 * 40, 4, 1000.0 / 120) > 0);
    RAWFRAME_EXPECT(fast.percent() == 100);
}

/// A device whose frame takes `full` milliseconds at the whole scale, its
/// frames waiting for a 60 Hz display's next refresh: done at the first
/// after their work, seen running half a refresh before (a Host iterating
/// at 120 Hz).
int displayed(RenderScale& scale, int frames, double full, std::uint32_t& least, std::uint32_t& most) {
    constexpr double kRefresh = 1000.0 / 60;
    int changes = 0;
    for (int frame = 0; frame < frames; ++frame) {
        const double kShare = static_cast<double>(scale.percent()) / 100.0;
        const double kDone = std::ceil(full * kShare * kShare / kRefresh) * kRefresh;
        changes += scale.paced(milliseconds(kDone - kRefresh / 2), milliseconds(kDone), kBudget) ? 1 : 0;
        least = std::min(least, scale.percent());
        most = std::max(most, scale.percent());
    }
    return changes;
}

RAWFRAME_TEST(ADisplaysWaitIsNeverSeenAsRoomOrAsTheDevicesTime) {
    // A fast device under vsync: every frame done at the next refresh.
    RenderScale fast{100, 25};
    std::uint32_t least = 100;
    std::uint32_t most = 0;
    RAWFRAME_EXPECT(displayed(fast, 4000, 4, least, most) == 0);
    RAWFRAME_EXPECT(fast.percent() == 100);
    // A device that needs two refreshes a frame at the whole scale falls to
    // where one does, then tries a step more after a while, back again
    // when it does not hold, ever more patiently.
    RenderScale slow{100, 25};
    RAWFRAME_EXPECT(displayed(slow, 8, 30, least, most) == 1);
    RAWFRAME_EXPECT(slow.percent() == 80);
    RAWFRAME_EXPECT(displayed(slow, 8, 30, least, most) == 1);
    RAWFRAME_EXPECT(slow.percent() == 65);
    least = 100;
    most = 0;
    const int kChanges = displayed(slow, 16000, 30, least, most);
    RAWFRAME_EXPECT(least >= 65 && most <= 75);
    RAWFRAME_EXPECT(kChanges >= 2 && kChanges <= 16);
    RAWFRAME_EXPECT(slow.percent() == 70);
}
