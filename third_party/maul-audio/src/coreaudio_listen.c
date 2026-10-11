// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's listeners on the system object. A restart of the audio
// server (kAudioHardwarePropertyServiceRestarted) asks a client to
// establish again any state it had, its listeners among it
// (AudioHardware.h); the drain does so when the change block reports
// one.

#include "coreaudio_listen.h"

#include "coreaudio_form.h"

#include <Block.h>

static const AudioObjectPropertySelector s_watched[] = {
    kAudioHardwarePropertyDevices,
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioHardwarePropertyDefaultInputDevice,
    kAudioHardwarePropertyServiceRestarted,
};

#define WATCHED_COUNT (sizeof(s_watched) / sizeof(s_watched[0]))

// Adds the block to every watched property, each tried whatever the
// others did.
static void AddSystem(maudCoreAudio* coreaudio)
{
    coreaudio->listening = true;
    for (size_t i = 0; i < WATCHED_COUNT; ++i)
    {
        AudioObjectPropertyAddress address =
            maudCoreAudioAddress(s_watched[i], kAudioObjectPropertyScopeGlobal);
        bool added = coreaudio->hal->addListener(kAudioObjectSystemObject, &address,
                                                 coreaudio->queue, coreaudio->listener) == noErr;
        coreaudio->listening = coreaudio->listening && added;
    }
}

static void RemoveSystem(maudCoreAudio* coreaudio)
{
    for (size_t i = 0; i < WATCHED_COUNT; ++i)
    {
        AudioObjectPropertyAddress address =
            maudCoreAudioAddress(s_watched[i], kAudioObjectPropertyScopeGlobal);
        OSStatus status = coreaudio->hal->removeListener(kAudioObjectSystemObject, &address,
                                                         coreaudio->queue, coreaudio->listener);
        (void)status;
    }
}

void maudCoreAudioListen(maudCoreAudio* coreaudio)
{
    atomic_bool* changed = &coreaudio->changed;
    atomic_bool* restarted = &coreaudio->restarted;
    coreaudio->listener = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress* addresses) {
      for (UInt32 i = 0; i < count; ++i)
      {
          if (addresses[i].mSelector == kAudioHardwarePropertyServiceRestarted)
          {
              atomic_store_explicit(restarted, true, memory_order_release);
          }
      }
      atomic_store_explicit(changed, true, memory_order_release);
    });
    AddSystem(coreaudio);
}

bool maudCoreAudioRelisten(maudCoreAudio* coreaudio)
{
    if (!atomic_exchange_explicit(&coreaudio->restarted, false, memory_order_acq_rel) ||
        coreaudio->listener == nullptr)
    {
        return false;
    }
    maudCoreAudioUnwatchSources(coreaudio);
    RemoveSystem(coreaudio);
    AddSystem(coreaudio);
    return true;
}

void maudCoreAudioStopListening(maudCoreAudio* coreaudio)
{
    if (coreaudio->listener == nullptr)
    {
        return;
    }
    maudCoreAudioUnwatchSources(coreaudio);
    RemoveSystem(coreaudio);
    dispatch_sync(coreaudio->queue, ^{
                  });
    Block_release(coreaudio->listener);
    coreaudio->listener = nullptr;
}
