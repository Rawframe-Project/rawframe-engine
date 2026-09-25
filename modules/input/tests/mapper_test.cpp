// The mapping runtime against ADR-0037's validation list: edges accounted
// once to each domain and never lost within a tick, deadzones and
// thresholds as declared, routing by priority and recency with consumption
// that blocks, the hygiene set, the text gate, and players kept apart.

#include "rawframe/input/mapper.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <string_view>
#include <vector>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr DeviceId kKeyboard{1};
constexpr DeviceId kPad{2};
constexpr DeviceId kMouse{3};
constexpr DeviceId kSecondPad{4};
constexpr PlayerSlot kFirst{0};
constexpr PlayerSlot kSecond{1};

Control key(std::string_view name) {
    return *controlNamed(DeviceClass::Keyboard, name);
}

Control pad(std::string_view name) {
    return *controlNamed(DeviceClass::Gamepad, name);
}

Binding single(Control control) {
    Binding binding;
    binding.device = control.device;
    binding.controls[0] = control;
    return binding;
}

// Actions, by index.
constexpr std::size_t kMove = 0;
constexpr std::size_t kJump = 1;
constexpr std::size_t kConfirm = 2;
constexpr std::size_t kFire = 3;
constexpr std::size_t kZoom = 4;
constexpr std::size_t kCrouch = 5;
constexpr std::size_t kType = 6;
// Contexts, by index.
constexpr std::size_t kOnFoot = 0;
constexpr std::size_t kMenu = 1;
constexpr std::size_t kOverlay = 2;
constexpr std::size_t kChat = 3;

ActionSet actionSet() {
    ActionSet set;
    Action move{.id = 1, .name = "move", .type = ValueType::Axis2D};
    Binding quad;
    quad.composite = Composite::Quad;
    quad.controls = {key("key_w"), key("key_s"), key("key_a"), key("key_d")};
    move.bindings.push_back(quad);
    Binding stick = single(pad("stick_left"));
    stick.deadzoneLower = 0.25F;
    stick.invertY = true;
    move.bindings.push_back(stick);
    set.actions.push_back(move);

    Action jump{.id = 2, .name = "jump", .type = ValueType::Bool};
    jump.bindings = {single(key("space")), single(pad("face_south"))};
    set.actions.push_back(jump);

    Action confirm{.id = 3, .name = "confirm", .type = ValueType::Bool};
    confirm.bindings = {single(key("space")), single(key("enter"))};
    set.actions.push_back(confirm);

    // A trigger with hysteresis: on at 0.75, off below 0.25.
    Action fire{.id = 4, .name = "fire", .type = ValueType::Bool, .pressThreshold = 0.75F, .releaseThreshold = 0.25F};
    Binding trigger = single(pad("trigger_right"));
    trigger.deadzoneLower = 0;
    fire.bindings.push_back(trigger);
    set.actions.push_back(fire);

    Action zoom{.id = 5, .name = "zoom", .type = ValueType::Bool, .pressThreshold = 0.5F, .releaseThreshold = 0.5F};
    zoom.bindings.push_back(single(*controlNamed(DeviceClass::Mouse, "wheel_y")));
    set.actions.push_back(zoom);

    // Shift and C exactly; C alone with anything else is not it.
    Action crouch{.id = 6, .name = "crouch", .type = ValueType::Bool};
    Binding shifted = single(key("key_c"));
    shifted.modifiers = static_cast<std::uint8_t>(Modifier::Shift);
    shifted.exactModifiers = true;
    crouch.bindings.push_back(shifted);
    set.actions.push_back(crouch);

    Action type{.id = 7, .name = "type", .type = ValueType::Bool};
    type.bindings.push_back(single(key("key_t")));
    set.actions.push_back(type);

    set.contexts.push_back(
        Context{.id = 10, .name = "on_foot", .priority = 0, .actions = {kMove, kJump, kFire, kZoom, kCrouch}});
    set.contexts.push_back(Context{.id = 11, .name = "menu", .priority = 10, .actions = {kConfirm}});
    set.contexts.push_back(Context{.id = 12, .name = "overlay", .priority = 0, .actions = {kConfirm}});
    set.contexts.push_back(
        Context{.id = 13, .name = "chat", .priority = 0, .actions = {kType}, .textEditGated = false});
    return set;
}

struct Rig {
    std::unique_ptr<Mapper> mapper = *Mapper::create(actionSet(), {.players = 2});

    Rig() {
        RAWFRAME_EXPECT(mapper->pair(kKeyboard, DeviceClass::Keyboard, kFirst).has_value());
        RAWFRAME_EXPECT(mapper->pair(kPad, DeviceClass::Gamepad, kFirst).has_value());
        RAWFRAME_EXPECT(mapper->pair(kMouse, DeviceClass::Mouse, kFirst).has_value());
        RAWFRAME_EXPECT(mapper->pair(kSecondPad, DeviceClass::Gamepad, kSecond).has_value());
        RAWFRAME_EXPECT(mapper->activate(kFirst, kOnFoot).has_value());
        RAWFRAME_EXPECT(mapper->activate(kSecond, kOnFoot).has_value());
    }

    void press(DeviceId device, Control control, float x = 1, float y = 0) {
        mapper->submit(ControlEvent{.device = device, .control = control, .x = x, .y = y});
    }
    void release(DeviceId device, Control control) {
        press(device, control, 0, 0);
    }
    const ActionState& now(std::size_t action, PlayerSlot player = kFirst) {
        mapper->update();
        return mapper->current(player, action);
    }
};

bool near(float value, float expected) {
    return std::abs(value - expected) < 1e-5F;
}

} // namespace

RAWFRAME_TEST(EdgesAreNeverLostAndCountedOncePerDomain) {
    Rig rig;
    // A press and release between two ticks: both edges, in order, in that
    // tick, and the state is off.
    rig.press(kKeyboard, key("space"));
    rig.release(kKeyboard, key("space"));
    rig.mapper->commit(1);
    const auto kEdges = rig.mapper->tickEdges(kFirst);
    RAWFRAME_EXPECT(kEdges.size() == 2 && kEdges[0].action == kJump && kEdges[0].pressed && kEdges[0].ordinal == 0 &&
                    !kEdges[1].pressed && kEdges[1].ordinal == 1);
    RAWFRAME_EXPECT(rig.mapper->pressedThisTick(kFirst, kJump) && rig.mapper->releasedThisTick(kFirst, kJump) &&
                    !rig.mapper->committed(kFirst, kJump).on);
    // A frame that runs two ticks: the edges are the first tick's only.
    rig.mapper->commit(2);
    RAWFRAME_EXPECT(rig.mapper->tickEdges(kFirst).empty() && rig.mapper->committedTick() == 2);
    // The frame saw them too, once, until it ended.
    RAWFRAME_EXPECT(rig.mapper->frameEdges(kFirst).size() == 2);
    rig.mapper->endFrame();
    RAWFRAME_EXPECT(rig.mapper->frameEdges(kFirst).empty());
    // A frame that runs no tick: its press waits for the next one.
    rig.press(kKeyboard, key("space"));
    rig.mapper->update();
    rig.mapper->endFrame();
    rig.mapper->commit(3);
    RAWFRAME_EXPECT(rig.mapper->pressedThisTick(kFirst, kJump) && rig.mapper->committed(kFirst, kJump).on);
}

RAWFRAME_TEST(ValuesFollowTheirDeclarations) {
    Rig rig;
    // A quad: up, then up and right, clamped to the circle.
    rig.press(kKeyboard, key("key_w"));
    RAWFRAME_EXPECT(near(rig.now(kMove).y, 1) && rig.now(kMove).on);
    rig.press(kKeyboard, key("key_d"));
    RAWFRAME_EXPECT(near(rig.now(kMove).x, std::sqrt(0.5F)) && near(rig.now(kMove).y, std::sqrt(0.5F)));
    rig.release(kKeyboard, key("key_w"));
    rig.release(kKeyboard, key("key_d"));
    // A stick: radially dead to 0.25, then inverse-lerped, y inverted.
    rig.press(kPad, pad("stick_left"), 0.2F, 0);
    RAWFRAME_EXPECT(near(rig.now(kMove).x, 0) && !rig.now(kMove).on);
    rig.press(kPad, pad("stick_left"), 0.625F, 0);
    RAWFRAME_EXPECT(near(rig.now(kMove).x, 0.5F) && rig.now(kMove).on);
    rig.press(kPad, pad("stick_left"), 0, 0.4375F);
    RAWFRAME_EXPECT(near(rig.now(kMove).y, -0.25F) && near(rig.now(kMove).x, 0) && !rig.now(kMove).on);
    // Hysteresis: on at 0.75, held down to 0.25.
    rig.press(kPad, pad("trigger_right"), 0.7F);
    RAWFRAME_EXPECT(!rig.now(kFire).on);
    rig.press(kPad, pad("trigger_right"), 0.8F);
    RAWFRAME_EXPECT(rig.now(kFire).on);
    rig.press(kPad, pad("trigger_right"), 0.3F);
    RAWFRAME_EXPECT(rig.now(kFire).on);
    rig.press(kPad, pad("trigger_right"), 0.2F);
    RAWFRAME_EXPECT(!rig.now(kFire).on);
    // A wheel moves for one tick and rests after it.
    rig.press(kMouse, *controlNamed(DeviceClass::Mouse, "wheel_y"), 1);
    rig.mapper->commit(1);
    RAWFRAME_EXPECT(rig.mapper->pressedThisTick(kFirst, kZoom) && rig.mapper->committed(kFirst, kZoom).on);
    rig.mapper->commit(2);
    RAWFRAME_EXPECT(rig.mapper->releasedThisTick(kFirst, kZoom) && !rig.mapper->committed(kFirst, kZoom).on);
    // Exact modifiers: shift and C, nothing more.
    rig.press(kKeyboard, key("key_c"));
    RAWFRAME_EXPECT(!rig.now(kCrouch).on);
    rig.press(kKeyboard, key("shift_left"));
    RAWFRAME_EXPECT(rig.now(kCrouch).on);
    rig.press(kKeyboard, key("control_left"));
    RAWFRAME_EXPECT(!rig.now(kCrouch).on);
}

RAWFRAME_TEST(RoutingBlocksWhatAHigherNodeConsumes) {
    Rig rig;
    // The menu is above the game: space confirms and does not jump.
    RAWFRAME_EXPECT(rig.mapper->activate(kFirst, kMenu).has_value());
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kConfirm).on && !rig.now(kJump).on);
    // The gamepad's jump is not claimed by the menu.
    rig.press(kPad, pad("face_south"));
    RAWFRAME_EXPECT(rig.now(kJump).on);
    rig.release(kPad, pad("face_south"));
    rig.release(kKeyboard, key("space"));
    // Closed, the game has space again.
    rig.mapper->deactivate(kFirst, kMenu);
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kJump).on && !rig.now(kConfirm).on);
    rig.release(kKeyboard, key("space"));
    // Equal priority: the most recently activated first, until the other is
    // brought to the front.
    RAWFRAME_EXPECT(rig.mapper->activate(kFirst, kOverlay).has_value());
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kConfirm).on && !rig.now(kJump).on);
    rig.release(kKeyboard, key("space"));
    rig.mapper->bringToFront(kFirst, kOnFoot);
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kJump).on && !rig.now(kConfirm).on);
    rig.release(kKeyboard, key("space"));
    // Activating it again moves nothing; disabling it routes nothing.
    RAWFRAME_EXPECT(rig.mapper->activate(kFirst, kOverlay).has_value());
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kJump).on);
    rig.release(kKeyboard, key("space"));
    rig.mapper->setEnabled(kFirst, kOnFoot, false);
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(!rig.now(kJump).on && rig.now(kConfirm).on);
}

RAWFRAME_TEST(TheNodeThatSawThePressSeesTheRelease) {
    Rig rig;
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(rig.now(kJump).on);
    rig.mapper->commit(1);
    // The menu opens over a held space: the jump stays held until space
    // comes up, and the menu never sees a press it did not see begin.
    RAWFRAME_EXPECT(rig.mapper->activate(kFirst, kMenu).has_value());
    RAWFRAME_EXPECT(rig.now(kJump).on && !rig.now(kConfirm).on);
    rig.release(kKeyboard, key("space"));
    rig.mapper->commit(2);
    RAWFRAME_EXPECT(rig.mapper->releasedThisTick(kFirst, kJump) && !rig.mapper->pressedThisTick(kFirst, kConfirm));
    // The next press is the menu's.
    rig.press(kKeyboard, key("space"));
    rig.mapper->commit(3);
    RAWFRAME_EXPECT(rig.mapper->pressedThisTick(kFirst, kConfirm) && !rig.mapper->pressedThisTick(kFirst, kJump));
    // Focus lost: everything held is released through edges.
    rig.press(kPad, pad("face_south"));
    rig.mapper->update();
    rig.mapper->releaseAll();
    rig.mapper->commit(4);
    RAWFRAME_EXPECT(!rig.mapper->committed(kFirst, kConfirm).on && !rig.mapper->committed(kFirst, kJump).on &&
                    rig.mapper->releasedThisTick(kFirst, kConfirm) && rig.mapper->releasedThisTick(kFirst, kJump));
}

RAWFRAME_TEST(TextEditingGatesTheKeyboard) {
    Rig rig;
    RAWFRAME_EXPECT(rig.mapper->activate(kFirst, kChat).has_value());
    rig.press(kKeyboard, key("key_w"));
    RAWFRAME_EXPECT(rig.now(kMove).on);
    // A key held into text editing is released, not stuck; typing moves
    // nothing, and polling sees nothing.
    rig.mapper->setTextEditing(true);
    rig.mapper->commit(1);
    RAWFRAME_EXPECT(rig.mapper->releasedThisTick(kFirst, kMove) && !rig.mapper->committed(kFirst, kMove).on);
    rig.press(kKeyboard, key("space"));
    RAWFRAME_EXPECT(!rig.now(kJump).on && !rig.now(kMove).on);
    // An ungated context still hears the keyboard; the gamepad is untouched.
    rig.press(kKeyboard, key("key_t"));
    RAWFRAME_EXPECT(rig.now(kType).on);
    rig.press(kPad, pad("face_south"));
    RAWFRAME_EXPECT(rig.now(kJump).on);
    rig.release(kPad, pad("face_south"));
    // Editing ends with W still held: movement comes back.
    rig.mapper->setTextEditing(false);
    RAWFRAME_EXPECT(rig.now(kMove).on);
}

RAWFRAME_TEST(PlayersNeverShareActionState) {
    Rig rig;
    rig.press(kSecondPad, pad("face_south"));
    RAWFRAME_EXPECT(rig.now(kJump, kSecond).on && !rig.now(kJump, kFirst).on);
    rig.press(kPad, pad("face_south"));
    rig.release(kSecondPad, pad("face_south"));
    RAWFRAME_EXPECT(rig.now(kJump, kFirst).on && !rig.now(kJump, kSecond).on);
    // Unpairing a device releases what it held.
    rig.mapper->unpair(kPad);
    RAWFRAME_EXPECT(!rig.now(kJump, kFirst).on);
    rig.press(kPad, pad("face_south"));
    RAWFRAME_EXPECT(!rig.now(kJump, kFirst).on && rig.mapper->statistics().unpairedEvents == 1);
    // A device is paired once.
    RAWFRAME_EXPECT(!rig.mapper->pair(kSecondPad, DeviceClass::Gamepad, kFirst).has_value());
}

RAWFRAME_TEST(AFullQueueDropsTheOldestAndReleases) {
    auto mapper = *Mapper::create(actionSet(), {.players = 1, .maximumQueuedEventsPerPlayer = 4});
    RAWFRAME_EXPECT(mapper->pair(kKeyboard, DeviceClass::Keyboard, kFirst).has_value());
    RAWFRAME_EXPECT(mapper->activate(kFirst, kOnFoot).has_value());
    mapper->submit({.device = kKeyboard, .control = key("space"), .x = 1});
    mapper->update();
    RAWFRAME_EXPECT(mapper->current(kFirst, kJump).on);
    // Six events for a queue of four: two lost, among them possibly a
    // release, so everything held is let go before the rest apply.
    for (const std::string_view kName : {"key_a", "key_b", "key_e", "key_f", "key_g", "key_h"}) {
        mapper->submit({.device = kKeyboard, .control = key(kName), .x = 1});
    }
    mapper->update();
    RAWFRAME_EXPECT(mapper->statistics().droppedEvents == 2 && !mapper->current(kFirst, kJump).on);
}
