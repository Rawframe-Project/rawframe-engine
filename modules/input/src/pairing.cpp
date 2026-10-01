#include "rawframe/input/pairing.h"

#include <algorithm>

namespace rawframe::input {

std::optional<PairingPolicy> pairingPolicyNamed(std::string_view name) noexcept {
    if (name == "merged") {
        return PairingPolicy::Merged;
    }
    if (name == "keyboard_first") {
        return PairingPolicy::KeyboardFirst;
    }
    if (name == "gamepads") {
        return PairingPolicy::Gamepads;
    }
    return std::nullopt;
}

Pairing::Pairing(PairingPolicy policy, std::size_t players) noexcept
    : policy_(policy), players_(std::max<std::size_t>(players, 1)) {
}

std::optional<PlayerSlot> Pairing::connect(DeviceId device, DeviceClass deviceClass) {
    if (const auto kHeld = playerOf(device)) {
        return kHeld;
    }
    std::optional<PlayerSlot> player;
    if (policy_ == PairingPolicy::Merged || deviceClass != DeviceClass::Gamepad) {
        player = PlayerSlot{0};
    } else {
        // The first player from where gamepads start who has none.
        const std::size_t kFirst = policy_ == PairingPolicy::KeyboardFirst && players_ > 1 ? 1 : 0;
        for (std::size_t slot = kFirst; slot < players_ && !player.has_value(); ++slot) {
            const bool kHasOne = std::ranges::any_of(table_, [slot](const Paired& each) {
                return each.player.value == slot && each.deviceClass == DeviceClass::Gamepad;
            });
            if (!kHasOne) {
                player = PlayerSlot{static_cast<std::uint8_t>(slot)};
            }
        }
    }
    if (!player.has_value()) {
        ++unpaired_;
        return std::nullopt;
    }
    table_.push_back(Paired{.device = device, .deviceClass = deviceClass, .player = *player});
    return player;
}

void Pairing::disconnect(DeviceId device) {
    std::erase_if(table_, [device](const Paired& each) {
        return each.device == device;
    });
}

std::optional<PlayerSlot> Pairing::playerOf(DeviceId device) const noexcept {
    const auto kPaired = std::ranges::find(table_, device, &Paired::device);
    return kPaired != table_.end() ? std::optional{kPaired->player} : std::nullopt;
}

} // namespace rawframe::input
