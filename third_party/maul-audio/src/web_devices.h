// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's audio devices, as enumerateDevices lists them, and the
// output device the context's AudioContext plays to.

#ifndef MAUL_AUDIO_SRC_WEB_DEVICES_H
#define MAUL_AUDIO_SRC_WEB_DEVICES_H

#include "web_core.h"

// Starts listing the page's devices now and on each devicechange.
void maudWebListDevices(int handle);

// Lists them again, as when the microphone is granted and ids and labels
// appear.
void maudWebRelistDevices(int handle);

// Stops listening to devicechange.
void maudWebStopListing(int handle);

// Brings the device table in line with the latest list, if a new one
// arrived; the two Default devices stay.
maudResult maudWebSyncDevices(maudContext* context);

// Whether an output stream on device may open: no other output plays
// to another device. Default is the empty sink.
bool maudWebSinkFree(const maudContext* context, maudDeviceId device);

// Points the AudioContext at an output stream's device.
void maudWebSetSink(maudContext* context, const maudStreamSlot* slot);

// The device id a capture stream asks getUserMedia for, written into
// out as UTF-8; empty for Default.
void maudWebCaptureDevice(const maudContext* context, const maudStreamSlot* slot, char* out,
                          size_t capacity);

#endif // MAUL_AUDIO_SRC_WEB_DEVICES_H
