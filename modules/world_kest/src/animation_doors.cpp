#include "animation_doors.h"

#include <array>
#include <cstdint>
#include <span>

namespace rawframe::world_kest {

namespace {

/// rawframe.animation's BonePose, as the program lays it out.
struct BonePose {
    bool posed = false;
    double x = 0;
    double y = 0;
    double z = 0;
    float qx = 0;
    float qy = 0;
    float qz = 0;
    float qw = 0;
};

/// rawframe.animation's AnimationEvent.
struct AnimationEvent {
    bool found = false;
    bool simulation = false;
    bool reverse = false;
    std::uint64_t event = 0;
    float weight = 0;
};

constexpr std::array<kest::Parameter, 2> kTakes = {kest::Parameter{kest::Slot::Value, "rawframe.world.Entity"},
                                                   kest::Parameter{kest::Slot::U32}};
constexpr std::array<kest::Parameter, 1> kBoneGives = {
    kest::Parameter{kest::Slot::Value, "rawframe.animation.BonePose"}};
constexpr std::array<kest::Parameter, 1> kEventGives = {
    kest::Parameter{kest::Slot::Value, "rawframe.animation.AnimationEvent"}};

/// The queries and the entity a call asks about; null queries when it
/// failed.
const world_animation::AnimationQueries* asked(kest::DoorCall& call, void* context, world::EntityHandle& entity) {
    const auto* const kQueries = static_cast<const AnimationDoorContext*>(context)->queries;
    if (kQueries == nullptr) {
        call.fail("this World has no animation to ask");
        return nullptr;
    }
    if (!call.value(0, std::as_writable_bytes(std::span{&entity, 1}))) {
        call.fail("the program's Entity is not the engine's");
        return nullptr;
    }
    return kQueries;
}

void boneDoor(kest::DoorCall& call, void* context) noexcept {
    world::EntityHandle entity;
    const world_animation::AnimationQueries* const kQueries = asked(call, context, entity);
    if (kQueries == nullptr) {
        return;
    }
    BonePose answer;
    const animation::Pose* const kPose = kQueries->pose(entity);
    const auto kIndex = static_cast<std::uint64_t>(call.integer(1));
    if (kPose != nullptr && kIndex < kPose->bones.size()) {
        const animation::Transform& bone = kPose->bones[kIndex];
        answer = BonePose{.posed = true,
                          .x = bone.translation[0],
                          .y = bone.translation[1],
                          .z = bone.translation[2],
                          .qx = static_cast<float>(bone.rotation[0]),
                          .qy = static_cast<float>(bone.rotation[1]),
                          .qz = static_cast<float>(bone.rotation[2]),
                          .qw = static_cast<float>(bone.rotation[3])};
    }
    if (!call.answerValue(std::as_bytes(std::span{&answer, 1}))) {
        call.fail("the program's BonePose is not the engine's");
    }
}

void eventDoor(kest::DoorCall& call, void* context) noexcept {
    world::EntityHandle entity;
    const world_animation::AnimationQueries* const kQueries = asked(call, context, entity);
    if (kQueries == nullptr) {
        return;
    }
    AnimationEvent answer;
    const std::span<const animation::GraphEvent> kEvents = kQueries->events(entity);
    const auto kIndex = static_cast<std::uint64_t>(call.integer(1));
    if (kIndex < kEvents.size()) {
        const animation::GraphEvent& fired = kEvents[kIndex];
        answer = AnimationEvent{.found = true,
                                .simulation = fired.relevance == animation::Relevance::Simulation,
                                .reverse = fired.reverse,
                                .event = fired.event,
                                .weight = static_cast<float>(fired.weight)};
    }
    if (!call.answerValue(std::as_bytes(std::span{&answer, 1}))) {
        call.fail("the program's AnimationEvent is not the engine's");
    }
}

} // namespace

result::Status addAnimationDoors(kest::DoorTable& doors, const AnimationDoorContext* context) {
    auto* const kContext = const_cast<AnimationDoorContext*>(context);
    RAWFRAME_TRY(doors.add(kest::Door{.name = "Animation.bone",
                                      .function = &boneDoor,
                                      .context = kContext,
                                      .takes = kTakes,
                                      .gives = kBoneGives,
                                      .safeForUntrusted = true}));
    return doors.add(kest::Door{.name = "Animation.event",
                                .function = &eventDoor,
                                .context = kContext,
                                .takes = kTakes,
                                .gives = kEventGives,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
