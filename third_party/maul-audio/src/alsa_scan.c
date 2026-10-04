// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scanning the sound cards. Each card's control lists its PCM devices
// and, per direction, whether the device has it and its name; no PCM is
// opened, so no codec wakes.

#include "alsa_scan.h"

#include <stdio.h>
#include <string.h>

maudDeviceForm maudAlsaFormOfPcm(const char* name)
{
    static const char* const digital[] = {"HDMI", "DisplayPort", "IEC958", "S/PDIF", "SPDIF"};
    for (size_t i = 0; name != nullptr && i < sizeof(digital) / sizeof(digital[0]); ++i)
    {
        if (strstr(name, digital[i]) != nullptr)
        {
            return maud_formDigital;
        }
    }
    return maud_formUnknown;
}

// Lists one card device's endpoint in one direction, if the card has it.
static bool ScanEndpoint(const maudAlsaApi* api, snd_ctl_t* ctl, const char* cardId,
                         const char* cardName, int device, maudDirection direction,
                         maudAlsaEndpoint* endpoint)
{
    alignas(max_align_t) unsigned char infoBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_pcm_info_t* info = (snd_pcm_info_t*)infoBytes;
    memset(infoBytes, 0, sizeof(infoBytes));
    api->pcmInfoSetDevice(info, (unsigned int)device);
    api->pcmInfoSetSubdevice(info, 0);
    api->pcmInfoSetStream(info, direction == maud_directionOutput ? SND_PCM_STREAM_PLAYBACK
                                                                  : SND_PCM_STREAM_CAPTURE);
    if (api->ctlPcmInfo(ctl, info) < 0)
    {
        return false;
    }
    endpoint->direction = direction;
    endpoint->form = maudAlsaFormOfPcm(api->pcmInfoGetName(info));
    int keyLength =
        snprintf(endpoint->key, sizeof(endpoint->key), "hw:CARD=%s,DEV=%d", cardId, device);
    snprintf(endpoint->name, sizeof(endpoint->name), "%s, %s", cardName, api->pcmInfoGetName(info));
    return keyLength > 0 && (size_t)keyLength < sizeof(endpoint->key);
}

// Lists every endpoint of one card from count on; returns the new count.
static uint32_t ScanCard(const maudAlsaApi* api, int card, maudAlsaEndpoint* endpoints,
                         uint32_t count, uint32_t capacity)
{
    char ctlName[32];
    snprintf(ctlName, sizeof(ctlName), "hw:%d", card);
    snd_ctl_t* ctl = nullptr;
    if (api->ctlOpen(&ctl, ctlName, SND_CTL_NONBLOCK) < 0)
    {
        return count;
    }
    alignas(max_align_t) unsigned char cardBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_ctl_card_info_t* cardInfo = (snd_ctl_card_info_t*)cardBytes;
    memset(cardBytes, 0, sizeof(cardBytes));
    if (api->ctlCardInfo(ctl, cardInfo) == 0)
    {
        const char* id = api->ctlCardInfoGetId(cardInfo);
        const char* name = api->ctlCardInfoGetName(cardInfo);
        int device = -1;
        while (api->ctlPcmNextDevice(ctl, &device) == 0 && device >= 0)
        {
            for (int direction = 0; direction < 2 && count < capacity; ++direction)
            {
                count += ScanEndpoint(api, ctl, id, name, device, (maudDirection)direction,
                                      &endpoints[count])
                             ? 1u
                             : 0u;
            }
        }
    }
    api->ctlClose(ctl);
    return count;
}

uint32_t maudAlsaScan(const maudAlsaApi* api, maudAlsaEndpoint* endpoints, maudDeviceSpec* specs,
                      uint32_t capacity, size_t nameLimit)
{
    uint32_t count = 0;
    int card = -1;
    while (count < capacity && api->cardNext(&card) == 0 && card >= 0)
    {
        count = ScanCard(api, card, endpoints, count, capacity);
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        size_t nameLength = strlen(endpoints[i].name);
        specs[i] = (maudDeviceSpec){
            .info = {.direction = endpoints[i].direction, .form = endpoints[i].form},
            .name = endpoints[i].name,
            .nameLength = nameLength < nameLimit ? nameLength : nameLimit,
            .key = endpoints[i].key,
            .keyLength = strlen(endpoints[i].key),
        };
    }
    return count;
}
