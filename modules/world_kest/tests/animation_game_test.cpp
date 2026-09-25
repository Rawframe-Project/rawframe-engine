// Games with animators (D124): an `animator` line's graph read beside the
// description, its clip and skeleton found by their sidecars, its parameter
// bound to the game's component by name, and entities played by the
// World's animation, all of them in a client's World and only the
// simulation's on a dedicated server.

#include "game_harness.h"
#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/test/test.h"
#include "rawframe/world/query.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_animation/registrar.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

constexpr base::Bits128 kSkeletonId{0, 0xa1};
constexpr base::Bits128 kSwingId{0, 0xa2};
constexpr base::Bits128 kGraphId{0, 0xa3};
constexpr base::Bits128 kRoot{1, 1};
constexpr std::uint64_t kTick = 0x6b1d2e3f40516273ULL;

std::string sidecar(base::Bits128 id) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(id, digits);
    return "{\n  \"schema\": 1,\n  \"resourceId\": \"" + std::string{digits.data(), digits.size()} +
           "\",\n  \"importer\": \"rawframe.animation\"\n}\n";
}

/// A game in a directory of its own: a pendulum's swing, one second long,
/// that ticks at its middle, played at the speed its entity's gait says.
/// `gait` is the program's parameter component.
std::filesystem::path writeGame(std::string_view name, std::string_view gait) {
    using namespace animation;
    const std::filesystem::path kDirectory =
        std::filesystem::temp_directory_path() / ("rawframe-" + std::string{name} + "-" + std::to_string(::getpid()));
    std::filesystem::remove_all(kDirectory);
    std::filesystem::create_directories(kDirectory);
    const Skeleton kRig{.bones = {Bone{.target = kRoot, .name = "root", .parent = std::nullopt, .bind = {}}}};
    const Clip kSwing{
        .skeleton = kSkeletonId,
        .duration = 1.0,
        .loop = Loop::Loop,
        .tracks = {Track{.bone = kRoot, .keys = {Key{.value = {}}, Key{.time = 0.75, .value = {1, 0, 0}}}}},
        .events = {ClipEvent{.event = kTick, .name = "tick", .time = 0.5}}};
    const Graph kGraph{.parameters = {Parameter{.name = "speed", .id = 1, .type = ParameterType::Float}},
                       .nodes = {GraphNode{.id = 1, .node = ClipNode{.clip = kSwingId, .speed = ParameterRef{1}}},
                                 GraphNode{.id = 2, .node = OutputNode{.pose = {.node = 1}}}},
                       .presentation = {}};
    // The documents where the sidecars say, in a directory of their own.
    std::filesystem::create_directories(kDirectory / "rig");
    writeText(kDirectory / "rig" / "pendulum.rfanim", *writeSkeleton(kRig));
    writeText(kDirectory / "rig" / "pendulum.rfanim.rfmeta", sidecar(kSkeletonId));
    writeText(kDirectory / "rig" / "swing.rfanim", *writeClip(kSwing));
    writeText(kDirectory / "rig" / "swing.rfanim.rfmeta", sidecar(kSwingId));
    writeText(kDirectory / "swinging.rfanim", *writeGraph(kGraph));
    writeText(kDirectory / "swinging.rfanim.rfmeta", sidecar(kGraphId));
    writeText(kDirectory / "swing.kest",
              "module swing\n\nstruct Gait {\n" + std::string{gait} +
                  "}\n\nfn hold(count: i32, gaits: [Gait]) {\n    let i = 0\n    while i < count {\n        i = i + 1\n"
                  "    }\n}\n");
    writeText(kDirectory / "swing.game",
              "program swing.kest\n"
              "component 1c0ffee0-0000-4000-8000-00000000a001 swing.gait Gait\n"
              "system swing.hold simulation hold read swing.gait\n"
              "animator 5a0000000000a001 swinging.rfanim parameters swing.gait\n"
              "spawn 1 swing.gait speed=1 rawframe.animation.animator graph=swinging.rfanim relevance=1\n"
              "spawn 1 swing.gait speed=1 rawframe.animation.animator graph=swinging.rfanim relevance=0\n"
              "spawn 1 swing.gait speed=0 rawframe.animation.animator graph=swinging.rfanim relevance=1\n");
    return kDirectory;
}

const std::array<composition::RegistrarEntry, 4> kAnimated = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

/// How many events each animator fired, in entity order, summed over
/// `ticks` ticks at 60 Hz, played in a composition for `role`; none when
/// the game does not start.
std::optional<std::vector<std::uint64_t>>
play(const std::filesystem::path& game, composition::TargetRole role, int ticks) {
    std::vector<composition::Problem> problems;
    auto plan = composition::compose(
        composition::CompositionRequest{
            .registrars = kAnimated, .role = role, .shutdownBudget = execution::MonotonicDuration::fromSeconds(1)},
        problems);
    RAWFRAME_EXPECT(plan.has_value());
    const std::string kText = "kest.game = " + (game / "swing.game").string() +
                              "\nworld.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n";
    const auto kConfiguration = composition::Configuration::parse(kText);
    execution::ManualClock clock;
    execution::CancellationScope root{clock};
    composition::Composition composition{
        *plan, composition::HostServices{.clock = &clock, .scope = &root, .configuration = &*kConfiguration}};
    if (!composition.start().has_value()) {
        composition.stop();
        simulation = nullptr;
        return std::nullopt;
    }
    std::vector<std::uint64_t> fired;
    for (int tick = 0; tick < ticks; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(
            composition::HostPhase::RunWorlds,
            composition::HostFrame{.iteration = static_cast<std::uint64_t>(tick), .now = clock.now()});
        world::World& world = *simulation->world();
        std::vector<std::pair<world::EntityHandle, std::uint32_t>> animators;
        auto query = world::Query<world::Read<world_animation::Animator>>::resolve(world.registry());
        query->forEach(world, [&animators](world::EntityHandle entity, const world_animation::Animator& animator) {
            animators.emplace_back(entity, animator.events);
        });
        std::ranges::sort(animators);
        fired.resize(animators.size());
        for (std::size_t at = 0; at < animators.size(); ++at) {
            fired[at] += animators[at].second;
        }
    }
    composition.stop();
    simulation = nullptr;
    return fired;
}

} // namespace

RAWFRAME_TEST(AGameAnimatesItsEntities) {
    const std::filesystem::path kGame = writeGame("animated", "    speed: f32\n");
    // Three seconds: the swing at full speed ticks at 0.5, 1.5, and 2.5;
    // the one at rest never gets there.
    const auto kClient = play(kGame, composition::TargetRole::Client, 180);
    RAWFRAME_EXPECT(kClient.has_value() && *kClient == (std::vector<std::uint64_t>{3, 3, 0}));
    // A dedicated server plays only the simulation's animators.
    const auto kServer = play(kGame, composition::TargetRole::DedicatedServer, 180);
    RAWFRAME_EXPECT(kServer.has_value() && *kServer == (std::vector<std::uint64_t>{3, 0, 0}));
    std::filesystem::remove_all(kGame);
}

RAWFRAME_TEST(AnAnimatorsParametersAreItsComponentsFields) {
    // A parameter with no field of its name, or one of another type, and the
    // game does not start.
    for (const std::string_view kGait : {"    pace: f32\n", "    speed: i32\n"}) {
        const std::filesystem::path kGame = writeGame("misanimated", kGait);
        RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::Client, 1).has_value());
        std::filesystem::remove_all(kGame);
    }
    // Nor without its clip beside it.
    const std::filesystem::path kGame = writeGame("unrigged", "    speed: f32\n");
    std::filesystem::remove(kGame / "rig" / "swing.rfanim.rfmeta");
    RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::Client, 1).has_value());
    std::filesystem::remove_all(kGame);
}
