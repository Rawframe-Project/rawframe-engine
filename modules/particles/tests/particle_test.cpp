// Particle emitters in a view's accounting (D352, D357): an emitter spawns
// at its rate on the particle clock, its particles born evenly across the
// frame, and a burst each time its count moves; its ring keeps going
// around, and starts anew at another size (D353); emitters out of view are
// left alone, the nearest are kept up to the limit and drawn farthest
// first, and values past a limit point are held and counted; an emitter
// gone keeps nothing; and its particles inherit its velocity, up to the
// limit (D359).

#include "rawframe/particles/particles.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace rawframe;
using namespace rawframe::particles;

namespace {

/// A fountain: a hundred a second, living a second, a tenth of a meter.
ParticleEmitter fountain() {
    return ParticleEmitter{.rate = 100, .lifetime = 1, .speed = 2, .spread = 0.3F, .sizeStart = 0.1F, .sizeEnd = 0.1F};
}

/// One view looking down -Z from the World's origin: it sees what reaches
/// before the eye.
struct Rig {
    Particles particles;
    Frame frame;
    Limits limits;
    std::vector<EmitterInstance> emitters;
    std::uint32_t entities = 0;
    Viewer viewer{.sees = [](const Vector& center, float radius) {
        return center[2] < radius;
    }};

    explicit Rig(Limits held = {}) : limits(held) {
    }

    std::size_t place(ParticleEmitter emitter, double z) {
        emitters.push_back(EmitterInstance{
            .entity = {.slot = ++entities, .generation = 1}, .emitter = emitter, .position = {0, 0, z}});
        return emitters.size() - 1;
    }

    const Frame& update(float elapsed) {
        particles.update(frame, emitters, {}, {}, viewer, {}, limits, elapsed);
        return frame;
    }
};

} // namespace

RAWFRAME_TEST(AnEmitterSpawnsAtItsRateAndBursts) {
    Rig rig;
    ParticleEmitter emitter = fountain();
    emitter.burstCount = 5;
    const std::size_t kFountain = rig.place(emitter, -5);
    // Its first frame owes nothing yet.
    const Frame& kFirst = rig.update(0);
    RAWFRAME_EXPECT(kFirst.emitters.size() == 1 && kFirst.emitters[0].spawned == 0);
    RAWFRAME_EXPECT(kFirst.emitters.size() == 1 && kFirst.emitters[0].capacity == 105);
    // A tenth of a second: ten, born a hundredth apart across it.
    const Frame& kSecond = rig.update(0.1F);
    RAWFRAME_EXPECT(kSecond.emitters.size() == 1);
    if (kSecond.emitters.size() == 1) {
        const EmitterDraw& kMade = kSecond.emitters[0];
        RAWFRAME_EXPECT(kMade.spawned == 10 && kMade.steady == 10 && kMade.first == 0);
        RAWFRAME_EXPECT(std::abs(kMade.step - 0.01F) < 1e-6F && std::abs(kMade.born - 0.0F) < 1e-6F);
        RAWFRAME_EXPECT(std::abs(kSecond.clock - 0.1F) < 1e-6F);
        RAWFRAME_EXPECT(std::abs(kMade.anchor[2] + 5) < 1e-6F && kMade.direction[1] == 1 && kMade.seed != 0);
        RAWFRAME_EXPECT(kMade.colorStart == (std::array<float, 4>{1, 1, 1, 1}));
    }
    // Its count moved twice: two bursts of five beside the steady ten.
    emitter.bursts = 2;
    rig.emitters[kFountain].emitter = emitter;
    const Frame& kThird = rig.update(0.1F);
    RAWFRAME_EXPECT(kThird.emitters.size() == 1 && kThird.emitters[0].spawned == 20 &&
                    kThird.emitters[0].steady == 10 && kThird.emitters[0].first == 10);
    // The ring goes around: after 105, the next spawn starts again near
    // its start.
    for (int frame = 0; frame < 7; ++frame) {
        static_cast<void>(rig.update(0.1F));
    }
    const Frame& kAround = rig.update(0.1F);
    RAWFRAME_EXPECT(kAround.emitters.size() == 1 && kAround.emitters[0].first == (30 + (70 * 1)) % 105);
    // Its ring the same one all along; another size starts another, which
    // a device's ring follows (D353).
    RAWFRAME_EXPECT(kAround.emitters.size() == 1 && kAround.emitters[0].ring == 1);
    emitter.rate = 50;
    rig.emitters[kFountain].emitter = emitter;
    const Frame& kSmaller = rig.update(0.1F);
    RAWFRAME_EXPECT(kSmaller.emitters.size() == 1 && kSmaller.emitters[0].ring == 2 && kSmaller.emitters[0].first == 0);
}

RAWFRAME_TEST(EmittersAreKeptByTheViewAndTheLimits) {
    Rig rig({.maximumEmitters = 2});
    // Before the eye, nearer and farther; behind it; one not sound; one
    // asking past the limit points.
    rig.place(fountain(), -5);
    rig.place(fountain(), -20);
    rig.place(fountain(), 30);
    ParticleEmitter broken = fountain();
    broken.speed = std::numeric_limits<float>::quiet_NaN();
    rig.place(broken, -6);
    ParticleEmitter greedy = fountain();
    greedy.rate = 5000;
    greedy.lifetime = 60;
    rig.place(greedy, -10);
    static_cast<void>(rig.update(0));
    const Frame& kFrame = rig.update(0.1F);
    // The nearest two in view kept, the farthest first; the third in view
    // past the limit and the unsound left out; the greedy held.
    RAWFRAME_EXPECT(kFrame.emitters.size() == 2 && kFrame.emittersLeftOut == 2 && kFrame.emittersHeld == 1);
    if (kFrame.emitters.size() == 2) {
        RAWFRAME_EXPECT(kFrame.emitters[0].anchor[2] < kFrame.emitters[1].anchor[2]);
        RAWFRAME_EXPECT(std::abs(kFrame.emitters[1].anchor[2] + 5) < 1e-6F);
        RAWFRAME_EXPECT(std::abs(kFrame.emitters[0].anchor[2] + 10) < 1e-6F);
        // Held to a thousand a second, twenty seconds, and its ring to the
        // limit.
        RAWFRAME_EXPECT(kFrame.emitters[0].spawned == 100 && kFrame.emitters[0].lifetime == 20 &&
                        kFrame.emitters[0].capacity == 4096);
    }
}

RAWFRAME_TEST(AnEmitterGoneKeepsNothing) {
    Rig rig;
    ParticleEmitter emitter = fountain();
    emitter.bursts = 3;
    emitter.burstCount = 4;
    const std::size_t kFountain = rig.place(emitter, -5);
    static_cast<void>(rig.update(0));
    static_cast<void>(rig.update(0.1F));
    // Gone, then back: new, so its count of bursts is not a burst.
    rig.emitters.erase(rig.emitters.begin() + static_cast<std::ptrdiff_t>(kFountain));
    RAWFRAME_EXPECT(rig.update(0.1F).emitters.empty());
    rig.place(emitter, -5);
    const Frame& kBack = rig.update(0.1F);
    RAWFRAME_EXPECT(kBack.emitters.size() == 1 && kBack.emitters[0].first == 0 && kBack.emitters[0].spawned == 10);
}

RAWFRAME_TEST(AnEmittersParticlesInheritItsVelocity) {
    Rig rig;
    ParticleEmitter emitter = fountain();
    emitter.inherit = 0.5F;
    emitter.columns = 4;
    emitter.rows = 0;
    const std::size_t kFountain = rig.place(emitter, -5);
    // Where it was not drawn the frame before, it gives nothing.
    const Frame& kFirst = rig.update(0.1F);
    RAWFRAME_EXPECT(kFirst.emitters.size() == 1 && kFirst.emitters[0].inherited == (Vector{0, 0, 0}));
    // A meter to the right in a tenth of a second: half of ten meters a
    // second; its flipbook four cells across, one down.
    rig.emitters[kFountain].position[0] = 1;
    const Frame& kMoved = rig.update(0.1F);
    RAWFRAME_EXPECT(kMoved.emitters.size() == 1 && kMoved.emittersHeld == 0);
    if (kMoved.emitters.size() == 1) {
        RAWFRAME_EXPECT(std::abs(kMoved.emitters[0].inherited[0] - 5) < 1e-3F && kMoved.emitters[0].inherited[1] == 0);
        RAWFRAME_EXPECT(kMoved.emitters[0].columns == 4 && kMoved.emitters[0].rows == 1);
    }
    // A teleport inherits no more than the limit, and is held.
    rig.emitters[kFountain].emitter.inherit = 1;
    rig.emitters[kFountain].position[0] = 101;
    const Frame& kFlung = rig.update(0.1F);
    RAWFRAME_EXPECT(kFlung.emitters.size() == 1 && kFlung.emittersHeld == 1);
    if (kFlung.emitters.size() == 1) {
        RAWFRAME_EXPECT(std::abs(kFlung.emitters[0].inherited[0] - 100) < 1e-3F);
    }
}
