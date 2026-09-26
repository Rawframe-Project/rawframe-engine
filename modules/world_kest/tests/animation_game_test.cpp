// Games with animators (D127): an `animator` line's graph read beside the
// description, its clip and skeleton found by their sidecars, its parameter
// bound to the game's component by name, and entities played by the
// World's animation, all of them in a client's World and only the
// simulation's on a dedicated server. A program reads their bones and
// events through rawframe.animation's doors, which answer nothing past the
// bones and events there are.

#include "game_harness.h"
#include "rawframe/animation/clip.h"
#include "rawframe/animation/graph.h"
#include "rawframe/animation/mask.h"
#include "rawframe/animation/skeleton.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/scratch.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_animation/registrar.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

constexpr base::Bits128 kSkeletonId{0, 0xa1};
constexpr base::Bits128 kSwingId{0, 0xa2};
constexpr base::Bits128 kGraphId{0, 0xa3};
constexpr base::Bits128 kHeldId{0, 0xa4};
constexpr base::Bits128 kRoot{1, 1};
constexpr std::uint64_t kTick = 0x6b1d2e3f40516273ULL;

std::string sidecar(base::Bits128 id) {
    std::array<char, base::kBits128HexDigits> digits{};
    base::formatBits128Hex(id, digits);
    return "{\n  \"schema\": 1,\n  \"resourceId\": \"" + std::string{digits.data(), digits.size()} +
           "\",\n  \"importer\": \"rawframe.animation\"\n}\n";
}

/// A game in a directory of its own: a pendulum's swing, one second long,
/// that reaches a meter along x at three quarters and ticks at its middle,
/// played at the speed its entity's gait says (tests/game/swing.kest, its
/// speed field declared as `speed`), a dedicated server posing only the
/// bones its `held` mask holds.
std::filesystem::path writeGame(std::string_view name, std::string_view speed) {
    using namespace animation;
    const std::filesystem::path kDirectory = test::scratchDirectory(name);
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
    writeText(kDirectory / "rig" / "held.rfanim", *writeMask(Mask{.skeleton = kSkeletonId, .chains = {{kRoot}}}));
    writeText(kDirectory / "rig" / "held.rfanim.rfmeta", sidecar(kHeldId));
    writeText(kDirectory / "swinging.rfanim", *writeGraph(kGraph));
    writeText(kDirectory / "swinging.rfanim.rfmeta", sidecar(kGraphId));
    std::string program = readText(std::filesystem::path{RAWFRAME_WORLD_KEST_GAMES} / "swing.kest");
    program.replace(program.find("speed: f32"), 10, speed);
    writeText(kDirectory / "swing.kest", program);
    writeText(kDirectory / "swing.game",
              "program swing.kest\n"
              "component 1c0ffee0-0000-4000-8000-00000000a001 swing.gait Gait\n"
              "system swing.follow simulation follow write swing.gait entities after rawframe.animation.step\n"
              "animator 5a0000000000a001 swinging.rfanim parameters swing.gait subset "
              "000000000000000000000000000000a4\n"
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

/// swing.Gait as the program lays it out.
struct Gait {
    float speed = 0;
    double reach = 0;
    std::uint32_t ticks = 0;
    std::uint32_t strays = 0;
};

/// One entity's animation after a run: the events its Animator said were
/// fired, summed over the steps, and its gait.
struct Played {
    std::uint64_t events = 0;
    Gait gait;
};

/// Each entity, in entity order, played for `ticks` ticks at 60 Hz in a
/// composition for `role`; none when the game does not start.
std::optional<std::vector<Played>> play(const std::filesystem::path& game, composition::TargetRole role, int ticks) {
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
    composition::Composition composition{*plan,
                                         composition::HostServices{.clock = &clock,
                                                                   .scope = &root,
                                                                   .cpu = &test::cpuExecutor(),
                                                                   .blockingIo = &test::blockingIoExecutor(),
                                                                   .configuration = &*kConfiguration}};
    if (!composition.start().has_value()) {
        composition.stop();
        simulation = nullptr;
        return std::nullopt;
    }
    std::vector<Played> played;
    for (int tick = 0; tick < ticks; ++tick) {
        clock.advance(execution::MonotonicDuration{16'666'667});
        composition.runHostPhase(
            composition::HostPhase::RunWorlds,
            composition::HostFrame{.iteration = static_cast<std::uint64_t>(tick), .now = clock.now()});
        world::World& world = *simulation->world();
        const auto kGait =
            world.registry().find(schema::ComponentTypeId::fromText("1c0ffee0-0000-4000-8000-00000000a001"));
        const auto kAnimator = world.registry().find(world_animation::Animator::kComponentTypeId);
        const std::array<world::ColumnTerm, 2> kTerms = {world::ColumnTerm{*kAnimator, world::Access::Read},
                                                         world::ColumnTerm{*kGait, world::Access::Read}};
        std::vector<std::pair<world::EntityHandle, Played>> now;
        world::ColumnQuery::resolve(kTerms, world.registry())
            ->forEachChunk(world, [&now](const world::ColumnChunk& chunk) {
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    Played each;
                    world_animation::Animator animator;
                    std::memcpy(&animator, chunk.columns[0] + (row * sizeof animator), sizeof animator);
                    std::memcpy(&each.gait, chunk.columns[1] + (row * sizeof each.gait), sizeof each.gait);
                    each.events = animator.events;
                    now.emplace_back(chunk.entities[row], each);
                }
            });
        std::ranges::sort(now, {}, &std::pair<world::EntityHandle, Played>::first);
        played.resize(now.size());
        for (std::size_t at = 0; at < now.size(); ++at) {
            played[at] = Played{.events = played[at].events + now[at].second.events, .gait = now[at].second.gait};
        }
    }
    composition.stop();
    simulation = nullptr;
    return played;
}

} // namespace

RAWFRAME_TEST(AGameAnimatesItsEntities) {
    const std::filesystem::path kGame = writeGame("animated", "speed: f32");
    // Half a second: the swing at full speed is two thirds of the way to
    // its reach and has not ticked; the one at rest is where it started.
    const auto kHalf = play(kGame, composition::TargetRole::Client, 30);
    RAWFRAME_EXPECT(kHalf.has_value() && kHalf->size() == 3);
    if (kHalf.has_value() && kHalf->size() == 3) {
        RAWFRAME_EXPECT(std::abs((*kHalf)[0].gait.reach - (2.0 / 3.0)) < 1e-9 && (*kHalf)[0].gait.ticks == 0);
        RAWFRAME_EXPECT((*kHalf)[2].gait.reach == 0);
    }
    // Three seconds: the swing at full speed ticks at 0.5, 1.5, and 2.5,
    // and the program saw each; the one at rest never gets there.
    const auto kClient = play(kGame, composition::TargetRole::Client, 180);
    RAWFRAME_EXPECT(kClient.has_value() && kClient->size() == 3);
    if (kClient.has_value() && kClient->size() == 3) {
        for (const std::uint64_t kSeen : {0, 1, 2}) {
            const Played& each = (*kClient)[kSeen];
            RAWFRAME_EXPECT(each.events == (kSeen < 2 ? 3U : 0U) && each.gait.ticks == each.events);
            RAWFRAME_EXPECT(each.gait.strays == 0);
        }
    }
    // A dedicated server plays only the simulation's animators; the other
    // has no pose to ask about.
    const auto kServer = play(kGame, composition::TargetRole::DedicatedServer, 180);
    RAWFRAME_EXPECT(kServer.has_value() && kServer->size() == 3);
    if (kServer.has_value() && kServer->size() == 3) {
        RAWFRAME_EXPECT((*kServer)[0].events == 3 && (*kServer)[1].events == 0 && (*kServer)[2].events == 0);
        RAWFRAME_EXPECT((*kServer)[1].gait.reach == 0 && (*kServer)[1].gait.ticks == 0);
        RAWFRAME_EXPECT((*kServer)[0].gait.strays == 0 && (*kServer)[1].gait.strays == 0);
    }
    std::filesystem::remove_all(kGame);
}

RAWFRAME_TEST(AnAnimatorsParametersAreItsComponentsFields) {
    // A parameter with no field of its name, or one of another type, and the
    // game does not start.
    for (const std::string_view kSpeed : {"pace: f32", "speed: i32"}) {
        const std::filesystem::path kGame = writeGame("misanimated", kSpeed);
        RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::Client, 1).has_value());
        std::filesystem::remove_all(kGame);
    }
    // Nor without its clip beside it.
    const std::filesystem::path kGame = writeGame("unrigged", "speed: f32");
    std::filesystem::remove(kGame / "rig" / "swing.rfanim.rfmeta");
    RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::Client, 1).has_value());
    std::filesystem::remove_all(kGame);
}

RAWFRAME_TEST(AnAnimatorsSubsetIsAMaskOfItsSkeleton) {
    const std::filesystem::path kGame = writeGame("subset", "speed: f32");
    const std::string kDescription = readText(kGame / "swing.game");
    const std::string kLine = "parameters swing.gait subset 000000000000000000000000000000a4";
    // Options out of their order, one twice, a subset of no identity, and
    // a word past them: the game does not start.
    for (const std::string_view kWritten : {"subset 000000000000000000000000000000a4 parameters swing.gait",
                                            "parameters swing.gait subset 000000000000000000000000000000a4 subset "
                                            "000000000000000000000000000000a4",
                                            "parameters swing.gait subset 00000000000000000000000000000000",
                                            "parameters swing.gait subset a4",
                                            "parameters swing.gait subset 000000000000000000000000000000a4 held"}) {
        std::string written = kDescription;
        written.replace(written.find(kLine), kLine.size(), kWritten);
        writeText(kGame / "swing.game", written);
        RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::DedicatedServer, 1).has_value());
    }
    writeText(kGame / "swing.game", kDescription);
    RAWFRAME_EXPECT(play(kGame, composition::TargetRole::DedicatedServer, 1).has_value());
    // Nor with a mask of another skeleton, or none where it names.
    const std::string kHeld = readText(kGame / "rig" / "held.rfanim");
    writeText(kGame / "rig" / "held.rfanim",
              *animation::writeMask(animation::Mask{.skeleton = kSwingId, .chains = {{kRoot}}}));
    RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::DedicatedServer, 1).has_value());
    writeText(kGame / "rig" / "held.rfanim", kHeld);
    std::filesystem::remove(kGame / "rig" / "held.rfanim.rfmeta");
    RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::DedicatedServer, 1).has_value());
    std::filesystem::remove_all(kGame);
}

RAWFRAME_TEST(AGamesRootMotionMovesItsEntityNotItsPose) {
    // The pendulum's root taking its x as root motion, and a fourth swing
    // asking for it: two thirds of a second in, that swing has ticked as
    // the others have, but its bone is where its entity is, while theirs
    // reach eight ninths of a meter.
    const std::filesystem::path kGame = writeGame("rooted", "speed: f32");
    animation::Skeleton rig = *animation::readSkeleton(readText(kGame / "rig" / "pendulum.rfanim"));
    rig.rootMotion = animation::RootMotionSource{.translation = {true, false, false}};
    writeText(kGame / "rig" / "pendulum.rfanim", *animation::writeSkeleton(rig));
    writeText(kGame / "swing.game",
              readText(kGame / "swing.game") +
                  "spawn 1 swing.gait speed=1 rawframe.animation.animator graph=swinging.rfanim relevance=1 "
                  "rawframe.animation.root_motion\n");
    const auto kPlayed = play(kGame, composition::TargetRole::Client, 40);
    RAWFRAME_EXPECT(kPlayed.has_value() && kPlayed->size() == 4);
    if (kPlayed.has_value() && kPlayed->size() == 4) {
        RAWFRAME_EXPECT(std::abs((*kPlayed)[0].gait.reach - (8.0 / 9.0)) < 1e-9 && (*kPlayed)[0].gait.ticks == 1);
        RAWFRAME_EXPECT((*kPlayed)[3].gait.reach == 0.0 && (*kPlayed)[3].gait.ticks == 1);
    }
    std::filesystem::remove_all(kGame);
}

RAWFRAME_TEST(AGamesClipsAnimateItsComponentsFields) {
    // The swing's clip also steps the gait's strays to seven at a quarter
    // second: half a second in, each swing playing has it, and the one at
    // rest has not reached it.
    const std::filesystem::path kGame = writeGame("tallied", "speed: f32");
    const auto kWithField = [&kGame](std::string_view field, animation::Channel channel) {
        animation::Clip swing = *animation::readClip(readText(kGame / "rig" / "swing.rfanim"));
        swing.tracks.push_back(animation::Track{
            .channel = channel,
            .keys = {animation::Key{.value = {}, .interpolation = animation::Interpolation::Step},
                     animation::Key{
                         .time = 0.25, .value = {7, 0, 0, 0}, .interpolation = animation::Interpolation::Step}},
            .property = animation::PropertyBinding{
                .component = schema::ComponentTypeId::fromText("1c0ffee0-0000-4000-8000-00000000a001").value,
                .field = std::string{field}}});
        writeText(kGame / "rig" / "swing.rfanim", *animation::writeClip(swing));
    };
    const std::string kSwing = readText(kGame / "rig" / "swing.rfanim");
    kWithField("strays", animation::Channel::Discrete);
    const auto kPlayed = play(kGame, composition::TargetRole::Client, 30);
    RAWFRAME_EXPECT(kPlayed.has_value() && kPlayed->size() == 3);
    if (kPlayed.has_value() && kPlayed->size() == 3) {
        RAWFRAME_EXPECT((*kPlayed)[0].gait.strays == 7 && (*kPlayed)[1].gait.strays == 7);
        RAWFRAME_EXPECT((*kPlayed)[2].gait.strays == 0);
    }
    // A field the component lacks, or one of a kind the track cannot play,
    // and the game does not start.
    for (const auto& [kField, kChannel] : std::vector<std::pair<std::string_view, animation::Channel>>{
             {"stray", animation::Channel::Discrete}, {"reach", animation::Channel::Discrete}}) {
        writeText(kGame / "rig" / "swing.rfanim", kSwing);
        kWithField(kField, kChannel);
        RAWFRAME_EXPECT(!play(kGame, composition::TargetRole::Client, 1).has_value());
    }
    std::filesystem::remove_all(kGame);
}
