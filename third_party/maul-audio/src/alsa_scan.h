// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The hardware endpoints of the machine's sound cards, as the control
// interface lists them.

#ifndef MAUL_AUDIO_SRC_ALSA_SCAN_H
#define MAUL_AUDIO_SRC_ALSA_SCAN_H

#include "alsa_api.h"
#include "context_core.h"
#include "device.h"

// Bytes of an endpoint's key, "hw:CARD=<id>,DEV=<n>", and name.
#define MAUD_ALSA_KEY_BYTES   64
#define MAUD_ALSA_LABEL_BYTES 128

typedef struct maudAlsaEndpoint
{
    maudDirection direction;
    maudDeviceForm form;
    char key[MAUD_ALSA_KEY_BYTES];
    char name[MAUD_ALSA_LABEL_BYTES];
} maudAlsaEndpoint;

// Lists up to capacity endpoints into endpoints, and into specs the
// device each is, pointing into endpoints, opening only the cards'
// controls; returns how many it listed. Names are cut to nameLimit
// bytes.
uint32_t maudAlsaScan(const maudAlsaApi* api, maudAlsaEndpoint* endpoints, maudDeviceSpec* specs,
                      uint32_t capacity, size_t nameLimit);

// The form a hardware PCM's name says: digital for HDMI, DisplayPort,
// IEC958 and S/PDIF, which drivers name so; unknown otherwise, since a
// PCM says nothing of an analog jack.
maudDeviceForm maudAlsaFormOfPcm(const char* name);

#endif // MAUL_AUDIO_SRC_ALSA_SCAN_H
