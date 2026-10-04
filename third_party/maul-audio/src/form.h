// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a device leads to, from the names PipeWire and PulseAudio give a
// device's form factor or its port's type.

#ifndef MAUL_AUDIO_SRC_FORM_H
#define MAUL_AUDIO_SRC_FORM_H

#include "maul-audio/device.h"

// The form a form factor or port type name means for a device of
// direction: "headphone" and "headphones" are headphones, "internal" is
// speakers on an output and a microphone on an input. Unknown for NULL,
// a connection (usb, bluetooth, network) or any other name.
maudDeviceForm maudFormOfName(const char* name, maudDirection direction);

#endif // MAUL_AUDIO_SRC_FORM_H
