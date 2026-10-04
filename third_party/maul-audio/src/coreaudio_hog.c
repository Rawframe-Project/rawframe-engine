// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hog mode. Setting kAudioDevicePropertyHogMode toggles: it takes a
// free device and gives back one this process holds, whatever value is
// passed; the owner, a pid or -1, is read before and after.

#include "coreaudio_hog.h"

#include <unistd.h>

static bool Owner(AudioObjectID object, pid_t* ownerOut)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal);
    UInt32 size = sizeof(*ownerOut);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, ownerOut) == noErr;
}

static bool Toggle(AudioObjectID object)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal);
    pid_t value = getpid();
    return AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(value), &value) == noErr;
}

maudResult maudCoreAudioTakeDevice(AudioObjectID object)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal);
    Boolean settable = false;
    pid_t owner = -1;
    if (!AudioObjectHasProperty(object, &address) ||
        AudioObjectIsPropertySettable(object, &address, &settable) != noErr || !settable ||
        !Owner(object, &owner))
    {
        return maud_errorUnsupported;
    }
    if (owner != -1)
    {
        return maud_errorPlatform;
    }
    return Toggle(object) && Owner(object, &owner) && owner == getpid() ? maud_success
                                                                        : maud_errorPlatform;
}

void maudCoreAudioGiveDevice(AudioObjectID object)
{
    pid_t owner = -1;
    if (Owner(object, &owner) && owner == getpid())
    {
        bool toggled = Toggle(object);
        (void)toggled;
    }
}
