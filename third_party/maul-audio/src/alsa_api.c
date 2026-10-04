// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libasound. The library is reference counted by the dynamic
// loader, so each context opens and closes it on its own. Every
// function of the table has one entry below, and the build fails when
// one is missing.

#include "alsa_api.h"

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

typedef struct ApiEntry
{
    const char* name;
    size_t offset;
} ApiEntry;

#define ENTRY(field, name) {name, offsetof(maudAlsaApi, field)}

static const ApiEntry s_entries[] = {
    ENTRY(libErrorSetLocal, "snd_lib_error_set_local"),
    ENTRY(cardNext, "snd_card_next"),
    ENTRY(ctlOpen, "snd_ctl_open"),
    ENTRY(ctlClose, "snd_ctl_close"),
    ENTRY(ctlCardInfo, "snd_ctl_card_info"),
    ENTRY(ctlCardInfoSizeof, "snd_ctl_card_info_sizeof"),
    ENTRY(ctlCardInfoGetId, "snd_ctl_card_info_get_id"),
    ENTRY(ctlCardInfoGetName, "snd_ctl_card_info_get_name"),
    ENTRY(ctlPcmNextDevice, "snd_ctl_pcm_next_device"),
    ENTRY(ctlPcmInfo, "snd_ctl_pcm_info"),
    ENTRY(pcmInfoSizeof, "snd_pcm_info_sizeof"),
    ENTRY(pcmInfoSetDevice, "snd_pcm_info_set_device"),
    ENTRY(pcmInfoSetSubdevice, "snd_pcm_info_set_subdevice"),
    ENTRY(pcmInfoSetStream, "snd_pcm_info_set_stream"),
    ENTRY(pcmInfoGetName, "snd_pcm_info_get_name"),
    ENTRY(pcmOpen, "snd_pcm_open"),
    ENTRY(pcmClose, "snd_pcm_close"),
    ENTRY(hwParamsSizeof, "snd_pcm_hw_params_sizeof"),
    ENTRY(hwParamsAny, "snd_pcm_hw_params_any"),
    ENTRY(hwParamsSetAccess, "snd_pcm_hw_params_set_access"),
    ENTRY(hwParamsSetFormat, "snd_pcm_hw_params_set_format"),
    ENTRY(hwParamsSetChannels, "snd_pcm_hw_params_set_channels"),
    ENTRY(hwParamsSetRateResample, "snd_pcm_hw_params_set_rate_resample"),
    ENTRY(hwParamsSetRate, "snd_pcm_hw_params_set_rate"),
    ENTRY(hwParamsSetRateNear, "snd_pcm_hw_params_set_rate_near"),
    ENTRY(hwParamsSetPeriodSizeNear, "snd_pcm_hw_params_set_period_size_near"),
    ENTRY(hwParamsSetBufferSizeNear, "snd_pcm_hw_params_set_buffer_size_near"),
    ENTRY(hwParams, "snd_pcm_hw_params"),
    ENTRY(hwParamsGetPeriodSize, "snd_pcm_hw_params_get_period_size"),
    ENTRY(hwParamsGetBufferSize, "snd_pcm_hw_params_get_buffer_size"),
    ENTRY(swParamsSizeof, "snd_pcm_sw_params_sizeof"),
    ENTRY(swParamsCurrent, "snd_pcm_sw_params_current"),
    ENTRY(swParamsSetStartThreshold, "snd_pcm_sw_params_set_start_threshold"),
    ENTRY(swParams, "snd_pcm_sw_params"),
    ENTRY(pcmGetChmap, "snd_pcm_get_chmap"),
    ENTRY(chmapFree, "free"),
    ENTRY(pollDescriptorsCount, "snd_pcm_poll_descriptors_count"),
    ENTRY(pollDescriptors, "snd_pcm_poll_descriptors"),
    ENTRY(pollDescriptorsRevents, "snd_pcm_poll_descriptors_revents"),
    ENTRY(pcmAvailUpdate, "snd_pcm_avail_update"),
    ENTRY(pcmDelay, "snd_pcm_delay"),
    ENTRY(pcmWritei, "snd_pcm_writei"),
    ENTRY(pcmReadi, "snd_pcm_readi"),
    ENTRY(pcmRecover, "snd_pcm_recover"),
    ENTRY(pcmPrepare, "snd_pcm_prepare"),
    ENTRY(pcmStart, "snd_pcm_start"),
    ENTRY(pcmDrop, "snd_pcm_drop"),
};

// The table holds the library handle and then only function pointers.
static_assert(sizeof(s_entries) / sizeof(s_entries[0]) ==
                  (sizeof(maudAlsaApi) - offsetof(maudAlsaApi, libErrorSetLocal)) /
                      sizeof(void (*)(void)),
              "every function of maudAlsaApi has an entry");

bool maudLoadAlsa(maudAlsaApi* api)
{
    *api = (maudAlsaApi){0};
    api->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); ++i)
    {
        void* function = dlsym(api->library, s_entries[i].name);
        if (function == nullptr)
        {
            maudUnloadAlsa(api);
            return false;
        }
        // The loader returns an object pointer; the slot has the
        // function's pointer type and the same size on every platform
        // with dlopen.
        memcpy((char*)api + s_entries[i].offset, (const void*)&function, sizeof(function));
    }
    return true;
}

void maudUnloadAlsa(maudAlsaApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    *api = (maudAlsaApi){0};
}
