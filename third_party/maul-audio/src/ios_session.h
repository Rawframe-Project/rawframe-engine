// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's audio session (AVAudioSession), in Objective-C.

#ifndef MAUL_AUDIO_SRC_IOS_SESSION_H
#define MAUL_AUDIO_SRC_IOS_SESSION_H

#include "ios_core.h"

// The session's current rate and output channels.
void maudIosSessionFormat(uint32_t* rate, uint32_t* channels);

// What the session adds on one side of a buffer, in nanoseconds: its
// output or input latency and its IO buffer.
int64_t maudIosSessionLatency(bool input);

// Starts observing the session's interruptions and route changes into
// signals; returns the observer, retained, or NULL.
void* maudIosSessionObserve(maudIosSignals* signals);

// Stops observing: once it returns, no report reaches the signals.
void maudIosSessionUnobserve(void* observer);

// Fills ports with the session's available inputs, at most capacity;
// returns the count.
uint32_t maudIosSessionInputs(maudIosPort* ports, uint32_t capacity);

// Makes the input whose UID is uid, length bytes, the session's
// preferred input; false when it is not available.
bool maudIosSessionPreferInput(const char* uid, size_t length);

// The forms the session's current route leads to, output and input.
void maudIosSessionRoute(maudDeviceForm* output, maudDeviceForm* input);

// Sets the category for the streams there are, outputs and inputs (iOS
// lets an input unit initialize only under a category that records),
// and activates the session while one runs or focus is asked for,
// deactivating it, so that others resume, otherwise. false when the
// session refuses.
bool maudIosSessionUpdate(maudIos* ios, maudIosUse use);

#endif // MAUL_AUDIO_SRC_IOS_SESSION_H
