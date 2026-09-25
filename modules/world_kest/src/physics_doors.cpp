#include "physics_doors.h"

#include "rawframe/world_replication/perception.h"

#include <array>
#include <span>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 4> kRayTakes = {kest::Parameter{kest::Slot::F64},
                                                      kest::Parameter{kest::Slot::F64},
                                                      kest::Parameter{kest::Slot::F32},
                                                      kest::Parameter{kest::Slot::F32}};
constexpr std::array<kest::Parameter, 1> kRayGives = {
    kest::Parameter{kest::Slot::Value, "rawframe.physics2d.RayHit2D"}};

constexpr std::array<kest::Parameter, 5> kRayAtTakes = {
    kest::Parameter{kest::Slot::F64},
    kest::Parameter{kest::Slot::F64},
    kest::Parameter{kest::Slot::F32},
    kest::Parameter{kest::Slot::F32},
    kest::Parameter{kest::Slot::Value, "rawframe.replication.Perception"}};

void castRayDoor(kest::DoorCall& call, void* context) noexcept {
    const physics2d::Physics2DQueries* const kQueries =
        *static_cast<const physics2d::Physics2DQueries* const*>(context);
    if (kQueries == nullptr) {
        call.fail("this World has no physics to ask");
        return;
    }
    const physics2d::RayHit2D kHit = kQueries->castRay(
        call.real(0), call.real(1), static_cast<float>(call.real(2)), static_cast<float>(call.real(3)));
    if (!call.answerValue(std::as_bytes(std::span{&kHit, 1}))) {
        call.fail("the program's RayHit2D is not the engine's");
    }
}

void castRayAtDoor(kest::DoorCall& call, void* context) noexcept {
    const physics2d::Physics2DQueries* const kQueries =
        *static_cast<const physics2d::Physics2DQueries* const*>(context);
    if (kQueries == nullptr) {
        call.fail("this World has no physics to ask");
        return;
    }
    world_replication::Perception seen;
    if (!call.value(4, std::as_writable_bytes(std::span{&seen, 1}))) {
        call.fail("the program's Perception is not the engine's");
        return;
    }
    const physics2d::RayHit2D kHit = kQueries->castRayAt(call.real(0),
                                                         call.real(1),
                                                         static_cast<float>(call.real(2)),
                                                         static_cast<float>(call.real(3)),
                                                         seen.baseTick,
                                                         seen.fraction);
    if (!call.answerValue(std::as_bytes(std::span{&kHit, 1}))) {
        call.fail("the program's RayHit2D is not the engine's");
    }
}

} // namespace

result::Status addPhysicsDoors(kest::DoorTable& doors, const physics2d::Physics2DQueries* const* queries) {
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Physics2D.castRay",
                                      .function = &castRayDoor,
                                      .context = const_cast<physics2d::Physics2DQueries**>(queries),
                                      .takes = kRayTakes,
                                      .gives = kRayGives}));
    return doors.add(kest::Door{.name = "Physics2D.castRayAt",
                                .function = &castRayAtDoor,
                                .context = const_cast<physics2d::Physics2DQueries**>(queries),
                                .takes = kRayAtTakes,
                                .gives = kRayGives});
}

} // namespace rawframe::world_kest
