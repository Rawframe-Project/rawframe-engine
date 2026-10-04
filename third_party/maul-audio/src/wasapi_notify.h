// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's IMMNotificationClient. Its callbacks run on the
// system's threads and only raise a flag, which the notification drain
// takes.

#ifndef MAUL_AUDIO_SRC_WASAPI_NOTIFY_H
#define MAUL_AUDIO_SRC_WASAPI_NOTIFY_H

#define COBJMACROS
#define CONST_VTABLE
#include <mmdeviceapi.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct maudWasapiNotifier
{
    // The COM object; first, so its pointer is the notifier's.
    IMMNotificationClient client;
    // A device or default changed since the drain last looked.
    atomic_bool changed;
} maudWasapiNotifier;

// Sets the notifier up; it has no reference count of its own and lives
// as long as its owner, which unregisters it before freeing it.
void maudInitWasapiNotifier(maudWasapiNotifier* notifier);

// Whether anything changed since the last call.
bool maudTakeWasapiChanges(maudWasapiNotifier* notifier);

#endif // MAUL_AUDIO_SRC_WASAPI_NOTIFY_H
