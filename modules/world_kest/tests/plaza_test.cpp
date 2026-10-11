// Plaza's lamps (D599): a walker joined is given a lamp, an entity of its
// own, which hangs at its right side, turned as its view is, and moves to
// its player's right hand while a headset's controller holds it: the
// hand's place from where the head began, turned with the view, set at the
// walker's head. A lamp whose walker is gone goes out. The test makes the
// walker as admission would and moves its stick between ticks, as its
// client's input would.

#include "game_harness.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/executors.h"
#include "rawframe/test/test.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world_animation/components.h"
#include "rawframe/world_animation/registrar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

const std::array<composition::RegistrarEntry, 5> kPlaza = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

constexpr auto kStickId = schema::ComponentTypeId::fromText("43f8c60f-1131-4732-8bed-d758dc5b5fa7");
constexpr auto kGaitId = schema::ComponentTypeId::fromText("c2c0913c-ceaf-42d9-9ff2-2368f8cfa1dd");
constexpr auto kLampId = schema::ComponentTypeId::fromText("3db8ed2c-629a-4e0d-bf0f-b043f215b135");

/// controls.Stick, as the program lays it out.
struct Stick {
    float runX = 0;
    float runZ = 0;
    float jump = 0;
    float heading = 0;
    float handX = 0;
    float handY = 0;
    float handZ = 0;
    float holding = 0;
};

/// plaza.Gait and plaza.Lamp.
struct Gait {
    double pace = 0;
};

struct Lamp {
    world::EntityHandle walker;
};

/// Plaza's files, held as a host holds what it fetched (D167), with its
/// meshes and their bodies left out: meshes are read cooked, and lamps
/// need none.
const composition::HeldFiles& plazaFiles() {
    static const composition::HeldFiles kHeld = [] {
        const std::string kDirectory = std::string{RAWFRAME_SAMPLE_GAMES} + "plaza/";
        std::vector<composition::HeldFiles::File> files;
        for (const std::string& path : test::filesUnder(kDirectory, "")) {
            std::string text = test::readFile(kDirectory + path);
            if (path == "plaza.game") {
                std::string kept;
                std::size_t from = 0;
                while (from < text.size()) {
                    const std::size_t kEnd = std::min(text.find('\n', from), text.size());
                    const std::string_view kLine{text.data() + from, kEnd - from};
                    if (!kLine.starts_with("mesh ") && !kLine.contains("rawframe.physics3d.mesh mesh=")) {
                        kept.append(kLine).push_back('\n');
                    }
                    from = kEnd + 1;
                }
                text = std::move(kept);
            }
            const auto kBytes = std::as_bytes(std::span{text.data(), text.size()});
            files.emplace_back("game/" + path, std::vector<std::byte>{kBytes.begin(), kBytes.end()});
        }
        return *composition::HeldFiles::of(std::move(files));
    }();
    return kHeld;
}

/// Plaza, composed and started on its files held in memory.
class PlayedPlaza : public PlayedGame {
public:
    PlayedPlaza()
        : PlayedGame(kPlaza,
                     "kest.game = game/plaza.game\nworld.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n",
                     &plazaFiles()) {
    }
};

} // namespace

RAWFRAME_TEST(AWalkersLampHangsAtItsSideAndFollowsItsHand) {
    PlayedPlaza played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    // A walker as admission makes one: its player's components, zeroed.
    const auto kMade = world.create();
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return;
    }
    const world::EntityHandle kWalker = *kMade;
    put(world, kWalker, kStickId, Stick{});
    put(world, kWalker, kGaitId, Gait{});
    put(world, kWalker, physics3d::Body3D::kComponentTypeId, physics3d::Body3D{});
    put(world, kWalker, physics3d::Pose3D::kComponentTypeId, physics3d::Pose3D{});
    put(world, kWalker, physics3d::Velocity3D::kComponentTypeId, physics3d::Velocity3D{});
    put(world, kWalker, physics3d::Character3D::kComponentTypeId, physics3d::Character3D{});
    put(world, kWalker, world_animation::Animator::kComponentTypeId, world_animation::Animator{});
    put(world, kWalker, world_animation::RootMotion::kComponentTypeId, world_animation::RootMotion{});
    played.run(3);
    std::vector<world::EntityHandle> lamps = holding(world, kLampId);
    RAWFRAME_EXPECT(lamps.size() == 1);
    if (lamps.size() != 1) {
        return;
    }
    const world::EntityHandle kLamp = lamps[0];
    RAWFRAME_EXPECT(valueOf<Lamp>(world, kLamp, kLampId)->walker == kWalker);
    // Where the lamp is against its walker, after a tick more.
    const auto kOffset = [&] {
        played.run(1);
        const auto* walker = valueOf<physics3d::Pose3D>(world, kWalker, physics3d::Pose3D::kComponentTypeId);
        const auto* lamp = valueOf<physics3d::Pose3D>(world, kLamp, physics3d::Pose3D::kComponentTypeId);
        return std::array<double, 3>{lamp->x - walker->x, lamp->y - walker->y, lamp->z - walker->z};
    };
    const auto kNear = [](std::array<double, 3> at, double x, double y, double z) {
        return std::abs(at[0] - x) < 0.05 && std::abs(at[1] - y) < 0.05 && std::abs(at[2] - z) < 0.05;
    };
    // At its right side, 0.3 meters along +X while the view looks along -Z.
    RAWFRAME_EXPECT(kNear(kOffset(), 0.3, -0.1, 0));
    // The view turned a quarter turn left, toward -X: its right is -Z.
    valueOf<Stick>(world, kWalker, kStickId)->heading = 1.5707964F;
    RAWFRAME_EXPECT(kNear(kOffset(), 0, -0.1, -0.3));
    // Held, half a meter ahead and 0.2 to the right of where the head
    // began and 0.3 below it: ahead of the turned view is -X.
    *valueOf<Stick>(world, kWalker, kStickId) =
        Stick{.heading = 1.5707964F, .handX = 0.2F, .handY = -0.3F, .handZ = -0.5F, .holding = 1};
    RAWFRAME_EXPECT(kNear(kOffset(), -0.5, 0.3, -0.2));
    // Let go: at its side again.
    valueOf<Stick>(world, kWalker, kStickId)->holding = 0;
    RAWFRAME_EXPECT(kNear(kOffset(), 0, -0.1, -0.3));
    // The walker gone, its lamp goes out.
    RAWFRAME_EXPECT(world.destroy(kWalker).has_value());
    played.run(3);
    RAWFRAME_EXPECT(holding(world, kLampId).empty());
}
