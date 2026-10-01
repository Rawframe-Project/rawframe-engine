// Particle emitters in the view stage (D352): an emitter spawns at its rate
// on the particle clock, its particles born evenly across the frame, and a
// burst each time its count moves; its ring keeps going around, and
// starts anew at another size (D353); emitters out of view are left
// alone, the nearest are kept up to the limit and drawn farthest first,
// and values past a limit point are held and counted; an emitter gone
// keeps nothing.

#include "rawframe/physics3d/components.h"
#include "rawframe/render_scene/scene.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string_view>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

constexpr auto kModelId = schema::ComponentTypeId::fromText("3c8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");
constexpr auto kEmitterId = schema::ComponentTypeId::fromText("7d8e1f52-7d04-4a2b-9e61-0f5a2c7d3b18");

template <typename T> schema::ComponentDescriptor plain(schema::ComponentTypeId id, std::string_view name) {
    return schema::ComponentDescriptor{
        .id = id, .name = name, .size = sizeof(T), .alignment = alignof(T), .plainData = true};
}

std::shared_ptr<const schema::SchemaRegistry> registry() {
    schema::RegistryBuilder builder;
    builder.add(plain<Model>(kModelId, "test.model"));
    builder.add(plain<ParticleEmitter>(kEmitterId, "test.emitter"));
    builder.add<physics3d::Pose3D>();
    return *builder.freeze();
}

/// A fountain: a hundred a second, living a second, a tenth of a meter.
ParticleEmitter fountain() {
    return ParticleEmitter{.rate = 100, .lifetime = 1, .speed = 2, .spread = 0.3F, .sizeStart = 0.1F, .sizeEnd = 0.1F};
}

struct Rig {
    std::shared_ptr<const schema::SchemaRegistry> schema = registry();
    world::World world{schema};
    std::unique_ptr<Scene> scene;

    explicit Rig(SceneLimits limits = {}) {
        scene = *Scene::create(*schema, {.models = {kModelId}, .emitters = {kEmitterId}, .limits = limits});
    }

    world::EntityHandle place(ParticleEmitter emitter, double z) {
        const world::EntityHandle kEntity = *world.create();
        RAWFRAME_EXPECT(world.insertErased(kEntity, *schema->find(kEmitterId), &emitter).has_value());
        RAWFRAME_EXPECT(
            world.insert(kEntity, *schema->key<physics3d::Pose3D>(), physics3d::Pose3D{.z = z}).has_value());
        return kEntity;
    }

    void change(world::EntityHandle entity, const ParticleEmitter& emitter) {
        auto* held = static_cast<ParticleEmitter*>(world.getErased(entity, *schema->find(kEmitterId)));
        RAWFRAME_EXPECT(held != nullptr);
        if (held != nullptr) {
            *held = emitter;
        }
    }

    const SceneFrame& frame(float elapsed) {
        scene->extract(world);
        return scene->queue({.fovY = 1.2F, .aspect = 1, .elapsed = elapsed});
    }
};

} // namespace

RAWFRAME_TEST(AnEmitterSpawnsAtItsRateAndBursts) {
    Rig rig;
    ParticleEmitter emitter = fountain();
    emitter.burstCount = 5;
    const world::EntityHandle kFountain = rig.place(emitter, -5);
    // Its first frame owes nothing yet.
    const SceneFrame& kFirst = rig.frame(0);
    RAWFRAME_EXPECT(kFirst.emitters.size() == 1 && kFirst.emitters[0].spawned == 0);
    RAWFRAME_EXPECT(kFirst.emitters.size() == 1 && kFirst.emitters[0].capacity == 105);
    // A tenth of a second: ten, born a hundredth apart across it.
    const SceneFrame& kSecond = rig.frame(0.1F);
    RAWFRAME_EXPECT(kSecond.emitters.size() == 1);
    if (kSecond.emitters.size() == 1) {
        const SceneEmitter& kMade = kSecond.emitters[0];
        RAWFRAME_EXPECT(kMade.spawned == 10 && kMade.steady == 10 && kMade.first == 0);
        RAWFRAME_EXPECT(std::abs(kMade.step - 0.01F) < 1e-6F && std::abs(kMade.born - 0.0F) < 1e-6F);
        RAWFRAME_EXPECT(std::abs(kSecond.particleClock - 0.1F) < 1e-6F);
        RAWFRAME_EXPECT(std::abs(kMade.anchor[2] + 5) < 1e-6F && kMade.direction[1] == 1 && kMade.seed != 0);
        RAWFRAME_EXPECT(kMade.colorStart == (std::array<float, 4>{1, 1, 1, 1}));
    }
    // Its count moved twice: two bursts of five beside the steady ten.
    emitter.bursts = 2;
    rig.change(kFountain, emitter);
    const SceneFrame& kThird = rig.frame(0.1F);
    RAWFRAME_EXPECT(kThird.emitters.size() == 1 && kThird.emitters[0].spawned == 20 &&
                    kThird.emitters[0].steady == 10 && kThird.emitters[0].first == 10);
    // The ring goes around: after 105, the next spawn starts again near
    // its start.
    for (int frame = 0; frame < 7; ++frame) {
        static_cast<void>(rig.frame(0.1F));
    }
    const SceneFrame& kAround = rig.frame(0.1F);
    RAWFRAME_EXPECT(kAround.emitters.size() == 1 && kAround.emitters[0].first == (30 + (70 * 1)) % 105);
    // Its ring the same one all along; another size starts another, which
    // a device's ring follows (D353).
    RAWFRAME_EXPECT(kAround.emitters.size() == 1 && kAround.emitters[0].ring == 1);
    emitter.rate = 50;
    rig.change(kFountain, emitter);
    const SceneFrame& kSmaller = rig.frame(0.1F);
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
    static_cast<void>(rig.frame(0));
    const SceneFrame& kFrame = rig.frame(0.1F);
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
    const world::EntityHandle kFountain = rig.place(emitter, -5);
    static_cast<void>(rig.frame(0));
    static_cast<void>(rig.frame(0.1F));
    // Gone, then back: new, so its count of bursts is not a burst.
    RAWFRAME_EXPECT(rig.world.destroy(kFountain).has_value());
    RAWFRAME_EXPECT(rig.frame(0.1F).emitters.empty());
    rig.place(emitter, -5);
    const SceneFrame& kBack = rig.frame(0.1F);
    RAWFRAME_EXPECT(kBack.emitters.size() == 1 && kBack.emitters[0].first == 0 && kBack.emitters[0].spawned == 10);
}
