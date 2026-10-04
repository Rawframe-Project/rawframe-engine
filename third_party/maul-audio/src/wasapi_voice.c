// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Voice processing on WASAPI. Windows maps a stream's category to a
// signal processing mode its driver defines; a communications stream
// gets the driver's echo cancellation, noise suppression and gain
// control, if it has them. Since build 22000 IAudioEffectsManager lists
// the effects on a stream, which ones are on, and which the stream may
// turn on or off. mingw-w64 does not declare that interface, so its
// vtable and the effect GUIDs (ksmedia.h) are spelled out here.

#include "wasapi_voice.h"

#include "voice.h"

#include <combaseapi.h>
#include <string.h>

typedef struct maudAudioEffect
{
    GUID id;
    BOOL canSetState;
    // AUDIO_EFFECT_STATE: 0 off, 1 on.
    int state;
} maudAudioEffect;

typedef struct maudEffectsManager maudEffectsManager;

// IAudioEffectsManager's vtable, in its declaration's order.
typedef struct maudEffectsManagerVtbl
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(maudEffectsManager*, REFIID, void**);
    ULONG(STDMETHODCALLTYPE* AddRef)(maudEffectsManager*);
    ULONG(STDMETHODCALLTYPE* Release)(maudEffectsManager*);
    HRESULT(STDMETHODCALLTYPE* RegisterCallback)(maudEffectsManager*, void*);
    HRESULT(STDMETHODCALLTYPE* UnregisterCallback)(maudEffectsManager*, void*);
    HRESULT(STDMETHODCALLTYPE* GetAudioEffects)(maudEffectsManager*, maudAudioEffect**, UINT32*);
    HRESULT(STDMETHODCALLTYPE* SetAudioEffectState)(maudEffectsManager*, GUID, int);
} maudEffectsManagerVtbl;

struct maudEffectsManager
{
    const maudEffectsManagerVtbl* lpVtbl;
};

static const GUID s_iidAudioClient2 = {
    0x726778CD, 0xF60A, 0x4EDA, {0x82, 0xDE, 0xE4, 0x76, 0x10, 0xCD, 0x78, 0xAA}};
static const GUID s_iidEffectsManager = {
    0x4460B3AE, 0x4B44, 0x4527, {0x86, 0x76, 0x75, 0x48, 0xA8, 0xAC, 0xD2, 0x60}};

// The effect GUIDs share all but their first field.
#define MAUD_EFFECT_GUID(first)                                                                    \
    {(first), 0x8211, 0x11E2, {0x8C, 0x70, 0x2C, 0x27, 0xD7, 0xF0, 0x01, 0xFA}}

static const struct
{
    GUID id;
    maudVoiceProcessing part;
} s_effects[] = {
    {MAUD_EFFECT_GUID(0x6F64ADBE), maud_voiceEchoCancellation},
    {MAUD_EFFECT_GUID(0x6F64ADBF), maud_voiceNoiseSuppression},
    {MAUD_EFFECT_GUID(0x6F64ADD0), maud_voiceNoiseSuppression},
    {MAUD_EFFECT_GUID(0x6F64ADC0), maud_voiceGainControl},
};

void maudWasapiAskForVoice(IAudioClient* client, const maudStreamCore* core)
{
    bool voice = core->def.voice != maud_voiceNone;
    bool input = core->def.direction == maud_directionInput;
    if (!voice && !input)
    {
        return;
    }
    IAudioClient2* client2 = nullptr;
    if (FAILED(IAudioClient_QueryInterface(client, &s_iidAudioClient2, (void**)&client2)))
    {
        return;
    }
    AudioClientProperties properties = {
        .cbSize = sizeof(properties),
        .bIsOffload = FALSE,
        .eCategory = voice ? AudioCategory_Communications : AudioCategory_Other,
        .Options = voice ? AUDCLNT_STREAMOPTIONS_NONE : AUDCLNT_STREAMOPTIONS_RAW,
    };
    // A device without raw processing refuses the raw option; the stream
    // then runs with its defaults.
    (void)IAudioClient2_SetClientProperties(client2, &properties);
    IAudioClient2_Release(client2);
}

// The part an effect is, or maud_voiceNone for the others.
static maudVoiceProcessing PartOf(const GUID* id)
{
    for (size_t i = 0; i < sizeof s_effects / sizeof s_effects[0]; ++i)
    {
        if (memcmp(id, &s_effects[i].id, sizeof *id) == 0)
        {
            return s_effects[i].part;
        }
    }
    return maud_voiceNone;
}

// Lists the effects, turning each voice effect the stream may set to
// what it asked for when set is true; returns the parts on, or -1 when
// the list cannot be read.
static int ReadEffects(maudEffectsManager* manager, maudVoiceProcessing asked, bool set)
{
    maudAudioEffect* effects = nullptr;
    UINT32 count = 0;
    if (FAILED(manager->lpVtbl->GetAudioEffects(manager, &effects, &count)))
    {
        return -1;
    }
    int active = maud_voiceNone;
    for (UINT32 i = 0; i < count; ++i)
    {
        maudVoiceProcessing part = PartOf(&effects[i].id);
        int wanted = (asked & part) != 0 ? 1 : 0;
        if (set && part != maud_voiceNone && effects[i].canSetState && effects[i].state != wanted)
        {
            (void)manager->lpVtbl->SetAudioEffectState(manager, effects[i].id, wanted);
        }
        active |= effects[i].state != 0 ? part : maud_voiceNone;
    }
    CoTaskMemFree(effects);
    return active;
}

void maudWasapiReportVoice(IAudioClient* client, maudStreamCore* core)
{
    if (core->def.direction != maud_directionInput)
    {
        return;
    }
    maudEffectsManager* manager = nullptr;
    if (FAILED(IAudioClient_GetService(client, &s_iidEffectsManager, (void**)&manager)))
    {
        return;
    }
    // Set, then read again: the state after setting is the report.
    int active = ReadEffects(manager, core->def.voice, true) < 0
                     ? -1
                     : ReadEffects(manager, core->def.voice, false);
    if (active >= 0)
    {
        maudReportVoice(core, (maudVoiceProcessing)active);
    }
    manager->lpVtbl->Release(manager);
}
