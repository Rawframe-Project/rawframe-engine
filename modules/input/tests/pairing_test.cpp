// The pairing table (SPEC-0029, D363): devices paired to local players as
// they connect, by policy, and a client's feed routed to each player's.

#include "rawframe/input/feed.h"
#include "rawframe/input/pairing.h"
#include "rawframe/test/test.h"

#include <array>

using namespace rawframe;
using input::DeviceClass;
using input::DeviceId;
using input::PlayerSlot;

RAWFRAME_TEST(DevicesArePairedByPolicy) {
    // Merged: everything plays the one player.
    input::Pairing merged(input::PairingPolicy::Merged, 1);
    RAWFRAME_EXPECT(merged.connect(DeviceId{1}, DeviceClass::Keyboard) == PlayerSlot{0} &&
                    merged.connect(DeviceId{2}, DeviceClass::Gamepad) == PlayerSlot{0} &&
                    merged.connect(DeviceId{3}, DeviceClass::Gamepad) == PlayerSlot{0});
    // Keyboard first: the keyboard and mouse the first player's, each
    // gamepad the next player's without one, one past them unpaired until
    // a player's goes.
    input::Pairing split(input::PairingPolicy::KeyboardFirst, 3);
    RAWFRAME_EXPECT(split.connect(DeviceId{1}, DeviceClass::Keyboard) == PlayerSlot{0} &&
                    split.connect(DeviceId{2}, DeviceClass::Mouse) == PlayerSlot{0} &&
                    split.connect(DeviceId{3}, DeviceClass::Gamepad) == PlayerSlot{1} &&
                    split.connect(DeviceId{4}, DeviceClass::Gamepad) == PlayerSlot{2} &&
                    !split.connect(DeviceId{5}, DeviceClass::Gamepad).has_value() && split.unpaired() == 1);
    RAWFRAME_EXPECT(split.connect(DeviceId{3}, DeviceClass::Gamepad) == PlayerSlot{1});
    split.disconnect(DeviceId{3});
    RAWFRAME_EXPECT(!split.playerOf(DeviceId{3}).has_value() &&
                    split.connect(DeviceId{6}, DeviceClass::Gamepad) == PlayerSlot{1});
    // Gamepads: from the first player, the keyboard the first's too.
    input::Pairing pads(input::PairingPolicy::Gamepads, 2);
    RAWFRAME_EXPECT(pads.connect(DeviceId{1}, DeviceClass::Gamepad) == PlayerSlot{0} &&
                    pads.connect(DeviceId{2}, DeviceClass::Keyboard) == PlayerSlot{0} &&
                    pads.connect(DeviceId{3}, DeviceClass::Gamepad) == PlayerSlot{1});
    // A single player keyboard first still has its gamepad.
    input::Pairing alone(input::PairingPolicy::KeyboardFirst, 1);
    RAWFRAME_EXPECT(alone.connect(DeviceId{1}, DeviceClass::Gamepad) == PlayerSlot{0});
    RAWFRAME_EXPECT(input::pairingPolicyNamed("keyboard_first") == input::PairingPolicy::KeyboardFirst &&
                    !input::pairingPolicyNamed("everyone").has_value());
}

RAWFRAME_TEST(AFeedIsRoutedToItsPlayersFeeds) {
    input::Feed client;
    std::array<input::Feed, 2> feeds;
    const std::array<input::Feed*, 2> kPlayers = {&feeds[0], &feeds[1]};
    input::Pairing pairing(input::PairingPolicy::KeyboardFirst, 2);
    client.connect(DeviceId{1}, DeviceClass::Keyboard);
    client.connect(DeviceId{2}, DeviceClass::Gamepad);
    client.connect(DeviceId{3}, DeviceClass::Gamepad);
    client.submit({.device = DeviceId{1}, .x = 1});
    client.submit({.device = DeviceId{2}, .x = 1});
    client.submit({.device = DeviceId{2}, .x = 0});
    client.submit({.device = DeviceId{3}, .x = 1});
    client.releaseAll();
    client.route(pairing, kPlayers);
    // The keyboard's coming, its event, and the release to the first; the
    // first gamepad's coming, its two events, and the release to the
    // second; the second gamepad nowhere, its event counted.
    RAWFRAME_EXPECT(client.waiting() == 0 && feeds[0].waiting() == 3 && feeds[1].waiting() == 4 &&
                    client.unrouted() == 1 && pairing.unpaired() == 1);
    client.disconnect(DeviceId{2});
    client.route(pairing, kPlayers);
    RAWFRAME_EXPECT(feeds[1].waiting() == 5 && !pairing.playerOf(DeviceId{2}).has_value());
}
