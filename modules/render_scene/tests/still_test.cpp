// Whether a view's frames draw the same picture (D952): what changes with
// time alone leaves it still, anything drawn otherwise does not; a frame
// that moves with time is never still; and a temporal resolve or an
// adapting exposure waits until it has settled.

#include "rawframe/render_scene/still.h"
#include "rawframe/test/test.h"

#include <memory>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

SceneFrame aFrame(double seconds) {
    SceneFrame frame;
    frame.time = {seconds, seconds - (1.0 / 60.0)};
    frame.eye = {0, 2, 5};
    frame.temporal.jitter = {0.25F * static_cast<float>(seconds), 0};
    frame.particles.clock = static_cast<float>(seconds);
    frame.draws.push_back(SceneDraw{.mesh = 1, .material = 0});
    frame.materials.push_back(noMaterial());
    frame.programs.resize(1);
    return frame;
}

} // namespace

RAWFRAME_TEST(AFrameThatChangesWithTimeAloneStandsStill) {
    Stillness stillness;
    RAWFRAME_EXPECT(!stillness.still(aFrame(1)));
    RAWFRAME_EXPECT(stillness.still(aFrame(2)) && stillness.still(aFrame(3)) && stillness.stood() == 2);
    // A model moved, or the eye: drawn again, and counted afresh.
    SceneFrame moved = aFrame(4);
    moved.draws[0].model[12] = 1;
    RAWFRAME_EXPECT(!stillness.still(moved) && stillness.stood() == 0);
    RAWFRAME_EXPECT(stillness.still(moved));
    SceneFrame looked = moved;
    looked.eye[0] = 1;
    RAWFRAME_EXPECT(!stillness.still(looked));
}

RAWFRAME_TEST(AFrameThatMovesWithTimeIsNeverStill) {
    // An emitter's particles, a post process, or a model drawn with a
    // program material (which may read the World's time).
    SceneFrame emitting = aFrame(1);
    emitting.particles.emitters.emplace_back();
    SceneFrame processed = aFrame(1);
    processed.postProcesses.emplace_back();
    SceneFrame programmed = aFrame(1);
    programmed.programs[0] = std::make_shared<const material::ProgramMaterial>();
    for (const SceneFrame& kFrame : {emitting, processed, programmed}) {
        Stillness stillness;
        RAWFRAME_EXPECT(movesWithTime(kFrame));
        for (int frame = 0; frame < 4; ++frame) {
            RAWFRAME_EXPECT(!stillness.still(kFrame));
        }
    }
    // A program the game has that nothing draws moves nothing; a trail's
    // or beam's points are the frame's own.
    SceneFrame unused = aFrame(1);
    unused.programs.push_back(std::make_shared<const material::ProgramMaterial>());
    unused.particles.ribbons.emplace_back();
    RAWFRAME_EXPECT(!movesWithTime(unused));
}

RAWFRAME_TEST(AnEditedWorldStandsWhileTimeAloneMovesIt) {
    // D979: an editor's edited World, its emitters, post processes, and
    // program materials moving with time alone, stands; a change of its
    // own still draws.
    SceneFrame frame = aFrame(1);
    frame.particles.emitters.emplace_back();
    frame.postProcesses.emplace_back();
    frame.programs[0] = std::make_shared<const material::ProgramMaterial>();
    Stillness stillness;
    stillness.holdTime(true);
    RAWFRAME_EXPECT(!stillness.still(frame));
    for (int at = 2; at < 5; ++at) {
        SceneFrame later = frame;
        later.time = {static_cast<double>(at), at - (1.0 / 60.0)};
        later.particles.clock = static_cast<float>(at);
        RAWFRAME_EXPECT(stillness.still(later));
    }
    SceneFrame moved = frame;
    moved.eye = {1, 2, 5};
    RAWFRAME_EXPECT(!stillness.still(moved));
}

RAWFRAME_TEST(AResolvingOrAdaptingFrameWaitsUntilItSettles) {
    const auto kStoodAfter = [](SceneFrame frame) {
        Stillness stillness;
        for (int at = 0; at < 1000; ++at) {
            frame.time[0] += 1.0 / 60.0;
            if (stillness.still(frame)) {
                return at;
            }
        }
        return -1;
    };
    SceneFrame resolving = aFrame(1);
    resolving.temporal.enabled = true;
    SceneFrame adapting = aFrame(1);
    adapting.metering.enabled = true;
    RAWFRAME_EXPECT(kStoodAfter(aFrame(1)) == 1);
    RAWFRAME_EXPECT(kStoodAfter(resolving) == 8);
    RAWFRAME_EXPECT(kStoodAfter(adapting) == 180);
}
