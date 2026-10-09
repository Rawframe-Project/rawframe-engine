// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's state, shared by its context and stream modules.

#ifndef MAUL_AUDIO_SRC_AAUDIO_CORE_H
#define MAUL_AUDIO_SRC_AAUDIO_CORE_H

#include "context_core.h"
#include "device.h"

#include <aaudio/AAudio.h>
#include <jni.h>
#include <stdatomic.h>

// A stream's AAudio stream, whose data callback runs on AAudio's thread
// while it plays.
typedef struct maudAaudioStream
{
    maudStreamCore* core;
    AAudioStream* stream;
    bool playing;
    // The stream's xrun count when the callback last read it.
    int32_t xruns;
    // Raised by the error callback when the stream's device went away or
    // the stream failed; the drain opens the stream again.
    atomic_bool lost;
} maudAaudioStream;

// Bytes kept for a listed device's key and for its name, in UTF-8.
#define MAUD_AAUDIO_KEY_BYTES  128u
#define MAUD_AAUDIO_NAME_BYTES 128u

// One device of a scan: its AAudio id (0 for the default, which a
// stream opens without one) and the text its spec points into.
typedef struct maudAaudioEndpoint
{
    maudDirection direction;
    int32_t id;
    char key[MAUD_AAUDIO_KEY_BYTES];
    char name[MAUD_AAUDIO_NAME_BYTES];
} maudAaudioEndpoint;

// What the Java half reports from Android's threads, for the drain:
// devices changed, and the last focus change (AudioManager's value, 0
// for none since the last drain).
typedef struct maudAaudioSignals
{
    atomic_bool changed;
    atomic_int focus;
} maudAaudioSignals;

// The Java half (aaudio_java.c), present when the host gave a Java VM
// and an Android Context: global references to the Context and to the
// library's maul.audio.Devices object, and the class's methods.
typedef struct maudAaudioJava
{
    JavaVM* vm;
    jobject context;
    jobject devices;
    jmethodID list;
    jmethodID mayRecord;
    jmethodID askToRecord;
    jmethodID requestFocus;
    jmethodID spatializer;
    jmethodID close;
} maudAaudioJava;

// AAudio's calls past API 30, loaded at run time; NULL where the device
// is older.
typedef struct maudAaudioLate
{
    void* library;
    void (*setContentSpatialized)(AAudioStreamBuilder* builder, bool spatialized);
    void (*setSpatializationBehavior)(AAudioStreamBuilder* builder,
                                      aaudio_spatialization_behavior_t behavior);
    bool (*isContentSpatialized)(AAudioStream* stream);
} maudAaudioLate;

typedef struct maudAaudio
{
    maudContext* context;
    maudAaudioLate late;
    maudAaudioStream* streams;
    // The default output's rate and channels, from the probe.
    uint32_t rate;
    uint32_t channels;
    maudAaudioSignals signals;
    bool hasJava;
    maudAaudioJava java;
    // Room for a scan.
    maudAaudioEndpoint* endpoints;
    maudDeviceSpec* specs;
    uint32_t endpointCount;
    size_t bytes;
} maudAaudio;

#endif // MAUL_AUDIO_SRC_AAUDIO_CORE_H
