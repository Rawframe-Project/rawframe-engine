#pragma once

// Which local player each of a client's devices plays (SPEC-0029's
// explicit pairing table, D363): a device is paired as it connects, by the
// client's policy, and let go as it goes. Device identity is never player
// identity: a player is a slot, and its devices are whichever the table
// holds for it.

#include "rawframe/input/controls.h"
#include "rawframe/input/mapper.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::input {

/// How devices are paired as they connect: every one to the first player
/// (a single local player's merged pairing); the keyboard and mouse to the
/// first and each gamepad to the first other player without one; or each
/// gamepad to the first player without one, from the first, the keyboard
/// and mouse to the first too. A gamepad with no player left is unpaired.
enum class PairingPolicy : std::uint8_t {
    Merged,
    KeyboardFirst,
    Gamepads
};

/// The policy a configuration names: `merged`, `keyboard_first`, or
/// `gamepads`; none for another word.
[[nodiscard]] std::optional<PairingPolicy> pairingPolicyNamed(std::string_view name) noexcept;

class Pairing {
public:
    /// For `players` local players, at least one.
    Pairing(PairingPolicy policy, std::size_t players) noexcept;

    /// The player the device that connects plays, recorded; none when the
    /// policy leaves it unpaired, counted. One already paired keeps its
    /// player.
    std::optional<PlayerSlot> connect(DeviceId device, DeviceClass deviceClass);
    /// The device went: its player's slot for its class is free.
    void disconnect(DeviceId device);
    /// The player a device plays; none for one unpaired.
    [[nodiscard]] std::optional<PlayerSlot> playerOf(DeviceId device) const noexcept;

    [[nodiscard]] std::size_t players() const noexcept {
        return players_;
    }
    /// Devices left unpaired as they connected.
    [[nodiscard]] std::uint64_t unpaired() const noexcept {
        return unpaired_;
    }

private:
    struct Paired {
        DeviceId device;
        DeviceClass deviceClass = DeviceClass::Keyboard;
        PlayerSlot player;
    };

    PairingPolicy policy_;
    std::size_t players_;
    std::vector<Paired> table_;
    std::uint64_t unpaired_ = 0;
};

} // namespace rawframe::input
