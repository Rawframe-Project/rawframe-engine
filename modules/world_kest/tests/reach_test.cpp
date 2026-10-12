// Reach's hands (D656): a player joined is seated round the table and given
// two hands, kinematic balls that go where its input holds them; a hand
// that grips above a block takes it on a joint, lifts it, and throws it
// when it opens moving, knocking as it lands (D660); a second player sits
// a quarter turn round; and a player gone takes its hands with it. The test
// makes the players as admission would and sets their hands between ticks,
// as their clients' input would. A client draws its own hands where its
// input holds them, and a block one holds with it (D665).

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
constexpr auto kPlacedId = schema::ComponentTypeId::fromText("ef26db88-b0b6-4651-a471-831d28226752");

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

/// rawframe.model.Placed.
struct Placed {
    bool placed = false;
    double x = 0;
    double y = 0;
    double z = 0;
};

const std::array<composition::RegistrarEntry, 5> kReachPresented = {
    kReach[0],
    kReach[1],
    kReach[2],
    kReach[3],
    composition::RegistrarEntry{"test_plan", &registerPlanWatcher, world_runtime::kScopes}};

/// Reach, composed and started from its directory.
class PlayedReach : public PlayedGame {
public:
    explicit PlayedReach(std::span<const composition::RegistrarEntry> registrars = kReach)
        : PlayedGame(registrars,
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

RAWFRAME_TEST(AClientDrawsItsOwnHandsWhereItsInputHoldsThem) {
    // A client presents the World it mirrors; here the server's own, so the
    // hands' poses are where the server last put them and the player's
    // input is moved on since, as a client's predicted input is (D665).
    PlayedReach played{kReachPresented};
    RAWFRAME_EXPECT(played.started() && presentationPlan != nullptr);
    if (!played.started() || presentationPlan == nullptr) {
        return;
    }
    auto presentation = presentationPlan->presentation();
    RAWFRAME_EXPECT(presentation.has_value() && *presentation != nullptr);
    if (!presentation.has_value() || *presentation == nullptr) {
        return;
    }
    world::World& world = *simulation->world();
    const world::EntityHandle kPlayer = joined(world);
    const world::EntityHandle kOther = joined(world);
    played.run(3);
    const world::EntityHandle kRight = handOf(world, kPlayer, 1);
    const world::EntityHandle kLeft = handOf(world, kPlayer, 0);
    const world::EntityHandle kTheirs = handOf(world, kOther, 1);
    RAWFRAME_EXPECT(!kRight.isNull() && !kLeft.isNull() && !kTheirs.isNull());
    if (kRight.isNull() || kLeft.isNull() || kTheirs.isNull()) {
        return;
    }
    const auto kPresent = [&](world::EntityHandle me) {
        RAWFRAME_EXPECT((*presentation)->present(world, me, {}, *world::TickRate::of(60)).has_value());
        RAWFRAME_EXPECT((*presentation)->statistics().systemsFailed == 0);
    };
    const auto kPlacedAt = [&](world::EntityHandle entity, double x, double y, double z) {
        const Placed* const kPlaced = valueOf<Placed>(world, entity, kPlacedId);
        return kPlaced != nullptr && kPlaced->placed && std::abs(kPlaced->x - x) < 1e-5 &&
               std::abs(kPlaced->y - y) < 1e-5 && std::abs(kPlaced->z - z) < 1e-5;
    };
    const auto kUnplaced = [&](world::EntityHandle entity) {
        const Placed* const kPlaced = valueOf<Placed>(world, entity, kPlacedId);
        return kPlaced != nullptr && !kPlaced->placed;
    };
    // The right hand held above the near row's middle block, not yet
    // ticked: the server's pose is still at rest, the drawing already there
    // (the first seat's frame is the World's, 0.9 along +Z).
    // Presenting attaches the player's view, which moves its components:
    // its input is found afresh each time.
    const auto kHandsNow = [&] {
        return valueOf<Hands>(world, kPlayer, kHandsId);
    };
    *kHandsNow() = Hands{.rightX = 0, .rightY = 0.88F, .rightZ = -0.75F};
    kPresent(kPlayer);
    RAWFRAME_EXPECT(near(poseOf(world, kRight), 0.2, 0.95, 0.55, 0.01));
    RAWFRAME_EXPECT(kPlacedAt(kRight, 0, 0.88F, 0.9 - 0.75F));
    // The left, held nowhere, is drawn at rest; the other player's hands,
    // and the blocks, where the server puts them.
    RAWFRAME_EXPECT(kPlacedAt(kLeft, -0.2, 0.95, 0.9 - 0.35));
    RAWFRAME_EXPECT(kUnplaced(kTheirs));
    for (const world::EntityHandle kBlock : holding(world, kBlockId)) {
        RAWFRAME_EXPECT(kUnplaced(kBlock));
    }
    // Presented for the other player, its hands are its own.
    kPresent(kOther);
    RAWFRAME_EXPECT(kUnplaced(kRight) && valueOf<Placed>(world, kTheirs, kPlacedId)->placed);
    // Taken: the block the right hand holds is drawn with the hand, as far
    // from it as the server keeps it.
    played.run(30);
    kHandsNow()->rightGrip = 1;
    played.run(3);
    const Hand* const kHeld = valueOf<Hand>(world, kRight, kHandId);
    RAWFRAME_EXPECT(kHeld->holding);
    const world::EntityHandle kBlock = kHeld->held;
    kHandsNow()->rightY = 1.3F;
    kPresent(kPlayer);
    const physics3d::Pose3D kHand = poseOf(world, kRight);
    const physics3d::Pose3D kHeldAt = poseOf(world, kBlock);
    RAWFRAME_EXPECT(kPlacedAt(kRight, 0, 1.3F, 0.9 - 0.75F));
    RAWFRAME_EXPECT(kPlacedAt(kBlock,
                              kHeldAt.x - kHand.x,
                              static_cast<double>(1.3F) + kHeldAt.y - kHand.y,
                              (0.9 - 0.75F) + kHeldAt.z - kHand.z));
    presentationPlan = nullptr;
}
