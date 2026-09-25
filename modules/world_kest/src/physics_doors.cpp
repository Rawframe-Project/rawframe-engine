#include "physics_doors.h"

#include "rawframe/world_replication/perception.h"

#include <array>
#include <optional>
#include <span>

namespace rawframe::world_kest {

namespace {

constexpr kest::Parameter kReal{kest::Slot::F64};
constexpr kest::Parameter kFloat{kest::Slot::F32};
constexpr kest::Parameter kClass{kest::Slot::U64};
constexpr kest::Parameter kSeen{kest::Slot::Value, "rawframe.replication.Perception"};

constexpr std::array<kest::Parameter, 4> kRayTakes = {kReal, kReal, kFloat, kFloat};
constexpr std::array<kest::Parameter, 5> kRayAtTakes = {kReal, kReal, kFloat, kFloat, kSeen};
constexpr std::array<kest::Parameter, 5> kRayAmongTakes = {kClass, kReal, kReal, kFloat, kFloat};
constexpr std::array<kest::Parameter, 6> kRayAtAmongTakes = {kClass, kReal, kReal, kFloat, kFloat, kSeen};
constexpr std::array<kest::Parameter, 1> kRayGives = {
    kest::Parameter{kest::Slot::Value, "rawframe.physics2d.RayHit2D"}};

constexpr std::array<kest::Parameter, 6> kRay3Takes = {kReal, kReal, kReal, kFloat, kFloat, kFloat};
constexpr std::array<kest::Parameter, 7> kRay3AtTakes = {kReal, kReal, kReal, kFloat, kFloat, kFloat, kSeen};
constexpr std::array<kest::Parameter, 7> kRay3AmongTakes = {kClass, kReal, kReal, kReal, kFloat, kFloat, kFloat};
constexpr std::array<kest::Parameter, 8> kRay3AtAmongTakes = {
    kClass, kReal, kReal, kReal, kFloat, kFloat, kFloat, kSeen};
constexpr std::array<kest::Parameter, 1> kRay3Gives = {
    kest::Parameter{kest::Slot::Value, "rawframe.physics3d.RayHit3D"}};

/// Takes back what one connection was sent, no further than it was sent.
class SentGate final : public physics::RewindGate {
public:
    SentGate(const world_replication::InterestHistory& interest, std::uint32_t viewer, std::uint64_t tick) noexcept
        : interest_(&interest), viewer_(viewer), tick_(tick) {
    }
    [[nodiscard]] std::optional<std::uint64_t> since(world::EntityHandle entity) const noexcept override {
        return interest_->sentSince(viewer_, entity, tick_);
    }

private:
    const world_replication::InterestHistory* interest_;
    std::uint32_t viewer_;
    std::uint64_t tick_;
};

/// The moment a Perception names, gated by what its connection was sent
/// when a connection claimed it and the doors know who was sent what.
struct Seen {
    physics::Moment moment;
    std::optional<SentGate> gate;
};

/// Reads the Perception argument into `seen`; false if it is not the
/// engine's.
[[nodiscard]] bool readSeen(kest::DoorCall& call, std::size_t argument, const PhysicsDoorContext& doors, Seen& seen) {
    world_replication::Perception perception;
    if (!call.value(argument, std::as_writable_bytes(std::span{&perception, 1}))) {
        call.fail("the program's Perception is not the engine's");
        return false;
    }
    if (doors.interest != nullptr && perception.viewer != 0) {
        seen.gate.emplace(*doors.interest, perception.viewer, perception.baseTick);
    }
    seen.moment = physics::Moment{.base = perception.baseTick,
                                  .fraction = perception.fraction,
                                  .gate = seen.gate.has_value() ? &*seen.gate : nullptr};
    return true;
}

/// A 2D ray door: `Among` doors take the class first; `At` doors take the
/// moment seen last.
template <bool Among, bool At> void rayDoor(kest::DoorCall& call, void* context) noexcept {
    const PhysicsDoorContext& doors = *static_cast<const PhysicsDoorContext*>(context);
    const physics2d::Physics2DQueries* const kQueries = doors.queries;
    if (kQueries == nullptr) {
        call.fail("this World has no physics to ask");
        return;
    }
    constexpr std::size_t kFirst = Among ? 1 : 0;
    const std::uint64_t kAmong = Among ? static_cast<std::uint64_t>(call.integer(0)) : physics::kEveryClass;
    physics2d::RayHit2D hit;
    if constexpr (At) {
        Seen seen;
        if (!readSeen(call, kFirst + 4, doors, seen)) {
            return;
        }
        hit = kQueries->castRayAt(call.real(kFirst),
                                  call.real(kFirst + 1),
                                  static_cast<float>(call.real(kFirst + 2)),
                                  static_cast<float>(call.real(kFirst + 3)),
                                  seen.moment,
                                  kAmong);
    } else {
        hit = kQueries->castRay(call.real(kFirst),
                                call.real(kFirst + 1),
                                static_cast<float>(call.real(kFirst + 2)),
                                static_cast<float>(call.real(kFirst + 3)),
                                kAmong);
    }
    if (!call.answerValue(std::as_bytes(std::span{&hit, 1}))) {
        call.fail("the program's RayHit2D is not the engine's");
    }
}

/// The same in three dimensions.
template <bool Among, bool At> void ray3Door(kest::DoorCall& call, void* context) noexcept {
    const PhysicsDoorContext& doors = *static_cast<const PhysicsDoorContext*>(context);
    const physics3d::Physics3DQueries* const kQueries = doors.queries3d;
    if (kQueries == nullptr) {
        call.fail("this World has no physics to ask");
        return;
    }
    constexpr std::size_t kFirst = Among ? 1 : 0;
    const std::uint64_t kAmong = Among ? static_cast<std::uint64_t>(call.integer(0)) : physics::kEveryClass;
    physics3d::RayHit3D hit;
    if constexpr (At) {
        Seen seen;
        if (!readSeen(call, kFirst + 6, doors, seen)) {
            return;
        }
        hit = kQueries->castRayAt(call.real(kFirst),
                                  call.real(kFirst + 1),
                                  call.real(kFirst + 2),
                                  static_cast<float>(call.real(kFirst + 3)),
                                  static_cast<float>(call.real(kFirst + 4)),
                                  static_cast<float>(call.real(kFirst + 5)),
                                  seen.moment,
                                  kAmong);
    } else {
        hit = kQueries->castRay(call.real(kFirst),
                                call.real(kFirst + 1),
                                call.real(kFirst + 2),
                                static_cast<float>(call.real(kFirst + 3)),
                                static_cast<float>(call.real(kFirst + 4)),
                                static_cast<float>(call.real(kFirst + 5)),
                                kAmong);
    }
    if (!call.answerValue(std::as_bytes(std::span{&hit, 1}))) {
        call.fail("the program's RayHit3D is not the engine's");
    }
}

} // namespace

result::Status addPhysicsDoors(kest::DoorTable& doors, std::uint8_t dimensions, const PhysicsDoorContext* context) {
    auto* const kContext = const_cast<PhysicsDoorContext*>(context);
    if (dimensions == 3) {
        RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics3D.castRay",
                                          .function = &ray3Door<false, false>,
                                          .context = kContext,
                                          .takes = kRay3Takes,
                                          .gives = kRay3Gives}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics3D.castRayAt",
                                          .function = &ray3Door<false, true>,
                                          .context = kContext,
                                          .takes = kRay3AtTakes,
                                          .gives = kRay3Gives}));
        RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics3D.castRayAmong",
                                          .function = &ray3Door<true, false>,
                                          .context = kContext,
                                          .takes = kRay3AmongTakes,
                                          .gives = kRay3Gives}));
        return doors.add(kest::Door{.name = "Physics3D.castRayAtAmong",
                                    .function = &ray3Door<true, true>,
                                    .context = kContext,
                                    .takes = kRay3AtAmongTakes,
                                    .gives = kRay3Gives});
    }
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics2D.castRay",
                                      .function = &rayDoor<false, false>,
                                      .context = kContext,
                                      .takes = kRayTakes,
                                      .gives = kRayGives}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics2D.castRayAt",
                                      .function = &rayDoor<false, true>,
                                      .context = kContext,
                                      .takes = kRayAtTakes,
                                      .gives = kRayGives}));
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics2D.castRayAmong",
                                      .function = &rayDoor<true, false>,
                                      .context = kContext,
                                      .takes = kRayAmongTakes,
                                      .gives = kRayGives}));
    return doors.add(kest::Door{.name = "Physics2D.castRayAtAmong",
                                .function = &rayDoor<true, true>,
                                .context = kContext,
                                .takes = kRayAtAmongTakes,
                                .gives = kRayGives});
}

} // namespace rawframe::world_kest
