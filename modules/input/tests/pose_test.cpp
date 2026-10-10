// Poses (ADR-0081's fourth value, D596): a controller's grip and aim mapped
// into pose actions in the one pipeline, on while located, the first
// located binding's pose, and its buttons as any device's.

#include "rawframe/input/mapper.h"
#include "rawframe/test/test.h"

#include <memory>
#include <string_view>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr DeviceId kHands{9};
constexpr PlayerSlot kPlayer{0};
constexpr std::size_t kHand = 0;
constexpr std::size_t kSelect = 1;

Binding single(std::string_view name, std::uint32_t slot = 0) {
    Binding binding;
    binding.slot = slot;
    binding.device = DeviceClass::Controller;
    binding.controls[0] = *controlNamed(DeviceClass::Controller, name);
    return binding;
}

std::unique_ptr<Mapper> mapperOf() {
    ActionSet set;
    set.actions.push_back(Action{
        .id = 1, .name = "hand", .type = ValueType::Pose, .bindings = {single("grip_right"), single("grip_left", 1)}});
    set.actions.push_back(
        Action{.id = 2, .name = "select", .type = ValueType::Bool, .bindings = {single("select_right")}});
    set.contexts.push_back(Context{.id = 10, .name = "play", .actions = {kHand, kSelect}});
    auto mapper = *Mapper::create(std::move(set), {.players = 1});
    RAWFRAME_EXPECT(mapper->activate(kPlayer, 0).has_value());
    RAWFRAME_EXPECT(mapper->pair(kHands, DeviceClass::Controller, kPlayer).has_value());
    return mapper;
}

void place(Mapper& mapper, std::string_view control, Pose pose) {
    mapper.submit(
        ControlEvent{.device = kHands, .control = *controlNamed(DeviceClass::Controller, control), .pose = pose});
}

} // namespace

RAWFRAME_TEST(AControllersPoseIsAPoseActionsWhileLocated) {
    const auto kMapper = mapperOf();
    Mapper& mapper = *kMapper;
    // Nothing located: off, at rest.
    mapper.update();
    RAWFRAME_EXPECT(!mapper.current(kPlayer, kHand).on);
    // The left grip located, the right not: the left's pose, on.
    const Pose kLeft{.position = {-0.2F, 1.1F, -0.4F}, .orientation = {0, 0.6F, 0, 0.8F}, .located = true};
    place(mapper, "grip_left", kLeft);
    place(mapper, "grip_right", Pose{});
    mapper.update();
    ActionState hand = mapper.current(kPlayer, kHand);
    RAWFRAME_EXPECT(hand.on && hand.pose.position == kLeft.position && hand.pose.orientation == kLeft.orientation);
    RAWFRAME_EXPECT(hand.x == kLeft.position[0] && hand.y == kLeft.position[1] && !hand.pose.tracked);
    // Both located: the first binding's, the right's.
    const Pose kRight{.position = {0.25F, 1.2F, -0.35F}, .located = true, .tracked = true};
    place(mapper, "grip_right", kRight);
    mapper.update();
    hand = mapper.current(kPlayer, kHand);
    RAWFRAME_EXPECT(hand.on && hand.pose.position == kRight.position && hand.pose.tracked);
    mapper.commit(1);
    // Neither located: off, released this tick.
    place(mapper, "grip_right", Pose{});
    place(mapper, "grip_left", Pose{});
    mapper.update();
    mapper.commit(2);
    RAWFRAME_EXPECT(!mapper.committed(kPlayer, kHand).on && mapper.releasedThisTick(kPlayer, kHand));
    // Its buttons are bools, and its events are a controller's (D559).
    mapper.submit(
        ControlEvent{.device = kHands, .control = *controlNamed(DeviceClass::Controller, "select_right"), .x = 1});
    mapper.update();
    RAWFRAME_EXPECT(mapper.current(kPlayer, kSelect).on);
    RAWFRAME_EXPECT(mapper.statistics().events[static_cast<std::size_t>(DeviceClass::Controller)] == 6);
    RAWFRAME_EXPECT(nameOf(DeviceClass::Controller) == "controller" &&
                    deviceClassNamed("controller") == DeviceClass::Controller);
}
