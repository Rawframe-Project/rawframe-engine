// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "pipewire_parse.h"

#include <pipewire/keys.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/json.h>
#include <spa/utils/string.h>
#include <string.h>

uint32_t maudPipewireChoiceDefault(const struct spa_pod* value)
{
    uint32_t count = 0;
    uint32_t choice = 0;
    const struct spa_pod* values = spa_pod_get_values(value, &count, &choice);
    if (values->type != SPA_TYPE_Int || count == 0)
    {
        return 0;
    }
    return (uint32_t)*(const int32_t*)SPA_POD_BODY_CONST(values);
}

bool maudPipewireNodeCard(const struct spa_dict* props, uint32_t* cardIdOut,
                          int32_t* profileDeviceOut)
{
    const char* card = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
    const char* profileDevice = spa_dict_lookup(props, "card.profile.device");
    *cardIdOut = 0;
    *profileDeviceOut = 0;
    return card != nullptr && profileDevice != nullptr && spa_atou32(card, cardIdOut, 10) &&
           spa_atoi32(profileDevice, profileDeviceOut, 10);
}

void maudPipewireParseDefaultName(const char* value, char* name, size_t nameBytes)
{
    name[0] = '\0';
    struct spa_json root;
    struct spa_json object;
    if (value == nullptr)
    {
        return;
    }
    spa_json_init(&root, value, strlen(value));
    if (spa_json_enter_object(&root, &object) <= 0)
    {
        return;
    }
    char key[16];
    while (spa_json_get_string(&object, key, sizeof(key)) > 0)
    {
        if (spa_streq(key, "name"))
        {
            if (spa_json_get_string(&object, name, (int)nameBytes) <= 0)
            {
                name[0] = '\0';
            }
            return;
        }
        const char* skipped;
        if (spa_json_next(&object, &skipped) <= 0)
        {
            return;
        }
    }
}

uint32_t maudPipewireGraphRate(uint32_t forceRate, uint32_t clockRate, uint32_t fallback)
{
    return forceRate != 0 ? forceRate : clockRate != 0 ? clockRate : fallback;
}

// The PipeWire channel position of each speaker.
static uint32_t PositionOf(maudSpeaker speaker)
{
    static const uint32_t positions[] = {
        [maud_speakerNone] = SPA_AUDIO_CHANNEL_UNKNOWN,
        [maud_speakerFrontLeft] = SPA_AUDIO_CHANNEL_FL,
        [maud_speakerFrontRight] = SPA_AUDIO_CHANNEL_FR,
        [maud_speakerFrontCenter] = SPA_AUDIO_CHANNEL_FC,
        [maud_speakerLowFrequency] = SPA_AUDIO_CHANNEL_LFE,
        [maud_speakerBackLeft] = SPA_AUDIO_CHANNEL_RL,
        [maud_speakerBackRight] = SPA_AUDIO_CHANNEL_RR,
        [maud_speakerSideLeft] = SPA_AUDIO_CHANNEL_SL,
        [maud_speakerSideRight] = SPA_AUDIO_CHANNEL_SR,
        [maud_speakerTopFrontLeft] = SPA_AUDIO_CHANNEL_TFL,
        [maud_speakerTopFrontRight] = SPA_AUDIO_CHANNEL_TFR,
        [maud_speakerTopBackLeft] = SPA_AUDIO_CHANNEL_TRL,
        [maud_speakerTopBackRight] = SPA_AUDIO_CHANNEL_TRR,
    };
    return positions[speaker];
}

const struct spa_pod* maudPipewireBuildFormat(maudChannelLayout layout, uint32_t rate,
                                              uint8_t* buffer, size_t bytes)
{
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, (uint32_t)bytes);
    uint32_t channels = maudGetLayoutChannelCount(layout);
    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = rate,
        .channels = channels,
    };
    for (uint32_t c = 0; c < channels; ++c)
    {
        info.position[c] =
            channels == 1 ? SPA_AUDIO_CHANNEL_MONO : PositionOf(maudGetLayoutSpeaker(layout, c));
    }
    return spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
}

const char* maudPipewireMediaCategory(maudDirection direction)
{
    return direction == maud_directionOutput ? "Playback" : "Capture";
}

const char* maudPipewireMediaRole(maudDeviceRole role)
{
    return role == maud_roleCommunications ? "Communication" : "Game";
}
