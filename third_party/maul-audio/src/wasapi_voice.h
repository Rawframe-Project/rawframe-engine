// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Voice processing on WASAPI: the stream category asked for before a
// client is initialized, and the effects reported after.

#ifndef MAUL_AUDIO_SRC_WASAPI_VOICE_H
#define MAUL_AUDIO_SRC_WASAPI_VOICE_H

#include "wasapi_core.h"

// Before IAudioClient_Initialize: a stream that asks for voice
// processing, or a duplex stream's output that does, is a
// communications stream; an input that asks for none asks for the raw
// signal. A client without IAudioClient2, or a device that refuses,
// keeps its defaults.
void maudWasapiAskForVoice(IAudioClient* client, const maudStreamCore* core);

// After it, for an input: turns the asked-for parts on and the others
// off where Windows lets the stream choose, and reports the parts on.
// Before Windows 11 (build 22000), and under Wine, nothing is reported.
void maudWasapiReportVoice(IAudioClient* client, maudStreamCore* core);

#endif // MAUL_AUDIO_SRC_WASAPI_VOICE_H
