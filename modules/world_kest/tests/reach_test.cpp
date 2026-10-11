// Reach's hands (D656): a player joined is seated round the table and given
// two hands, kinematic balls that go where its input holds them; a hand
// that grips above a block takes it on a joint, lifts it, and throws it
// when it opens moving, knocking as it lands (D660); a second player sits
// a quarter turn round; and a player gone takes its hands with it. The test
// makes the players as admission would and sets their hands between ticks,
// as their clients' input would.

#include "game_harness.h"
#include "rawframe/physics3d/components.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <string>
#include <vector>

using namespace rawframe;
using namespace rawframe::game_test;

namespace {

const std::array<composition::RegistrarEntry, 4> kReach = {
    kRegistrars[0],
    kRegistrars[1],
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"test", &registerWatcher, world_runtime::kScopes}};

constexpr auto kHandsId = schema::ComponentTypeId::fromText("dced10fa-8c5c-4fde-98c4-769f8565275c");
constexpr auto kSeatId = schema::ComponentTypeId::fromText("aa174e17-8efd-4de0-b82e-bd5b6d42d638");
constexpr auto kHandId = schema::ComponentTypeId::fromText("8c4cd7c9-b5ff-4560-8edd-1dc2b3795509");
constexpr auto kBlockId = schema::ComponentTypeId::fromText("ea827a4e-3a16-42dc-b120-eb28ea24c609");
constexpr auto kGripId = schema::ComponentTypeId::fromText("84064735-82e5-4725-90d0-39e682d09191");
constexpr auto kKnockId = schema::ComponentTypeId::fromText("a0fd2692-1aa3-4429-bca5-5f8b4e743eff");

/// controls.Hands, reach.Seat, reach.Hand, and reach.Block, as the program
/// lays them out.
struct Hands {
    float leftX = 0;
    float leftY = 0;
    float leftZ = 0;
    float leftGrip = 0;
    float rightX = 0;
    float rightY = 0;
    float rightZ = 0;
    float rightGrip = 0;
};

struct Seat {
    double x = 0;
    double z = 0;
    double heading = 0;
    bool placed = false;
};

struct Hand {
    world::EntityHandle player;
    std::uint8_t side = 0;
    bool holding = false;
    world::EntityHandle held;
};

struct Block {
    bool held = false;
    world::EntityHandle holder;
    std::uint32_t grips = 0;
};

/// rawframe.sound.Emitter.
struct Emitter {
    std::uint64_t sound = 0;
    std::uint32_t cue = 0;
    bool playing = false;
    std::uint8_t despawn = 0;
};

/// Reach, composed and started from its directory.
class PlayedReach : public PlayedGame {
public:
    PlayedReach()
        : PlayedGame(kReach,
                     "kest.game = " + std::string{RAWFRAME_SAMPLE_GAMES} +
                         "reach/reach.game\nworld.tick_rate = 60\nworld.maximum_ticks_per_iteration = 1\n") {
    }
};

/// A player as admission makes one: its player's components, zeroed.
world::EntityHandle joined(world::World& world) {
    const auto kMade = world.create();
    RAWFRAME_EXPECT(kMade.has_value());
    if (!kMade.has_value()) {
        return {};
    }
    put(world, *kMade, kHandsId, Hands{});
    put(world, *kMade, kSeatId, Seat{});
    return *kMade;
}

const physics3d::Pose3D& poseOf(world::World& world, world::EntityHandle entity) {
    return *valueOf<physics3d::Pose3D>(world, entity, physics3d::Pose3D::kComponentTypeId);
}

bool near(const physics3d::Pose3D& pose, double x, double y, double z, double within) {
    return std::abs(pose.x - x) < within && std::abs(pose.y - y) < within && std::abs(pose.z - z) < within;
}

/// The player's hand on `side`.
world::EntityHandle handOf(world::World& world, world::EntityHandle player, std::uint8_t side) {
    for (const world::EntityHandle kHand : holding(world, kHandId)) {
        const Hand* const kFound = valueOf<Hand>(world, kHand, kHandId);
        if (kFound->player == player && kFound->side == side) {
            return kHand;
        }
    }
    return {};
}

} // namespace

RAWFRAME_TEST(AHandTakesABlockLiftsItAndThrowsIt) {
    PlayedReach played;
    RAWFRAME_EXPECT(played.started());
    if (!played.started()) {
        return;
    }
    world::World& world = *simulation->world();
    const world::EntityHandle kPlayer = joined(world);
    played.run(3);
    // Seated before the table, facing its middle, with two hands at rest
    // above the table's edge.
    const Seat* const kSeat = valueOf<Seat>(world, kPlayer, kSeatId);
    RAWFRAME_EXPECT(kSeat->placed && std::abs(kSeat->x) < 1e-6 && std::abs(kSeat->z - 0.9) < 1e-6);
    RAWFRAME_EXPECT(holding(world, kHandId).size() == 2);
    const world::EntityHandle kRight = handOf(world, kPlayer, 1);
    RAWFRAME_EXPECT(!kRight.isNull());
    if (kRight.isNull()) {
        return;
    }
    RAWFRAME_EXPECT(near(poseOf(world, kRight), 0.2, 0.95, 0.55, 0.01));
    // It hears from a seated eye above its seat (D660).
    RAWFRAME_EXPECT(near(poseOf(world, kPlayer), 0, 1.2, 0.9, 1e-6));
    // The right hand just above the middle block of the near row: 0.15
    // from the table's middle, 0.75 ahead of the seat.
    Hands* const kHands = valueOf<Hands>(world, kPlayer, kHandsId);
    *kHands = Hands{.rightX = 0, .rightY = 0.88F, .rightZ = -0.75F};
    played.run(30);
    RAWFRAME_EXPECT(near(poseOf(world, kRight), 0, 0.88, 0.15, 0.01));
    RAWFRAME_EXPECT(holding(world, kGripId).empty());
    // Gripped: the block is taken, once, by this hand.
    kHands->rightGrip = 1;
    played.run(3);
    RAWFRAME_EXPECT(holding(world, kGripId).size() == 1);
    const Hand* const kHeld = valueOf<Hand>(world, kRight, kHandId);
    RAWFRAME_EXPECT(kHeld->holding);
    const world::EntityHandle kBlock = kHeld->held;
    const Block* const kTaken = valueOf<Block>(world, kBlock, kBlockId);
    RAWFRAME_EXPECT(kTaken != nullptr && kTaken->held && kTaken->holder == kRight && kTaken->grips == 1);
    // Lifted half a meter, it hangs below the hand.
    kHands->rightY = 1.4F;
    played.run(40);
    RAWFRAME_EXPECT(poseOf(world, kBlock).y > 1.2 && near(poseOf(world, kBlock), 0, 1.3, 0.15, 0.15));
    // Swung away across the table and let go: it flies on, and falls.
    for (int step = 1; step <= 6; ++step) {
        kHands->rightZ = -0.75F - (0.1F * static_cast<float>(step));
        played.run(1);
    }
    // Held, it moves as the hand does, a decimeter a tick.
    RAWFRAME_EXPECT(near(poseOf(world, kBlock), 0, 1.3, -0.45, 0.15));
    // Opened as it still swings.
    kHands->rightZ = -1.45F;
    kHands->rightGrip = 0;
    played.run(2);
    RAWFRAME_EXPECT(holding(world, kGripId).empty() && !valueOf<Hand>(world, kRight, kHandId)->holding);
    RAWFRAME_EXPECT(!valueOf<Block>(world, kBlock, kBlockId)->held);
    const auto* kFlying = valueOf<physics3d::Velocity3D>(world, kBlock, physics3d::Velocity3D::kComponentTypeId);
    RAWFRAME_EXPECT(kFlying->z < -4.0F);
    const std::uint32_t kKnocked = valueOf<Emitter>(world, kBlock, kKnockId)->cue;
    played.run(90);
    RAWFRAME_EXPECT(poseOf(world, kBlock).z < -0.6);
    // It knocked as it landed (D660).
    const Emitter* const kKnock = valueOf<Emitter>(world, kBlock, kKnockId);
    RAWFRAME_EXPECT(kKnock->sound == 0x3494a617bc371640 && kKnock->cue > kKnocked);
    // A second player sits a quarter turn round, at the table's +X side.
    const world::EntityHandle kSecond = joined(world);
    played.run(3);
    const Seat* const kOther = valueOf<Seat>(world, kSecond, kSeatId);
    RAWFRAME_EXPECT(kOther->placed && std::abs(kOther->x - 0.9) < 1e-6 && std::abs(kOther->z) < 1e-6);
    RAWFRAME_EXPECT(holding(world, kHandId).size() == 4);
    // The first gone, its hands go with it.
    RAWFRAME_EXPECT(world.destroy(kPlayer).has_value());
    played.run(3);
    RAWFRAME_EXPECT(holding(world, kHandId).size() == 2);
}
