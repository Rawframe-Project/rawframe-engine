#include "rawframe/world_kest/sound_doors.h"

#include <array>
#include <cmath>

namespace rawframe::world_kest {

namespace {

constexpr std::array<kest::Parameter, 2> kTakes = {kest::Parameter{kest::Slot::U32}, kest::Parameter{kest::Slot::F32}};

void volumeDoor(kest::DoorCall& call, void* context) noexcept {
    const auto* const kContext = static_cast<const SoundDoorContext*>(context);
    if (kContext == nullptr || kContext->volumes == nullptr) {
        return;
    }
    PresentedVolumes& volumes = *kContext->volumes;
    const auto kRole = static_cast<std::size_t>(call.integer(0));
    const auto kDecibels = static_cast<float>(call.real(1));
    // The master's and the other roles', never none's.
    if (kRole == 0 || kRole >= volumes.decibels.size() || !std::isfinite(kDecibels) ||
        volumes.decibels[kRole] == kDecibels) {
        return;
    }
    volumes.decibels[kRole] = kDecibels;
    ++volumes.revision;
}

} // namespace

result::Status addSoundDoors(kest::DoorTable& doors, const SoundDoorContext* context) {
    return doors.add(kest::Door{.name = "Sound.volume",
                                .function = &volumeDoor,
                                .context = const_cast<SoundDoorContext*>(context),
                                .takes = kTakes,
                                .safeForUntrusted = true});
}

} // namespace rawframe::world_kest
