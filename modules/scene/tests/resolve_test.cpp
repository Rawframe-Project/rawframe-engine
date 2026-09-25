// Instances resolved: a platform prefab instanced twice in a level, each
// copy moved, changed, and renamed through its mapping; the level instanced
// again in a world; and every way an instance can fail to fit its source.

#include "rawframe/scene/errors.h"
#include "rawframe/scene/resolve.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene;

namespace {

base::Bits128 id(std::uint64_t low) {
    return base::Bits128{.high = 0x1000, .low = low};
}

FieldValue number(std::string text) {
    return FieldValue{.kind = FieldValue::Kind::Number, .number = std::move(text)};
}

FieldValue entity(std::uint64_t low) {
    return FieldValue{.kind = FieldValue::Kind::Entity, .entity = id(low)};
}

const base::Bits128 kPlatformScene{.high = 1, .low = 1};
const base::Bits128 kLevelScene{.high = 1, .low = 2};

/// A platform and a marker linked to it.
Scene platform() {
    return Scene{
        .schema = {{.component = "game.body", .mark = 1},
                   {.component = "game.link", .mark = 2},
                   {.component = "game.pose", .mark = 3}},
        .entities = {{.id = id(1),
                      .name = "platform",
                      .components = {{.name = "game.body", .fields = {{.name = "width", .value = number("3")}}},
                                     {.name = "game.pose", .fields = {{.name = "y", .value = number("3")}}}}},
                     {.id = id(2),
                      .name = "marker",
                      .components = {{.name = "game.link", .fields = {{.name = "to", .value = entity(1)}}}}}}};
}

/// The ground, and two platforms: one moved left, one moved right and
/// lowered to the default, its marker's link removed and a pose added.
Scene level() {
    Scene made{
        .schema = {{.component = "game.body", .mark = 1},
                   {.component = "game.link", .mark = 2},
                   {.component = "game.pose", .mark = 3}},
        .entities = {{.id = id(10),
                      .name = "ground",
                      .components = {{.name = "game.body", .fields = {{.name = "width", .value = number("40")}}}}}}};
    made.instances.push_back(
        SceneInstance{.scene = kPlatformScene,
                      .entities = {{.source = id(1), .instance = id(11)}, {.source = id(2), .instance = id(12)}},
                      .overrides = {{.entity = id(11),
                                     .component = "game.pose",
                                     .kind = Override::Kind::Set,
                                     .fields = {{.name = "x", .value = number("-15")}}}}});
    made.instances.push_back(SceneInstance{
        .scene = kPlatformScene,
        .entities = {{.source = id(1), .instance = id(21)}, {.source = id(2), .instance = id(22)}},
        .overrides = {{.entity = id(21),
                       .component = "game.pose",
                       .kind = Override::Kind::Set,
                       .fields = {{.name = "x", .value = number("15")}, {.name = "y", .value = number("0")}}},
                      {.entity = id(22), .component = "game.link", .kind = Override::Kind::Remove, .fields = {}},
                      {.entity = id(22),
                       .component = "game.pose",
                       .kind = Override::Kind::Add,
                       .fields = {{.name = "x", .value = number("1")}}}}});
    return made;
}

SceneSource sources(std::map<base::Bits128, Scene> scenes) {
    return [scenes = std::move(scenes)](base::Bits128 scene) -> result::Result<Scene> {
        const auto kFound = scenes.find(scene);
        if (kFound == scenes.end()) {
            return result::fail(
                result::ErrorClass::NotFound, kSceneDomain, code(SceneError::SceneInvalid), "no such scene");
        }
        return kFound->second;
    };
}

bool unfit(const result::Result<Scene>& outcome) {
    return !outcome.has_value() && outcome.error().domain() == kSceneDomain &&
           outcome.error().code() == code(SceneError::InstanceInvalid);
}

} // namespace

RAWFRAME_TEST(InstancesResolveThroughTheirMappingAndPatch) {
    const SceneSource kSources = sources({{kPlatformScene, platform()}});
    const auto kResolved = resolveInstances(level(), kSources);
    RAWFRAME_EXPECT(kResolved.has_value());
    if (!kResolved.has_value()) {
        return;
    }
    const std::vector<SceneEntity>& entities = kResolved->entities;
    RAWFRAME_EXPECT(kResolved->instances.empty() && entities.size() == 5);
    if (entities.size() != 5) {
        return;
    }
    // The ground, then each copy in its source's order, renamed.
    RAWFRAME_EXPECT(entities[0].id == id(10) && entities[1].id == id(11) && entities[2].id == id(12) &&
                    entities[3].id == id(21) && entities[4].id == id(22));
    RAWFRAME_EXPECT(entities[1].name == "platform" && entities[2].name == "marker");
    // Left: moved, still three meters up.
    const std::vector<SceneField> kLeftPose = {{.name = "x", .value = number("-15")},
                                               {.name = "y", .value = number("3")}};
    RAWFRAME_EXPECT(entities[1].components[1].name == "game.pose" && entities[1].components[1].fields == kLeftPose);
    // Its marker's link follows the mapping to the left platform.
    RAWFRAME_EXPECT(entities[2].components[0].fields[0].value.entity == id(11));
    // Right: moved, and set to the default height, which is no field.
    const std::vector<SceneField> kRightPose = {{.name = "x", .value = number("15")}};
    RAWFRAME_EXPECT(entities[3].components[1].fields == kRightPose);
    // Its marker: the link removed, a pose added.
    RAWFRAME_EXPECT(entities[4].components.size() == 1 && entities[4].components[0].name == "game.pose");
    // The schema is what the entities use.
    RAWFRAME_EXPECT(kResolved->schema.size() == 3);

    // The level instanced in turn: every entity it brings mapped again.
    Scene world{.schema = {}, .entities = {}};
    SceneInstance whole{.scene = kLevelScene, .entities = {}, .overrides = {}};
    for (const std::uint64_t kSource : {10, 11, 12, 21, 22}) {
        whole.entities.push_back({.source = id(kSource), .instance = id(100 + kSource)});
    }
    world.instances.push_back(whole);
    const auto kWorld = resolveInstances(world, sources({{kPlatformScene, platform()}, {kLevelScene, level()}}));
    RAWFRAME_EXPECT(kWorld.has_value() && kWorld->entities.size() == 5 && kWorld->entities[2].id == id(112) &&
                    kWorld->entities[2].components[0].fields[0].value.entity == id(111));
}

RAWFRAME_TEST(AnInstanceThatDoesNotFitItsSourceIsRefused) {
    const SceneSource kSources = sources({{kPlatformScene, platform()}});
    const auto kChanged = [&kSources](auto change) {
        Scene scene = level();
        change(scene);
        return resolveInstances(scene, kSources);
    };
    // The mapping: one short, or one the source has not.
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.instances[0].entities.pop_back();
    })));
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.instances[0].entities[1].source = id(3);
    })));
    // The patch: set or remove what is not there, add what is.
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.instances[1].overrides[1].component = "game.body";
    })));
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.instances[0].overrides[0].kind = Override::Kind::Add;
    })));
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.instances[0].overrides[0].entity = id(12);
    })));
    // Two layouts of one component.
    RAWFRAME_EXPECT(unfit(kChanged([](Scene& scene) {
        scene.schema[0].mark = 9;
    })));
    // A source that cannot be had is its source's error.
    const auto kMissing = kChanged([](Scene& scene) {
        scene.instances[0].scene = base::Bits128{.high = 7, .low = 7};
    });
    RAWFRAME_EXPECT(!kMissing.has_value() && kMissing.error().code() == code(SceneError::SceneInvalid));

    // A scene that instances itself, however far down.
    Scene loop{.schema = {}, .entities = {}};
    loop.instances.push_back(SceneInstance{.scene = kLevelScene, .entities = {}, .overrides = {}});
    RAWFRAME_EXPECT(unfit(resolveInstances(loop, sources({{kLevelScene, loop}}))));
    // Seventeen scenes deep.
    std::map<base::Bits128, Scene> chain;
    for (std::uint64_t depth = 0; depth < 18; ++depth) {
        Scene link{.schema = {}, .entities = {}};
        if (depth > 0) {
            link.instances.push_back(
                SceneInstance{.scene = base::Bits128{.high = 2, .low = depth - 1}, .entities = {}, .overrides = {}});
        }
        chain.emplace(base::Bits128{.high = 2, .low = depth}, std::move(link));
    }
    RAWFRAME_EXPECT(unfit(resolveInstances(chain.at(base::Bits128{.high = 2, .low = 17}), sources(chain))));
    RAWFRAME_EXPECT(resolveInstances(chain.at(base::Bits128{.high = 2, .low = 15}), sources(chain)).has_value());
}

RAWFRAME_TEST(AnInstanceMayRemoveItsEntitiesButNotWhatIsStillNamed) {
    const SceneSource kSources = sources({{kPlatformScene, platform()}});
    // The first copy's marker removed whole: its platform stays.
    Scene removed = level();
    removed.instances[0].overrides.insert(removed.instances[0].overrides.begin() + 1,
                                          Override{.entity = id(12), .kind = Override::Kind::Remove});
    const auto kResolved = resolveInstances(removed, kSources);
    RAWFRAME_EXPECT(kResolved.has_value() && kResolved->entities.size() == 4);
    if (kResolved.has_value()) {
        RAWFRAME_EXPECT(!std::ranges::contains(kResolved->entities, id(12), &SceneEntity::id));
    }
    // It reads back as written.
    const auto kWritten = writeScene(removed);
    RAWFRAME_EXPECT(kWritten.has_value() && kWritten->find("\"remove\": true") != std::string::npos);
    if (kWritten.has_value()) {
        const auto kRead = readScene(*kWritten);
        RAWFRAME_EXPECT(kRead.has_value() && *kRead == removed);
    }
    // The platform removed while its marker still links to it: refused.
    Scene named = level();
    named.instances[0].overrides = {Override{.entity = id(11), .kind = Override::Kind::Remove}};
    RAWFRAME_EXPECT(unfit(resolveInstances(named, kSources)));
    // A removal is its entity's only entry.
    Scene crowded = level();
    crowded.instances[0].overrides.insert(crowded.instances[0].overrides.begin(),
                                          Override{.entity = id(11), .kind = Override::Kind::Remove});
    RAWFRAME_EXPECT(!writeScene(crowded).has_value());
}
