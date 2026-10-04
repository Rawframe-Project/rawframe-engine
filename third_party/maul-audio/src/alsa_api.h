// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libasound, opened at run time. Every function the backend calls goes
// through this table, so the library links nothing of ALSA's and runs
// where it is missing.

#ifndef MAUL_AUDIO_SRC_ALSA_API_H
#define MAUL_AUDIO_SRC_ALSA_API_H

#include <alsa/asoundlib.h>
#include <stdbool.h>

// Bytes set aside for each ALSA info or parameter structure, checked
// against its size when the library is loaded.
#define MAUD_ALSA_STRUCT_BYTES 1024

typedef struct maudAlsaApi
{
    void* library;
    snd_local_error_handler_t (*libErrorSetLocal)(snd_local_error_handler_t handler);
    int (*cardNext)(int* card);
    int (*ctlOpen)(snd_ctl_t** ctl, const char* name, int mode);
    int (*ctlClose)(snd_ctl_t* ctl);
    int (*ctlCardInfo)(snd_ctl_t* ctl, snd_ctl_card_info_t* info);
    size_t (*ctlCardInfoSizeof)(void);
    const char* (*ctlCardInfoGetId)(const snd_ctl_card_info_t* info);
    const char* (*ctlCardInfoGetName)(const snd_ctl_card_info_t* info);
    int (*ctlPcmNextDevice)(snd_ctl_t* ctl, int* device);
    int (*ctlPcmInfo)(snd_ctl_t* ctl, snd_pcm_info_t* info);
    size_t (*pcmInfoSizeof)(void);
    void (*pcmInfoSetDevice)(snd_pcm_info_t* info, unsigned int device);
    void (*pcmInfoSetSubdevice)(snd_pcm_info_t* info, unsigned int subdevice);
    void (*pcmInfoSetStream)(snd_pcm_info_t* info, snd_pcm_stream_t stream);
    const char* (*pcmInfoGetName)(const snd_pcm_info_t* info);
    int (*pcmOpen)(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode);
    int (*pcmClose)(snd_pcm_t* pcm);
    size_t (*hwParamsSizeof)(void);
    int (*hwParamsAny)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
    int (*hwParamsSetAccess)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_access_t access);
    int (*hwParamsSetFormat)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, snd_pcm_format_t format);
    int (*hwParamsSetChannels)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int channels);
    int (*hwParamsSetRateResample)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params,
                                   unsigned int resample);
    int (*hwParamsSetRate)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int rate,
                           int direction);
    int (*hwParamsSetRateNear)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, unsigned int* rate,
                               int* direction);
    int (*hwParamsSetPeriodSizeNear)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params,
                                     snd_pcm_uframes_t* frames, int* direction);
    int (*hwParamsSetBufferSizeNear)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params,
                                     snd_pcm_uframes_t* frames);
    int (*hwParams)(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
    int (*hwParamsGetPeriodSize)(const snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames,
                                 int* direction);
    int (*hwParamsGetBufferSize)(const snd_pcm_hw_params_t* params, snd_pcm_uframes_t* frames);
    size_t (*swParamsSizeof)(void);
    int (*swParamsCurrent)(snd_pcm_t* pcm, snd_pcm_sw_params_t* params);
    int (*swParamsSetStartThreshold)(snd_pcm_t* pcm, snd_pcm_sw_params_t* params,
                                     snd_pcm_uframes_t frames);
    int (*swParams)(snd_pcm_t* pcm, snd_pcm_sw_params_t* params);
    snd_pcm_chmap_t* (*pcmGetChmap)(snd_pcm_t* pcm);
    // The C library's free, as libasound links it: snd_pcm_get_chmap's
    // map is libasound's allocation, which it documents releasing with
    // free. No memory of the library's own goes through it.
    void (*chmapFree)(void* map);
    int (*pollDescriptorsCount)(snd_pcm_t* pcm);
    int (*pollDescriptors)(snd_pcm_t* pcm, struct pollfd* fds, unsigned int space);
    int (*pollDescriptorsRevents)(snd_pcm_t* pcm, struct pollfd* fds, unsigned int count,
                                  unsigned short* revents);
    snd_pcm_sframes_t (*pcmAvailUpdate)(snd_pcm_t* pcm);
    int (*pcmDelay)(snd_pcm_t* pcm, snd_pcm_sframes_t* delay);
    snd_pcm_sframes_t (*pcmWritei)(snd_pcm_t* pcm, const void* buffer, snd_pcm_uframes_t frames);
    snd_pcm_sframes_t (*pcmReadi)(snd_pcm_t* pcm, void* buffer, snd_pcm_uframes_t frames);
    int (*pcmRecover)(snd_pcm_t* pcm, int error, int silent);
    int (*pcmPrepare)(snd_pcm_t* pcm);
    int (*pcmStart)(snd_pcm_t* pcm);
    int (*pcmDrop)(snd_pcm_t* pcm);
} maudAlsaApi;

// Opens libasound and fills the table. False, with the table zeroed,
// when the library or a function is missing.
bool maudLoadAlsa(maudAlsaApi* api);

// Closes what maudLoadAlsa opened.
void maudUnloadAlsa(maudAlsaApi* api);

#endif // MAUL_AUDIO_SRC_ALSA_API_H
