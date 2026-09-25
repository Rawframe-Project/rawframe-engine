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

/// A ray door: `Among` doors take the class first; `At` doors take the
/// moment seen last.
/// Takes back what one connection was sent, no further than it was sent.
class SentGate final : public physics2d::RewindGate {
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

template <bool Among, bool At> void rayDoor(kest::DoorCall& call, void* context) noexcept {
    const PhysicsDoorContext& doors = *static_cast<const PhysicsDoorContext*>(context);
    const physics2d::Physics2DQueries* const kQueries = doors.queries;
    if (kQueries == nullptr) {
        call.fail("this World has no physics to ask");
        return;
    }
    constexpr std::size_t kFirst = Among ? 1 : 0;
    const std::uint64_t kAmong = Among ? static_cast<std::uint64_t>(call.integer(0)) : physics2d::kEveryClass;
    physics2d::RayHit2D hit;
    if constexpr (At) {
        world_replication::Perception seen;
        if (!call.value(kFirst + 4, std::as_writable_bytes(std::span{&seen, 1}))) {
            call.fail("the program's Perception is not the engine's");
            return;
        }
        // A moment a connection claimed rewinds only what it was sent.
        std::optional<SentGate> gate;
        if (doors.interest != nullptr && seen.viewer != 0) {
            gate.emplace(*doors.interest, seen.viewer, seen.baseTick);
        }
        hit = kQueries->castRayAt(call.real(kFirst),
                                  call.real(kFirst + 1),
                                  static_cast<float>(call.real(kFirst + 2)),
                                  static_cast<float>(call.real(kFirst + 3)),
                                  physics2d::Moment{.base = seen.baseTick,
                                                    .fraction = seen.fraction,
                                                    .gate = gate.has_value() ? &*gate : nullptr},
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

} // namespace

result::Status addPhysicsDoors(kest::DoorTable& doors, const PhysicsDoorContext* context) {
    auto* const kContext = const_cast<PhysicsDoorContext*>(context);
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
