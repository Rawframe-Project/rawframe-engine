// Gale's ships (D681): a player joined is given a ship that floats on the
// rolling sea at its draft, upright, rides out a list it is given, sails
// ahead at its throttle and turns at its rudder, and sinks from the World
// when its player goes, its player's own entity (D685); a second player's
// ship is launched beside the first. A ship's broadside holes the ship beside it until it founders,
// sinks, and its player is given another (D683). The test makes the
// players as admission would and sets their helms between ticks, as their
// clients' input would.

#include "game_harness.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <string>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

const std::array<composition::RegistrarEntry, 4> kGale = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

constexpr auto kHelmId = schema::ComponentTypeId::fromText("787f081f-15a5-42fc-bbeb-06b230674c63");
constexpr auto kShipId = schema::ComponentTypeId::fromText("b71d9c3f-8a0e-4a92-824d-73479a820d53");
constexpr auto kGunsId = schema::ComponentTypeId::fromText("91c03597-33c6-4c73-9ec6-5bdc9e4b356b");

/// controls.Helm, gale.Ship, and gale.Guns, as the program lays them out.
struct Helm {
    float throttle = 0;
    float rudder = 0;
    bool port = false;
    bool starboard = false;
};

struct Ship {
    bool launched = false;
    std::uint32_t hull = 0;
    std::uint32_t sinking = 0;
    std::uint32_t reload = 0;
    std::uint32_t fired = 0;
};

struct Guns {
    std::uint32_t hits = 0;
};

/// Gale, composed and started from its directory.
class PlayedGale : public PlayedGame {
public:
    PlayedGale()
        : PlayedGame(kGale,
                     "kest.game = " + std::string{RAWFRAME_SAMPLE_GAMES} +
                         "gale/gale.game\nworld.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n") {
    }
};

/// A player as admission makes one: its player's components, zeroed; it
/// is its own ship (D685).
world::EntityHandle joined(world::World& world) {
    const auto kMade = world.create();
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return {};
    }
    put(world, *kMade, kHelmId, Helm{});
    put(world, *kMade, kShipId, Ship{});
    put(world, *kMade, kGunsId, Guns{});
    put(world, *kMade, physics3d::Body3D::kComponentTypeId, physics3d::Body3D{});
    put(world, *kMade, physics3d::Pose3D::kComponentTypeId, physics3d::Pose3D{});
    put(world, *kMade, physics3d::Velocity3D::kComponentTypeId, physics3d::Velocity3D{});
    put(world, *kMade, physics3d::Impulse3D::kComponentTypeId, physics3d::Impulse3D{});
    return *kMade;
}

const physics3d::Pose3D& poseOf(world::World& world, world::EntityHandle entity) {
    return *valueOf<physics3d::Pose3D>(world, entity, physics3d::Pose3D::kComponentTypeId);
}

/// How far up the ship's own up points: 1 upright, 0 on its side.
double upright(const physics3d::Pose3D& pose) {
    return 1.0 - (2.0 * ((static_cast<double>(pose.qx) * pose.qx) + (static_cast<double>(pose.qz) * pose.qz)));
}

/// Where the ship's bow points about +Y, in radians from -Z, turning to
/// starboard (+X) positive.
double heading(const physics3d::Pose3D& pose) {
    // The ship's ahead, -Z turned by its rotation, flattened.
    const double kX = -2.0 * ((static_cast<double>(pose.qx) * pose.qz) + (static_cast<double>(pose.qw) * pose.qy));
    const double kZ =
        -(1.0 - (2.0 * ((static_cast<double>(pose.qx) * pose.qx) + (static_cast<double>(pose.qy) * pose.qy))));
    return std::atan2(kX, -kZ);
}

/// Whether the ship rides the sea as a hull should, the waves rolling under
/// it: its middle within half a meter of the still sea, upright within
/// about fifteen degrees.
bool afloat(const physics3d::Pose3D& pose) {
    return pose.y > -0.5 && pose.y < 0.6 && upright(pose) > 0.93;
}

} // namespace

RAWFRAME_TEST(AShipFloatsSailsAndTurnsAtItsHelm) {
    PlayedGale played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    const world::EntityHandle kPlayer = joined(world);
    played.run(3);
    const world::EntityHandle kShip = kPlayer;
    RAWFRAME_EXPECT(valueOf<Ship>(world, kShip, kShipId)->launched);
    // Launched first, at the row's first place, and settled at its draft
    // a few seconds on, riding the waves without sinking or leaping.
    RAWFRAME_EXPECT(std::abs(poseOf(world, kShip).x + 21.0) < 0.5);
    for (int second = 0; second < 6; ++second) {
        played.run(60);
        RAWFRAME_EXPECT(afloat(poseOf(world, kShip)));
    }
    // Given a list, it rights itself.
    auto* const kSpin = valueOf<physics3d::Velocity3D>(world, kShip, physics3d::Velocity3D::kComponentTypeId);
    kSpin->angularZ = 1.0F;
    played.run(10);
    RAWFRAME_EXPECT(upright(poseOf(world, kShip)) < 0.99);
    played.run(240);
    RAWFRAME_EXPECT(afloat(poseOf(world, kShip)));
    // Full ahead for ten seconds: it sails off toward -Z, its heading held.
    const physics3d::Pose3D kBefore = poseOf(world, kShip);
    Helm* const kHelm = valueOf<Helm>(world, kPlayer, kHelmId);
    kHelm->throttle = 1;
    played.run(600);
    const physics3d::Pose3D kAhead = poseOf(world, kShip);
    RAWFRAME_EXPECT(kBefore.z - kAhead.z > 30.0 && std::abs(kAhead.x - kBefore.x) < 5.0);
    RAWFRAME_EXPECT(afloat(kAhead) && std::abs(heading(kAhead)) < 0.2);
    // Hard to starboard: it turns, and still floats.
    kHelm->rudder = 1;
    played.run(180);
    const physics3d::Pose3D kTurned = poseOf(world, kShip);
    RAWFRAME_EXPECT(heading(kTurned) > 0.5 && heading(kTurned) < 1.6);
    RAWFRAME_EXPECT(afloat(kTurned));
    // A second player's ship is launched beside the first's place.
    const world::EntityHandle kSecond = joined(world);
    played.run(3);
    RAWFRAME_EXPECT(std::abs(poseOf(world, kSecond).x + 7.0) < 0.5);
    // The first player gone, its ship goes with it.
    RAWFRAME_EXPECT(world.destroy(kPlayer).has_value());
    played.run(3);
    RAWFRAME_EXPECT(holding(world, kShipId).size() == 1);
}

RAWFRAME_TEST(ABroadsideHolesAShipUntilItFoundersAndItsPlayerSailsAgain) {
    PlayedGale played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    const world::EntityHandle kGunner = joined(world);
    const world::EntityHandle kTarget = joined(world);
    played.run(120);
    const world::EntityHandle kFiring = kGunner;
    const world::EntityHandle kHoled = kTarget;
    // Fourteen meters apart abeam, whole and unhurt.
    RAWFRAME_EXPECT(valueOf<Ship>(world, kHoled, kShipId)->hull == 5);
    // The gunner fires to starboard, at the ship beside it, as fast as its
    // guns reload, until that ship founders.
    valueOf<Helm>(world, kGunner, kHelmId)->starboard = true;
    played.run(30);
    RAWFRAME_EXPECT(valueOf<Ship>(world, kHoled, kShipId)->hull == 4);
    RAWFRAME_EXPECT(valueOf<Guns>(world, kFiring, kGunsId)->hits == 1);
    for (int volley = 0; volley < 12 && valueOf<Ship>(world, kHoled, kShipId)->hull > 0; ++volley) {
        played.run(90);
    }
    RAWFRAME_EXPECT(valueOf<Ship>(world, kHoled, kShipId)->hull == 0);
    RAWFRAME_EXPECT(valueOf<Guns>(world, kFiring, kGunsId)->hits == 5);
    valueOf<Helm>(world, kGunner, kHelmId)->starboard = false;
    // It floods and goes down.
    played.run(200);
    RAWFRAME_EXPECT(poseOf(world, kHoled).y < -2.0);
    // Abandoned, it is launched again, whole, at the next place in the row,
    // its player sailing on.
    played.run(120);
    const Ship* const kAgain = valueOf<Ship>(world, kTarget, kShipId);
    RAWFRAME_EXPECT(kAgain->launched && kAgain->hull == 5 && kAgain->sinking == 0);
    RAWFRAME_EXPECT(std::abs(poseOf(world, kTarget).x - 7.0) < 0.5 && poseOf(world, kTarget).y > -1.0);
}
