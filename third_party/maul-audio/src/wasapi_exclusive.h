// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// WASAPI's exclusive mode: the endpoint for one stream, in a format it
// takes as is.

#ifndef MAUL_AUDIO_SRC_WASAPI_EXCLUSIVE_H
#define MAUL_AUDIO_SRC_WASAPI_EXCLUSIVE_H

#include "wasapi_core.h"

#include <mmdeviceapi.h>

// Initializes entry's client for exclusive, event-driven use of device:
// the first format the device takes as is (32-bit float, 32-bit integers
// with 32 or 24 valid bits, 16-bit integers) at the stream's rate and
// channels, with a buffer of the stream's period, at least the device's
// minimum and aligned as it asks. maud_errorUnsupported when the device
// or the user's policy allows no exclusive use of any of them;
// maud_errorPlatform when another application holds the endpoint.
maudResult maudWasapiInitializeExclusive(maudWasapiStream* entry, IMMDevice* device);

#endif // MAUL_AUDIO_SRC_WASAPI_EXCLUSIVE_H
