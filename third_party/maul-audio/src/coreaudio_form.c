// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Data sources and transports. The data source codes are IOKit's port
// subtypes (IOAudioTypes.h), written out so the library needs no IOKit
// header.

#include "coreaudio_form.h"

#define FOUR(a, b, c, d)                                                                           \
    (((UInt32)(a) << 24) | ((UInt32)(b) << 16) | ((UInt32)(c) << 8) | (UInt32)(d))

static AudioObjectPropertyScope ScopeOf(maudDirection direction)
{
    return direction == maud_directionOutput ? kAudioObjectPropertyScopeOutput
                                             : kAudioObjectPropertyScopeInput;
}

static maudDeviceForm FormOfSource(UInt32 source)
{
    switch (source)
    {
    case FOUR('i', 's', 'p', 'k'):
    case FOUR('e', 's', 'p', 'k'):
        return maud_formSpeakers;
    case FOUR('h', 'd', 'p', 'n'):
        return maud_formHeadphones;
    case FOUR('i', 'm', 'i', 'c'):
    case FOUR('e', 'm', 'i', 'c'):
        return maud_formMicrophone;
    case FOUR('l', 'i', 'n', 'e'):
        return maud_formLine;
    case FOUR('s', 'p', 'd', 'f'):
        return maud_formDigital;
    default:
        return maud_formUnknown;
    }
}

maudDeviceForm maudCoreAudioFormOf(AudioObjectID object, maudDirection direction)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyDataSource, ScopeOf(direction));
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    maudDeviceForm form =
        AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr
            ? FormOfSource(value)
            : maud_formUnknown;
    if (form != maud_formUnknown)
    {
        return form;
    }
    address =
        maudCoreAudioAddress(kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal);
    size = sizeof(value);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr &&
        (value == kAudioDeviceTransportTypeHDMI || value == kAudioDeviceTransportTypeDisplayPort))
    {
        return maud_formDigital;
    }
    return maud_formUnknown;
}

static AudioObjectPropertyAddress SourceOf(const maudCoreAudioWatch* watch)
{
    return maudCoreAudioAddress(kAudioDevicePropertyDataSource, ScopeOf(watch->direction));
}

// Whether the scan's count endpoints include the watched one.
static bool Scanned(const maudCoreAudio* coreaudio, uint32_t count, const maudCoreAudioWatch* watch)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (coreaudio->endpoints[i].object == watch->object &&
            coreaudio->specs[i].info.direction == watch->direction)
        {
            return true;
        }
    }
    return false;
}

static void Unwatch(maudCoreAudio* coreaudio, uint32_t index)
{
    AudioObjectPropertyAddress address = SourceOf(&coreaudio->watched[index]);
    OSStatus status = AudioObjectRemovePropertyListenerBlock(
        coreaudio->watched[index].object, &address, coreaudio->queue, coreaudio->listener);
    (void)status;
    coreaudio->watched[index] = coreaudio->watched[--coreaudio->watchedCount];
}

void maudCoreAudioWatchSources(maudCoreAudio* coreaudio, uint32_t count)
{
    if (coreaudio->listener == nullptr)
    {
        return;
    }
    for (uint32_t w = coreaudio->watchedCount; w > 0; --w)
    {
        if (!Scanned(coreaudio, count, &coreaudio->watched[w - 1]))
        {
            Unwatch(coreaudio, w - 1);
        }
    }
    for (uint32_t i = 0; i < count && coreaudio->watchedCount < coreaudio->watchCapacity; ++i)
    {
        maudCoreAudioWatch watch = {coreaudio->endpoints[i].object,
                                    coreaudio->specs[i].info.direction};
        bool watched = false;
        for (uint32_t w = 0; w < coreaudio->watchedCount && !watched; ++w)
        {
            watched = coreaudio->watched[w].object == watch.object &&
                      coreaudio->watched[w].direction == watch.direction;
        }
        AudioObjectPropertyAddress address = SourceOf(&watch);
        if (!watched && AudioObjectHasProperty(watch.object, &address) &&
            AudioObjectAddPropertyListenerBlock(watch.object, &address, coreaudio->queue,
                                                coreaudio->listener) == noErr)
        {
            coreaudio->watched[coreaudio->watchedCount++] = watch;
        }
    }
}

void maudCoreAudioUnwatchSources(maudCoreAudio* coreaudio)
{
    while (coreaudio->watchedCount > 0)
    {
        Unwatch(coreaudio, coreaudio->watchedCount - 1);
    }
}
