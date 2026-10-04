// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Form factor and port type names, as PipeWire's device.form-factor,
// PulseAudio's device.form_factor and both servers' port types spell
// them.

#include "form.h"

#include <string.h>

typedef struct Name
{
    const char* name;
    maudDeviceForm form;
} Name;

static const Name s_names[] = {
    {"speaker", maud_formSpeakers},      {"car", maud_formSpeakers},
    {"hifi", maud_formSpeakers},         {"portable", maud_formSpeakers},
    {"tv", maud_formSpeakers},           {"computer", maud_formSpeakers},
    {"headphone", maud_formHeadphones},  {"headphones", maud_formHeadphones},
    {"headset", maud_formHeadset},       {"hands-free", maud_formHeadset},
    {"handsfree", maud_formHeadset},     {"handset", maud_formHandset},
    {"earpiece", maud_formHandset},      {"phone", maud_formHandset},
    {"microphone", maud_formMicrophone}, {"mic", maud_formMicrophone},
    {"webcam", maud_formMicrophone},     {"line", maud_formLine},
    {"analog", maud_formLine},           {"hdmi", maud_formDigital},
    {"spdif", maud_formDigital},
};

maudDeviceForm maudFormOfName(const char* name, maudDirection direction)
{
    if (name == nullptr)
    {
        return maud_formUnknown;
    }
    if (strcmp(name, "internal") == 0)
    {
        return direction == maud_directionOutput ? maud_formSpeakers : maud_formMicrophone;
    }
    for (size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]); ++i)
    {
        if (strcmp(name, s_names[i].name) == 0)
        {
            return s_names[i].form;
        }
    }
    return maud_formUnknown;
}
